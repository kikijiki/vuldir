#include "vuldir/api/Binder.hpp"
#include "vuldir/api/Buffer.hpp"
#include "vuldir/api/CommandBuffer.hpp"
#include "vuldir/api/Device.hpp"
#include "vuldir/api/Image.hpp"
#include "vuldir/api/Pipeline.hpp"
#include "vuldir/api/dx/DxUti.hpp"
#include "vuldir/core/Uti.hpp"

using namespace vd;

CommandPool::CommandPool(Device& device_, QueueType type_):
  device{device_}, type{type_}, handle{}, dxType{}
{
  if(!isValid(type))
    throw std::invalid_argument("Command pool queue type is invalid");
  switch(type) {
    case QueueType::Graphics:
      dxType = D3D12_COMMAND_LIST_TYPE_DIRECT;
      break;
    case QueueType::Compute:
      dxType = D3D12_COMMAND_LIST_TYPE_COMPUTE;
      break;
    case QueueType::Copy:
      dxType = D3D12_COMMAND_LIST_TYPE_COPY;
      break;
  }

  VDDxTry(
    device.api().CreateCommandAllocator(dxType, IID_PPV_ARGS(&handle)));
}

CommandPool::~CommandPool() {}

void CommandPool::Reset() { VDDxTry(handle->Reset()); }

CommandBuffer::CommandBuffer(Device& device, CommandPool& pool):
  m_device{device},
  m_pool{pool},
  m_state{State::Closed},
  m_bindPoint{BindPoint::Graphics},
  m_pipelineBound{false},
  m_indexBufferBound{false},
  m_rendering{false},
  m_barriers{},
  m_handle{}
{
  VD_MARKER_SCOPED();

  if(&device != &pool.device)
    throw std::invalid_argument(
      "Command pool belongs to another device");

  VDDxTry(m_device.api().CreateCommandList1(
    0u, pool.dxType, D3D12_COMMAND_LIST_FLAG_NONE,
    IID_PPV_ARGS(&m_handle)));

  reset(false);
}

CommandBuffer::~CommandBuffer() {}

void CommandBuffer::Reset() { reset(false); }

void CommandBuffer::reset(bool resetPool)
{
  VD_MARKER_SCOPED();

  if(m_state == State::Recording) {
    throw std::runtime_error(
      "CommandBuffer::Reset cannot recover a Recording buffer; call "
      "End first");
  }
  if(m_state != State::Closed) return;

  if(resetPool) m_pool.Reset();

  VDDxTry(m_handle->Reset(m_pool.handle.Get(), nullptr));

  m_barriers.barriers.clear();
  m_barriers.pendingBuffers.clear();
  m_barriers.pendingImages.clear();

  m_bindPoint        = BindPoint::Graphics;
  m_pipelineBound    = false;
  m_indexBufferBound = false;
  m_rendering        = false;

  m_state = State::Ready;
}

void CommandBuffer::Begin()
{
  VD_MARKER_SCOPED();

  if(m_state != State::Ready) {
    throw std::runtime_error(
      "CommandBuffer::Begin requires Ready state (call Reset after "
      "End)");
  }

  if(m_pool.type == QueueType::Graphics) {
    m_device.GetBinder().Bind(*this, BindPoint::Graphics);
  } else if(m_pool.type == QueueType::Compute) {
    m_device.GetBinder().Bind(*this, BindPoint::Compute);
  }

  m_state = State::Recording;
}

void CommandBuffer::End()
{
  VD_MARKER_SCOPED();

  if(m_state != State::Recording) {
    throw std::runtime_error(
      "CommandBuffer::End requires Recording state");
  }
  if(m_rendering)
    throw std::runtime_error(
      "CommandBuffer::End requires EndRendering first");

  if(!m_barriers.barriers.empty()) { FlushBarriers(); }

  VDDxTry(m_handle->Close());
  m_state = State::Closed;
}

void CommandBuffer::BeginRendering(
  Span<Attachment const> colors, const Attachment* depthStencil)
{
  requireRecording("BeginRendering");
  if(m_pool.type != QueueType::Graphics)
    throw std::runtime_error(
      "BeginRendering requires a graphics command buffer");
  if(m_rendering)
    throw std::runtime_error("CommandBuffer::BeginRendering is nested");
  if(colors.size() > 8u)
    throw std::invalid_argument(
      "CommandBuffer::BeginRendering supports at most eight colors");
  const auto validateAttachment = [&](const Attachment& att, bool depth) {
    if(!att.view || !att.view->resource)
      throw std::invalid_argument("Rendering attachment view is null");
    if(&att.view->resource->GetDevice() != &m_device)
      throw std::invalid_argument(
        "Rendering attachment belongs to another device");
    const auto aspect = getFormatAspect(att.view->format);
    if(depth ? aspect == ImageAspect::Color : aspect != ImageAspect::Color)
      throw std::invalid_argument(
        "Rendering attachment format has the wrong aspect");
    if(att.view->type != (depth ? ViewType::DSV : ViewType::RTV))
      throw std::invalid_argument(
        "Rendering attachment has the wrong view type");
    if(att.state == ResourceState::Undefined ||
       getState(*att.view->resource) != att.state)
      throw std::invalid_argument(
        "Rendering attachment state does not match its image state");
    if(att.resolveMode != ResolveMode::None || att.resolveView)
      throw std::invalid_argument(
        "DX12 rendering attachments do not support inline resolves");
  };
  for(const auto& att: colors) validateAttachment(att, false);
  if(depthStencil) validateAttachment(*depthStencil, true);
  if(colors.empty() && !depthStencil)
    throw std::invalid_argument(
      "CommandBuffer::BeginRendering requires at least one attachment");

  const auto RTVs = Transform<D3D12_CPU_DESCRIPTOR_HANDLE>::ToVec(
    colors, [](const Attachment& att) { return att.view->handle.cpu; });

  const auto hasDSV = depthStencil && depthStencil->view;

  D3D12_CPU_DESCRIPTOR_HANDLE DSV{};
  if(hasDSV) { DSV = depthStencil->view->handle.cpu; }

  m_handle->OMSetRenderTargets(
    vd::size32(RTVs), std::data(RTVs), false, hasDSV ? &DSV : nullptr);

  if(hasDSV && depthStencil->loadOp == LoadOp::Clear) {
    D3D12_CLEAR_FLAGS flags{};
    if(
      getFormatAspect(depthStencil->view->format) ==
      ImageAspect::Depth) {
      flags = D3D12_CLEAR_FLAG_DEPTH;
    } else {
      flags = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL;
    }

    const auto clearValue =
      std::get<DepthStencil>(depthStencil->clearValue);

    if(clearValue.stencil > std::numeric_limits<u8>::max())
      throw std::invalid_argument(
        "DX12 stencil clear value must fit an 8-bit value");

    m_handle->ClearDepthStencilView(
      DSV, flags, clearValue.depth, static_cast<u8>(clearValue.stencil),
      0, nullptr);
  }

  for(u32 idx = 0u; idx < std::size(colors); ++idx) {
    const auto& color = colors[idx];
    const auto& RTV   = RTVs[idx];

    if(color.loadOp == LoadOp::Clear) {
      auto clearValue = std::get<Float4>(color.clearValue);
      m_handle->ClearRenderTargetView(
        RTV, clearValue.data(), 0, nullptr);
    }
  }
  m_rendering = true;
}

void CommandBuffer::EndRendering()
{
  requireRecording("EndRendering");
  if(!m_rendering)
    throw std::runtime_error(
      "CommandBuffer::EndRendering has no active rendering scope");
  m_rendering = false;
}

void CommandBuffer::PushConstants(Span<u32 const> data)
{
  requireRecording("PushConstants");
  if(m_pool.type == QueueType::Copy)
    throw std::runtime_error(
      "PushConstants is invalid on a copy command buffer");
  if(data.empty() || data.size_bytes() > PushConstantsSize)
    throw std::invalid_argument(
      "Push constants must contain between 4 and 16 bytes");
  const u32 rootIndex =
    m_device.GetBinder().GetDescriptorInfo().pushConstantsIdx;
  if(m_bindPoint == BindPoint::Graphics)
    m_handle->SetGraphicsRoot32BitConstants(
      rootIndex, vd::size32(data), std::data(data), 0u);
  else
    m_handle->SetComputeRoot32BitConstants(
      rootIndex, vd::size32(data), std::data(data), 0u);
}

void CommandBuffer::SetViewport(const Viewport& viewport)
{
  requireRecording("SetViewport");
  if(m_pool.type != QueueType::Graphics)
    throw std::runtime_error(
      "SetViewport requires a graphics command buffer");
  auto dxViewport = convert(viewport);
  m_handle->RSSetViewports(1, &dxViewport);
}

void CommandBuffer::SetScissor(const Rect& rect)
{
  requireRecording("SetScissor");
  if(m_pool.type != QueueType::Graphics)
    throw std::runtime_error(
      "SetScissor requires a graphics command buffer");
  auto dxRect = convert(rect);
  m_handle->RSSetScissorRects(1, &dxRect);
}

void CommandBuffer::BindIndexBuffer(
  const Buffer& buffer, IndexType indexType, u64 offset)
{
  VD_MARKER_SCOPED();
  requireRecording("BindIndexBuffer");
  if(m_pool.type != QueueType::Graphics)
    throw std::runtime_error(
      "BindIndexBuffer requires a graphics command buffer");

  const u64 alignment = indexType == IndexType::U16 ? 2u : 4u;
  if(&buffer.GetDevice() != &m_device)
    throw std::invalid_argument("Index buffer belongs to another device");
  if(!buffer.GetUsage().IsSet(ResourceUsage::IndexBuffer))
    throw std::invalid_argument("Buffer was not created for index use");
  if(offset >= buffer.GetSize())
    throw std::out_of_range("Index buffer offset is out of bounds");
  if(offset % alignment != 0u)
    throw std::invalid_argument("Index buffer offset is misaligned");
  if(getState(const_cast<Buffer&>(buffer)) != ResourceState::IndexBuffer)
    throw std::invalid_argument("Buffer is not in index-buffer state");
  const u64 remaining = buffer.GetSize() - offset;
  if(remaining > MaxU32)
    throw std::overflow_error("DX12 index-buffer view exceeds 4 GiB");

  D3D12_INDEX_BUFFER_VIEW view{
    .BufferLocation =
      buffer.GetHandle()->GetGPUVirtualAddress() + offset,
    .SizeInBytes = static_cast<u32>(remaining),
    .Format      = convert(indexType)};

  m_handle->IASetIndexBuffer(&view);
  m_indexBufferBound = true;
}

void CommandBuffer::Draw(
  u32 vertexCount, u32 instanceCount, u32 vertexOffset,
  u32 instanceOffset)
{
  VD_MARKER_SCOPED();
  requireRecording("Draw");
  if(
    m_pool.type != QueueType::Graphics || !m_pipelineBound ||
    m_bindPoint != BindPoint::Graphics)
    throw std::runtime_error(
      "Draw requires a bound graphics pipeline on a graphics command buffer");

  m_handle->DrawInstanced(
    vertexCount, instanceCount, vertexOffset, instanceOffset);
}

void CommandBuffer::DrawIndexed(
  u32 indexCount, u32 instanceCount, u32 indexOffset, u32 vertexOffset,
  u32 instanceOffset)
{
  VD_MARKER_SCOPED();
  requireRecording("DrawIndexed");
  if(
    m_pool.type != QueueType::Graphics || !m_pipelineBound ||
    m_bindPoint != BindPoint::Graphics || !m_indexBufferBound)
    throw std::runtime_error(
      "DrawIndexed requires a graphics pipeline and index buffer");

  m_handle->DrawIndexedInstanced(
    indexCount, instanceCount, indexOffset, vertexOffset,
    instanceOffset);
}

void CommandBuffer::Dispatch(
  u32 groupCountX, u32 groupCountY, u32 groupCountZ)
{
  requireRecording("Dispatch");
  if(
    m_pool.type == QueueType::Copy || !m_pipelineBound ||
    m_bindPoint != BindPoint::Compute)
    throw std::runtime_error(
      "Dispatch requires a bound compute pipeline");
  m_handle->Dispatch(groupCountX, groupCountY, groupCountZ);
}

void CommandBuffer::AddBarrier()
{
  requireRecording("AddBarrier");
  D3D12_RESOURCE_BARRIER bar{
    .Type  = D3D12_RESOURCE_BARRIER_TYPE_UAV,
    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE};
  bar.UAV.pResource = nullptr;

  m_barriers.barriers.push_back(bar);
}

void CommandBuffer::AddBarrier(Buffer& res, ResourceState dst)
{
  requireRecording("AddBarrier");
  const ResourceState srcState = getState(res);

  if(
    dst == ResourceState::UnorderedAccess &&
    srcState == ResourceState::UnorderedAccess) {
    D3D12_RESOURCE_BARRIER bar{
      .Type  = D3D12_RESOURCE_BARRIER_TYPE_UAV,
      .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE};
    bar.UAV.pResource = res.GetHandle();

    auto [pending, inserted] = m_barriers.pendingBuffers.try_emplace(
      &res, Barriers::PendingState{srcState, dst});
    if(!inserted) pending->second.final = dst;
    m_barriers.barriers.push_back(bar);
    return;
  }

  if(srcState == dst) return;

  D3D12_RESOURCE_BARRIER bar{
    .Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE};
  bar.Transition.pResource   = res.GetHandle();
  bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  bar.Transition.StateBefore = convert(srcState);
  bar.Transition.StateAfter  = convert(dst);

  auto [pending, inserted] = m_barriers.pendingBuffers.try_emplace(
    &res, Barriers::PendingState{srcState, dst});
  if(!inserted) pending->second.final = dst;
  m_barriers.barriers.push_back(bar);
}

void CommandBuffer::AddBarrier(Image& res, ResourceState dst)
{
  requireRecording("AddBarrier");
  const ResourceState srcState = getState(res);

  if(
    dst == ResourceState::UnorderedAccess &&
    srcState == ResourceState::UnorderedAccess) {
    D3D12_RESOURCE_BARRIER bar{
      .Type  = D3D12_RESOURCE_BARRIER_TYPE_UAV,
      .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE};
    bar.UAV.pResource = res.GetHandle();

    auto [pending, inserted] = m_barriers.pendingImages.try_emplace(
      &res, Barriers::PendingState{srcState, dst});
    if(!inserted) pending->second.final = dst;
    m_barriers.barriers.push_back(bar);
    return;
  }

  if(srcState == dst) return;

  D3D12_RESOURCE_BARRIER bar{
    .Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE};
  bar.Transition.pResource   = res.GetHandle();
  bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  bar.Transition.StateBefore = convert(srcState);
  bar.Transition.StateAfter  = convert(dst);

  auto [pending, inserted] = m_barriers.pendingImages.try_emplace(
    &res, Barriers::PendingState{srcState, dst});
  if(!inserted) pending->second.final = dst;
  m_barriers.barriers.push_back(bar);
}

void CommandBuffer::FlushBarriers()
{
  requireRecording("FlushBarriers");
  const auto numBarriers = vd::size32(m_barriers.barriers);
  if(numBarriers == 0u) return;

  m_handle->ResourceBarrier(
    numBarriers, std::data(m_barriers.barriers));

  m_barriers.barriers.clear();
}

ResourceState CommandBuffer::getState(Buffer& res) const
{
  const auto it = m_barriers.pendingBuffers.find(&res);
  return it == m_barriers.pendingBuffers.end() ? res.GetState()
                                                : it->second.final;
}

ResourceState CommandBuffer::getState(Image& res) const
{
  const auto it = m_barriers.pendingImages.find(&res);
  return it == m_barriers.pendingImages.end() ? res.GetState()
                                               : it->second.final;
}

void CommandBuffer::commitResourceStates()
{
  for(const auto& [buffer, state]: m_barriers.pendingBuffers)
    buffer->SetState(state.final);
  for(const auto& [image, state]: m_barriers.pendingImages)
    image->SetState(state.final);
}

void CommandBuffer::requireRecording(const char* operation) const
{
  if(m_state != State::Recording)
    throw makeError<std::runtime_error>(
      "CommandBuffer::%s requires Recording state", operation);
}

void CommandBuffer::Copy(
  const Buffer& src, Buffer& dst, u64 srcOffset, u64 dstOffset,
  u64 size)
{
  requireRecording("Copy");
  if(size == 0u)
    throw std::invalid_argument("Copy size must be non-zero");
  if(srcOffset > src.GetSize() || size > src.GetSize() - srcOffset)
    throw std::out_of_range("Copy source buffer range is out of bounds");
  if(dstOffset > dst.GetSize() || size > dst.GetSize() - dstOffset)
    throw std::out_of_range(
      "Copy destination buffer range is out of bounds");
  if(
    &src.GetDevice() != &m_device || &dst.GetDevice() != &m_device)
    throw std::invalid_argument("Copy buffer belongs to another device");
  if(
    getState(const_cast<Buffer&>(src)) != ResourceState::CopySrc ||
    getState(dst) != ResourceState::CopyDst)
    throw std::invalid_argument("Copy buffers are not in copy states");
  if(&src == &dst) {
    const u64 srcEnd = srcOffset + size;
    const u64 dstEnd = dstOffset + size;
    if(srcOffset < dstEnd && dstOffset < srcEnd)
      throw std::invalid_argument("Copy buffer ranges overlap");
  }

  m_handle->CopyBufferRegion(
    dst.GetHandle(), dstOffset, src.GetHandle(), srcOffset, size);
}

void CommandBuffer::Copy(
  const Buffer& src, Image& dst, u64 offset, u32 mip, u32 layer)
{
  requireRecording("Copy");
  const auto& desc  = dst.GetDesc();
  if(mip >= desc.mips)
    throw std::out_of_range("Copy image mip is out of range");
  const u32 layers =
    desc.dimension == Dimension::e3D ? 1u : desc.extent[2];
  if(layer >= layers)
    throw std::out_of_range("Copy image layer is out of range");
  const auto  width = std::max(1u, desc.extent[0] >> mip);
  const auto height = std::max(1u, desc.extent[1] >> mip);
  const auto depth =
    desc.dimension == Dimension::e3D ? std::max(1u, desc.extent[2] >> mip)
                                     : 1u;
  const auto format = desc.format;

  // Calculate row pitch aligned to D3D12 requirements (256 bytes)
  const u64 formatSize = getFormatSize(format);
  if(formatSize == 0u || width > MaxU64 / formatSize)
    throw std::overflow_error("Copy image byte size overflow");
  const u64 unalignedRowPitch = static_cast<u64>(width) * formatSize;
  const u64 rowPitch64 = alignUp(
    unalignedRowPitch,
    static_cast<u64>(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));
  if(rowPitch64 > MaxU32)
    throw std::overflow_error("Copy image row pitch overflow");
  const auto rowPitch = static_cast<u32>(rowPitch64);
  if(offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT != 0u)
    throw std::runtime_error("CopyTexture: source offset is not aligned");
  if(height > MaxU64 / rowPitch64)
    throw std::overflow_error("Copy image byte size overflow");
  const u64 sliceSize = rowPitch64 * height;
  if(depth > MaxU64 / sliceSize)
    throw std::overflow_error("Copy image byte size overflow");
  const u64 requiredSize = sliceSize * depth;
  if(offset > src.GetSize() || requiredSize > src.GetSize() - offset)
    throw std::runtime_error("CopyTexture: source buffer is too small");
  if(
    &src.GetDevice() != &m_device || &dst.GetDevice() != &m_device)
    throw std::invalid_argument("Copy resource belongs to another device");
  if(
    getState(const_cast<Buffer&>(src)) != ResourceState::CopySrc ||
    getState(dst) != ResourceState::CopyDst)
    throw std::invalid_argument("Copy resources are not in copy states");

  D3D12_TEXTURE_COPY_LOCATION srcLoc{
    .pResource       = src.GetHandle(),
    .Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
    .PlacedFootprint = {
      .Offset    = offset,
      .Footprint = {
        .Format   = convert(format),
        .Width    = width,
        .Height   = height,
        .Depth    = depth,
        .RowPitch = rowPitch,
      }}};

  D3D12_TEXTURE_COPY_LOCATION dstLoc{
    .pResource        = dst.GetHandle(),
    .Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
    .SubresourceIndex = dst.GetSubresourceIndex(mip, layer)};

  m_handle->CopyTextureRegion(&dstLoc, 0u, 0u, 0u, &srcLoc, nullptr);
}

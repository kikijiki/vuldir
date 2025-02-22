#include "vuldir/api/Binder.hpp"
#include "vuldir/api/Buffer.hpp"
#include "vuldir/api/CommandBuffer.hpp"
#include "vuldir/api/Device.hpp"
#include "vuldir/api/Image.hpp"
#include "vuldir/api/Pipeline.hpp"
#include "vuldir/api/dx/DxUti.hpp"

using namespace vd;

CommandPool::CommandPool(Device& device_, QueueType type_):
  device{device_}, type{type_}, handle{}, dxType{}
{
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

  device.api().CreateCommandAllocator(dxType, IID_PPV_ARGS(&handle));
}

CommandPool::~CommandPool() {}

void CommandPool::Reset() { handle->Reset(); }

CommandBuffer::CommandBuffer(Device& device, CommandPool& pool):
  m_device{device},
  m_pool{pool},
  m_state{State::Closed},
  m_barriers{},
  m_handle{}
{
  VD_MARKER_SCOPED();

  m_device.api().CreateCommandList1(
    0u, pool.dxType, D3D12_COMMAND_LIST_FLAG_NONE,
    IID_PPV_ARGS(&m_handle));

  Reset(true);
}

CommandBuffer::~CommandBuffer() {}

void CommandBuffer::Reset(bool resetPool)
{
  VD_MARKER_SCOPED();

  if(m_state != State::Closed) return;

  m_handle->Reset(m_pool.handle.Get(), nullptr);
  if(resetPool) m_pool.Reset(); // Move after command list reset
  m_state = State::Ready;
}

void CommandBuffer::Begin()
{
  VD_MARKER_SCOPED();

  const auto& rootSignature = m_device.GetBinder().GetRootSignature();
  const auto& descriptors   = m_device.GetBinder().GetDescriptorInfo();

  if(
    m_pool.type == QueueType::Graphics ||
    m_pool.type == QueueType::Compute) {
    m_handle->SetDescriptorHeaps(
      size32(descriptors.heaps), std::data(descriptors.heaps));
  }

  if(m_pool.type == QueueType::Graphics) {
    m_handle->SetGraphicsRootSignature(rootSignature);
    for(const auto& table: descriptors.tables) {
      m_handle->SetGraphicsRootDescriptorTable(
        table.index, table.handle);
    }
  } else if(m_pool.type == QueueType::Compute) {
    m_handle->SetComputeRootSignature(rootSignature);
    for(const auto& table: descriptors.tables) {
      m_handle->SetComputeRootDescriptorTable(
        table.index, table.handle);
    }
  }

  m_state = State::Recording;
}

void CommandBuffer::End()
{
  VD_MARKER_SCOPED();

  m_handle->Close();
  m_state = State::Closed;
}

void CommandBuffer::BeginRendering(
  Span<Attachment const> colors, const Attachment* depthStencil)
{
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

    VDAssertMsg(
      clearValue.stencil <= std::numeric_limits<u8>::max(),
      "Stencil value is out of range! For DX12 it must fit a u8");

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
}

void CommandBuffer::EndRendering() {}

void CommandBuffer::PushConstants(Span<u32 const> data)
{
  m_handle->SetGraphicsRoot32BitConstants(
    m_device.GetBinder().GetDescriptorInfo().pushConstantsIdx,
    vd::size32(data), std::data(data), 0u);
}

void CommandBuffer::SetViewport(const Viewport& viewport)
{
  auto dxViewport = convert(viewport);
  m_handle->RSSetViewports(1, &dxViewport);
}

void CommandBuffer::SetScissor(const Rect& rect)
{
  auto dxRect = convert(rect);
  m_handle->RSSetScissorRects(1, &dxRect);
}

void CommandBuffer::Draw(
  u32 vertexCount, u32 instanceCount, u32 vertexOffset,
  u32 instanceOffset)
{
  VD_MARKER_SCOPED();

  m_handle->DrawInstanced(
    vertexCount, instanceCount, vertexOffset, instanceOffset);
}

void CommandBuffer::DrawIndexed(
  u32 indexCount, u32 instanceCount, u32 indexOffset, u32 vertexOffset,
  u32 instanceOffset)
{
  VD_MARKER_SCOPED();

  m_handle->DrawIndexedInstanced(
    indexCount, instanceCount, indexOffset, vertexOffset,
    instanceOffset);
}

void CommandBuffer::AddBarrier()
{
  D3D12_RESOURCE_BARRIER bar{
    .Type  = D3D12_RESOURCE_BARRIER_TYPE_UAV,
    .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE};
  bar.UAV.pResource = nullptr;

  m_barriers.resources.emplace_back();
  m_barriers.states.push_back(ResourceState::Undefined);
  m_barriers.barriers.push_back(bar);
}

void CommandBuffer::AddBarrier(Buffer& res, ResourceState dst)
{
  if(res.GetState() == dst) return;

  D3D12_RESOURCE_BARRIER bar{};

  if(dst == ResourceState::UnorderedAccess) {
    bar.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    bar.Flags         = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    bar.UAV.pResource = res.GetHandle();
  } else {
    bar.Type                 = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bar.Flags                = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    bar.Transition.pResource = res.GetHandle();
    bar.Transition.Subresource =
      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    bar.Transition.StateBefore = convert(res.GetState());
    bar.Transition.StateAfter  = convert(dst);
  }

  m_barriers.resources.push_back(&res);
  m_barriers.states.push_back(dst);
  m_barriers.barriers.push_back(bar);
}

void CommandBuffer::AddBarrier(Image& res, ResourceState dst)
{
  if(res.GetState() == dst) return;

  D3D12_RESOURCE_BARRIER bar{};

  if(dst == ResourceState::UnorderedAccess) {
    bar.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    bar.Flags         = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    bar.UAV.pResource = res.GetHandle();
  } else {
    bar.Type                 = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bar.Flags                = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    bar.Transition.pResource = res.GetHandle();
    bar.Transition.Subresource =
      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    bar.Transition.StateBefore = convert(res.GetState());
    bar.Transition.StateAfter  = convert(dst);
  }

  m_barriers.resources.push_back(&res);
  m_barriers.states.push_back(dst);
  m_barriers.barriers.push_back(bar);
}

void CommandBuffer::FlushBarriers()
{
  const auto numBarriers = vd::size32(m_barriers.barriers);
  if(numBarriers == 0u) return;

  m_handle->ResourceBarrier(
    numBarriers, std::data(m_barriers.barriers));

  // Update resources state.
  for(u32 idx = 0u; idx < numBarriers; ++idx) {
    auto& bar = m_barriers.barriers[idx];

    if(bar.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) continue;

    auto& res   = m_barriers.resources[idx];
    auto  state = m_barriers.states[idx];

    if(std::holds_alternative<Buffer*>(res)) {
      std::get<Buffer*>(res)->SetState(state);
    } else if(std::holds_alternative<Image*>(res)) {
      std::get<Image*>(res)->SetState(state);
    }
  }

  m_barriers.resources.clear();
  m_barriers.states.clear();
  m_barriers.barriers.clear();
}

void CommandBuffer::Copy(const Buffer& src, Buffer& dst)
{
  Copy(src, dst, 0u, 0u, src.GetSize());
}

void CommandBuffer::Copy(
  const Buffer& src, Buffer& dst, u64 srcOffset, u64 dstOffset,
  u64 size)
{
  const auto srcSize = src.GetSize();
  const auto dstSize = dst.GetSize();

  if(srcSize == 0u) {
    VDLogW("CopyBuffer: source buffer has zero size");
    return;
  }

  if(dstSize == 0u) {
    VDLogW("CopyBuffer: destination buffer has zero size");
    return;
  }

  if(srcOffset + size > src.GetSize()) {
    VDLogW("Copy: source buffer is too small");
    return;
  }

  if(dstOffset + size > dst.GetSize()) {
    VDLogW("Copy: destination buffer is too small");
    return;
  }

  m_handle->CopyBufferRegion(
    dst.GetHandle(), srcOffset, src.GetHandle(), dstOffset, size);
}

void CommandBuffer::Copy(
  const Buffer& src, Image& dst, u64 offset, u32 mip, u32 layer)
{
  D3D12_TEXTURE_COPY_LOCATION srcLoc{
    .pResource       = src.GetHandle(),
    .Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
    .PlacedFootprint = {
      .Offset    = offset,
      .Footprint = {
        .Format   = convert(dst.GetFormat()),
        .Width    = dst.GetDesc().extent[0],
        .Height   = dst.GetDesc().extent[1],
        .Depth    = dst.GetDesc().extent[2],
        .RowPitch = 0u,
      }}};

  D3D12_TEXTURE_COPY_LOCATION dstLoc{
    .pResource        = dst.GetHandle(),
    .Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
    .SubresourceIndex = dst.GetSubresourceIndex(mip, layer)};

  m_handle->CopyTextureRegion(&dstLoc, 0u, 0u, 0u, &srcLoc, nullptr);
}

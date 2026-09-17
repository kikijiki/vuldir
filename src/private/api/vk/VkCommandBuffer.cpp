#include "vuldir/api/Binder.hpp"
#include "vuldir/api/Buffer.hpp"
#include "vuldir/api/CommandBuffer.hpp"
#include "vuldir/api/Device.hpp"
#include "vuldir/api/Image.hpp"
#include "vuldir/api/RenderContext.hpp"
#include "vuldir/api/vk/VkDispatcher.hpp"
#include "vuldir/api/vk/VkUti.hpp"

using namespace vd;

namespace {

// Mip-adjusted attachment size. BeginRendering's render area must cover the
// attachments and stay within them, so it is derived from these.
UInt2 attachmentExtent(const Image::View& view)
{
  const auto& extent = view.resource->GetDesc().extent;
  const u32   mip    = view.range.mipOffset;
  if(mip >= 32u) return {1u, 1u};
  return {
    std::max(1u, extent[0] >> mip), std::max(1u, extent[1] >> mip)};
}

} // namespace

CommandPool::CommandPool(Device& device_, QueueType type_):
  device{device_}, type{type_}, handle{}
{
  if(!isValid(type))
    throw std::invalid_argument("Command pool queue type is invalid");
  VkCommandPoolCreateInfo poolCI{};
  poolCI.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolCI.queueFamilyIndex = device.GetQueueFamily(type);
  poolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  VDVkTry(device.api().CreateCommandPool(&poolCI, &handle));
}

CommandPool::~CommandPool()
{
  if(handle) {
    device.api().DestroyCommandPool(handle);
    handle = nullptr;
  }
}

void CommandPool::Reset()
{
  VDVkTry(device.api().ResetCommandPool(handle, 0u));
}

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
  if(&device != &pool.device)
    throw std::invalid_argument(
      "Command pool belongs to another device");

  VkCommandBufferAllocateInfo cmdCI = {};

  cmdCI.sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdCI.commandPool = m_pool.handle;
  cmdCI.commandBufferCount = 1u;
  cmdCI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

  VDVkTry(m_device.api().AllocateCommandBuffers(&cmdCI, &m_handle));

  reset(false);
}

CommandBuffer::~CommandBuffer()
{
  if(m_handle) {
    m_device.api().FreeCommandBuffers(m_pool.handle, 1u, &m_handle);
    m_handle = nullptr;
  }
}

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
  VDVkTry(m_device.api().ResetCommandBuffer(m_handle, 0u));

  m_barriers.memoryBarriers.clear();
  m_barriers.bufferBarriers.clear();
  m_barriers.imageBarriers.clear();
  m_barriers.pendingBuffers.clear();
  m_barriers.pendingImages.clear();

  m_bindPoint       = BindPoint::Graphics;
  m_pipelineBound   = false;
  m_indexBufferBound = false;
  m_rendering       = false;

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

  VkCommandBufferBeginInfo info{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

  VDVkTry(m_device.api().BeginCommandBuffer(m_handle, &info));
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

  if(
    !m_barriers.memoryBarriers.empty() ||
    !m_barriers.bufferBarriers.empty() ||
    !m_barriers.imageBarriers.empty()) {
    FlushBarriers();
  }

  VDVkTry(m_device.api().EndCommandBuffer(m_handle));
  m_state = State::Closed;
}

void CommandBuffer::BeginRendering(
  Span<Attachment const> color, const Attachment* depthStencil)
{
  requireRecording("BeginRendering");
  if(m_pool.type != QueueType::Graphics)
    throw std::runtime_error(
      "BeginRendering requires a graphics command buffer");
  if(m_rendering)
    throw std::runtime_error("CommandBuffer::BeginRendering is nested");
  if(color.size() > 8u)
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

    if(att.resolveMode == ResolveMode::None) {
      if(att.resolveView)
        throw std::invalid_argument(
          "Resolve view requires a non-None resolve mode");
      return;
    }
    if(
      !att.resolveView || !att.resolveView->resource ||
      &att.resolveView->resource->GetDevice() != &m_device)
      throw std::invalid_argument("Resolve attachment view is invalid");
    if(att.view->samples <= 1u || att.resolveView->samples != 1u)
      throw std::invalid_argument("Resolve attachment sample counts are invalid");
    if(
      att.resolveView->type != (depth ? ViewType::DSV : ViewType::RTV) ||
      att.resolveView->format != att.view->format)
      throw std::invalid_argument("Resolve attachment view is incompatible");
    if(
      att.resolveState == ResourceState::Undefined ||
      getState(*att.resolveView->resource) != att.resolveState)
      throw std::invalid_argument(
        "Resolve attachment state does not match its image state");
  };
  for(const auto& att: color) validateAttachment(att, false);
  if(depthStencil) validateAttachment(*depthStencil, true);

  // Render area is derived from the attachments, not the last SetScissor:
  // without a prior SetScissor it would be zero-sized and draw nothing.
  UInt2      area{MaxU32, MaxU32};
  const auto includeAttachment = [&](const Attachment& att) {
    const auto extent = attachmentExtent(*att.view);
    area[0]           = std::min(area[0], extent[0]);
    area[1]           = std::min(area[1], extent[1]);
  };
  for(const auto& att: color) includeAttachment(att);
  if(depthStencil) includeAttachment(*depthStencil);
  if(area[0] == MaxU32)
    throw std::invalid_argument(
      "CommandBuffer::BeginRendering requires at least one attachment");

  const auto toVk =
    [](const Attachment& att) -> VkRenderingAttachmentInfo {
    return {
      .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .pNext       = nullptr,
      .imageView   = att.view ? att.view->handle : nullptr,
      .imageLayout = getVkImageLayout(att.state),
      .resolveMode = convert(att.resolveMode),
      .resolveImageView =
        att.resolveView ? att.resolveView->handle : nullptr,
      .resolveImageLayout = getVkImageLayout(att.resolveState),
      .loadOp             = convert(att.loadOp),
      .storeOp            = convert(att.storeOp),
      .clearValue         = convert(att.clearValue),
    };
  };

  const auto vkColorAtts =
    Transform<VkRenderingAttachmentInfo>::ToVec(color, toVk);

  VkRenderingInfo info{
    .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
    .pNext                = nullptr,
    .flags                = 0u,
    .renderArea           = {{0, 0}, {area[0], area[1]}},
    .layerCount           = 1u,
    .viewMask             = 0u,
    .colorAttachmentCount = vd::size32(vkColorAtts),
    .pColorAttachments    = std::data(vkColorAtts),
  };

  VkRenderingAttachmentInfo vkDepthAtt;
  VkRenderingAttachmentInfo vkStencilAtt;
  if(depthStencil) {
    if(
      vd::getVkDepth(depthStencil->view->format) !=
      VK_FORMAT_UNDEFINED) {
      vkDepthAtt            = toVk(*depthStencil);
      info.pDepthAttachment = &vkDepthAtt;
    }
    if(
      vd::getVkStencil(depthStencil->view->format) !=
      VK_FORMAT_UNDEFINED) {
      vkStencilAtt            = toVk(*depthStencil);
      info.pStencilAttachment = &vkStencilAtt;
    }
  }

  m_device.api().CmdBeginRendering(m_handle, &info);
  m_rendering = true;
}

void CommandBuffer::EndRendering()
{
  requireRecording("EndRendering");
  if(!m_rendering)
    throw std::runtime_error(
      "CommandBuffer::EndRendering has no active rendering scope");
  m_device.api().CmdEndRendering(m_handle);
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
  m_device.api().CmdPushConstants(
    m_handle, m_device.GetBinder().GetPipelineLayout(),
    VK_SHADER_STAGE_ALL, 0u, static_cast<u32>(data.size_bytes()),
    std::data(data));
}

void CommandBuffer::SetViewport(const Viewport& viewport)
{
  requireRecording("SetViewport");
  if(m_pool.type != QueueType::Graphics)
    throw std::runtime_error(
      "SetViewport requires a graphics command buffer");
  const auto vkViewport = convert(viewport);
  m_device.api().CmdSetViewport(m_handle, 0u, 1u, &vkViewport);
}

void CommandBuffer::SetScissor(const Rect& rect)
{
  requireRecording("SetScissor");
  if(m_pool.type != QueueType::Graphics)
    throw std::runtime_error(
      "SetScissor requires a graphics command buffer");
  const auto vkRect = convert(rect);
  m_device.api().CmdSetScissor(m_handle, 0u, 1u, &vkRect);
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

  const auto vkIndexType = convert(indexType);
  m_device.api().CmdBindIndexBuffer(
    m_handle, buffer.GetHandle(), offset, vkIndexType);
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

  m_device.api().CmdDraw(
    m_handle, vertexCount, instanceCount, vertexOffset, instanceOffset);
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

  m_device.api().CmdDrawIndexed(
    m_handle, indexCount, instanceCount, indexOffset, vertexOffset,
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
  m_device.api().CmdDispatch(
    m_handle, groupCountX, groupCountY, groupCountZ);
}

void CommandBuffer::AddBarrier()
{
  requireRecording("AddBarrier");
  VkMemoryBarrier2 bar{
    .sType        = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
    .pNext        = nullptr,
    .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    .srcAccessMask =
      VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
    .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    .dstAccessMask =
      VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT};

  m_barriers.memoryBarriers.push_back(bar);
}

void CommandBuffer::AddBarrier(Buffer& res, ResourceState dstState)
{
  requireRecording("AddBarrier");
  const ResourceState srcState = getState(res);

  if(srcState == dstState) return;

  constexpr uint32_t srcQueueFamily = VK_QUEUE_FAMILY_IGNORED;
  constexpr uint32_t dstQueueFamily = VK_QUEUE_FAMILY_IGNORED;

  VkBufferMemoryBarrier2 bar{
    .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
    .pNext               = nullptr,
    .srcStageMask        = toPipelineStage2(srcState),
    .srcAccessMask       = toAccessFlags2(srcState),
    .dstStageMask        = toPipelineStage2(dstState),
    .dstAccessMask       = toAccessFlags2(dstState),
    .srcQueueFamilyIndex = srcQueueFamily,
    .dstQueueFamilyIndex = dstQueueFamily,
    .buffer              = res.GetHandle(),
    .offset              = 0u,
    .size                = VK_WHOLE_SIZE};

  auto [pending, inserted] = m_barriers.pendingBuffers.try_emplace(
    &res, Barriers::PendingState{srcState, dstState});
  if(!inserted) pending->second.final = dstState;
  m_barriers.bufferBarriers.push_back(bar);
}

void CommandBuffer::AddBarrier(Image& res, ResourceState dstState)
{
  requireRecording("AddBarrier");
  const ResourceState srcState = getState(res);

  if(srcState == dstState) return;

  constexpr uint32_t srcQueueFamily = VK_QUEUE_FAMILY_IGNORED;
  constexpr uint32_t dstQueueFamily = VK_QUEUE_FAMILY_IGNORED;

  VkImageMemoryBarrier2 bar{
    .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
    .pNext               = nullptr,
    .srcStageMask        = toPipelineStage2(srcState),
    .srcAccessMask       = toAccessFlags2(srcState),
    .dstStageMask        = toPipelineStage2(dstState),
    .dstAccessMask       = toAccessFlags2(dstState),
    .oldLayout           = getVkImageLayout(srcState),
    .newLayout           = getVkImageLayout(dstState),
    .srcQueueFamilyIndex = srcQueueFamily,
    .dstQueueFamilyIndex = dstQueueFamily,
    .image               = res.GetHandle(),
    .subresourceRange    = {
         .aspectMask     = getVkAspectFlags(res.GetFormat()),
         .baseMipLevel   = 0,
         .levelCount     = VK_REMAINING_MIP_LEVELS,
         .baseArrayLayer = 0,
         .layerCount     = VK_REMAINING_ARRAY_LAYERS}};

  auto [pending, inserted] = m_barriers.pendingImages.try_emplace(
    &res, Barriers::PendingState{srcState, dstState});
  if(!inserted) pending->second.final = dstState;
  m_barriers.imageBarriers.push_back(bar);
}

void CommandBuffer::FlushBarriers()
{
  requireRecording("FlushBarriers");
  if(
    m_barriers.memoryBarriers.empty() &&
    m_barriers.bufferBarriers.empty() &&
    m_barriers.imageBarriers.empty()) {
    return;
  }

  VkDependencyInfo dependencyInfo{
    .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
    .pNext                    = nullptr,
    .dependencyFlags          = 0,
    .memoryBarrierCount       = vd::size32(m_barriers.memoryBarriers),
    .pMemoryBarriers          = std::data(m_barriers.memoryBarriers),
    .bufferMemoryBarrierCount = vd::size32(m_barriers.bufferBarriers),
    .pBufferMemoryBarriers    = std::data(m_barriers.bufferBarriers),
    .imageMemoryBarrierCount  = vd::size32(m_barriers.imageBarriers),
    .pImageMemoryBarriers     = std::data(m_barriers.imageBarriers)};

  m_device.api().CmdPipelineBarrier2(m_handle, &dependencyInfo);

  m_barriers.memoryBarriers.clear();
  m_barriers.bufferBarriers.clear();
  m_barriers.imageBarriers.clear();
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

  VkBufferCopy2 region{
    .sType     = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
    .pNext     = nullptr,
    .srcOffset = srcOffset,
    .dstOffset = dstOffset,
    .size      = size};

  VkCopyBufferInfo2 desc{
    .sType       = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
    .pNext       = nullptr,
    .srcBuffer   = src.GetHandle(),
    .dstBuffer   = dst.GetHandle(),
    .regionCount = 1u,
    .pRegions    = &region};

  m_device.api().CmdCopyBuffer2(m_handle, &desc);
}

void CommandBuffer::Copy(
  const Buffer& src, Image& dst, u64 offset, u32 mip, u32 layer)
{
  requireRecording("Copy");
  const auto& desc = dst.GetDesc();
  if(mip >= desc.mips)
    throw std::out_of_range("Copy image mip is out of range");
  const u32 layers =
    desc.dimension == Dimension::e3D ? 1u : desc.extent[2];
  if(layer >= layers)
    throw std::out_of_range("Copy image layer is out of range");

  auto extent = desc.extent;
  for(u32 idx = 0u; idx < mip; ++idx) {
    if(extent[0] > 1u) extent[0] /= 2u;
    if(extent[1] > 1u) extent[1] /= 2u;
    if(desc.dimension == Dimension::e3D && extent[2] > 1u)
      extent[2] /= 2u;
  }

  const u32 depth =
    desc.dimension == Dimension::e3D ? std::max(1u, extent[2]) : 1u;
  const u64 formatSize = getFormatSize(desc.format);
  if(formatSize == 0u || extent[0] > MaxU64 / formatSize)
    throw std::overflow_error("Copy image byte size overflow");
  const u64 rowSize = static_cast<u64>(extent[0]) * formatSize;
  if(extent[1] > MaxU64 / rowSize)
    throw std::overflow_error("Copy image byte size overflow");
  const u64 sliceSize = rowSize * extent[1];
  if(depth > MaxU64 / sliceSize)
    throw std::overflow_error("Copy image byte size overflow");
  const u64 requiredSize = sliceSize * depth;
  if(offset > src.GetSize() || requiredSize > src.GetSize() - offset)
    throw std::out_of_range("Copy image source buffer is too small");
  if(
    &src.GetDevice() != &m_device || &dst.GetDevice() != &m_device)
    throw std::invalid_argument("Copy resource belongs to another device");
  if(
    getState(const_cast<Buffer&>(src)) != ResourceState::CopySrc ||
    getState(dst) != ResourceState::CopyDst)
    throw std::invalid_argument("Copy resources are not in copy states");
  if(offset % formatSize != 0u)
    throw std::invalid_argument(
      "Copy image source offset is not texel-aligned");

  VkImageSubresourceLayers subresource{
    .aspectMask     = getVkFormatAspect(desc.format),
    .mipLevel       = mip,
    .baseArrayLayer = layer,
    .layerCount     = 1u};

  VkBufferImageCopy2 copyDesc{
    .sType             = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2,
    .pNext             = nullptr,
    .bufferOffset      = offset,
    .bufferRowLength   = 0u,
    .bufferImageHeight = 0u,
    .imageSubresource  = subresource,
    .imageOffset       = {0u, 0u, 0u},
    .imageExtent       = {extent[0], extent[1], depth}};

  VkCopyBufferToImageInfo2 copyInfo{
    .sType          = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2,
    .pNext          = nullptr,
    .srcBuffer      = src.GetHandle(),
    .dstImage       = dst.GetHandle(),
    .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    .regionCount    = 1u,
    .pRegions       = &copyDesc};

  m_device.api().CmdCopyBufferToImage2(m_handle, &copyInfo);
}

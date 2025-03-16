#include "vuldir/api/Binder.hpp"
#include "vuldir/api/Buffer.hpp"
#include "vuldir/api/CommandBuffer.hpp"
#include "vuldir/api/Device.hpp"
#include "vuldir/api/Image.hpp"
#include "vuldir/api/RenderContext.hpp"
#include "vuldir/api/vk/VkDispatcher.hpp"
#include "vuldir/api/vk/VkUti.hpp"

using namespace vd;

CommandPool::CommandPool(Device& device_, QueueType type_):
  device{device_}, type{type_}, handle{}
{
  VkCommandPoolCreateInfo poolCI{};
  poolCI.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolCI.queueFamilyIndex = device.GetQueueFamily(type);
  poolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  device.api().CreateCommandPool(&poolCI, &handle);
}

CommandPool::~CommandPool()
{
  if(handle) {
    device.api().DestroyCommandPool(handle);
    handle = nullptr;
  }
}

void CommandPool::Reset() { device.api().ResetCommandPool(handle, 0u); }

CommandBuffer::CommandBuffer(Device& device, CommandPool& pool):
  m_device{device},
  m_pool{pool},
  m_state{State::Closed},
  m_barriers{},
  m_handle{},
  m_renderArea{}
{
  VkCommandBufferAllocateInfo cmdCI = {};

  cmdCI.sType       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdCI.commandPool = m_pool.handle;
  cmdCI.commandBufferCount = 1u;
  cmdCI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

  VDVkTry(m_device.api().AllocateCommandBuffers(&cmdCI, &m_handle));

  Reset(true);
}

CommandBuffer::~CommandBuffer()
{
  if(m_handle) {
    m_device.api().FreeCommandBuffers(m_pool.handle, 1u, &m_handle);
    m_handle = nullptr;
  }
}

void CommandBuffer::Reset(bool resetPool)
{
  VD_MARKER_SCOPED();

  if(m_state != State::Closed) return;

  if(resetPool) m_pool.Reset();
  m_device.api().ResetCommandBuffer(m_handle, 0u);
  m_state = State::Ready;
}

void CommandBuffer::Begin()
{
  VD_MARKER_SCOPED();

  VkCommandBufferBeginInfo info{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

  m_device.api().BeginCommandBuffer(m_handle, &info);
  m_state = State::Recording;
}

void CommandBuffer::End()
{
  VD_MARKER_SCOPED();

  m_device.api().EndCommandBuffer(m_handle);
  m_state = State::Closed;
}

void CommandBuffer::BeginRendering(
  Span<Attachment const> color, const Attachment* depthStencil)
{
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
    .renderArea           = m_renderArea,
    .layerCount           = 1u,
    .viewMask             = 0u,
    .colorAttachmentCount = vd::size32(vkColorAtts),
    .pColorAttachments    = std::data(vkColorAtts),
  };

  VkRenderingAttachmentInfo vkDepthAtt;
  VkRenderingAttachmentInfo vkStencilAtt;
  if(depthStencil && depthStencil->view) {
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
}

void CommandBuffer::EndRendering()
{
  m_device.api().CmdEndRendering(m_handle);
}

void CommandBuffer::PushConstants(Span<u32 const> data)
{
  m_device.api().CmdPushConstants(
    m_handle, m_device.GetBinder().GetPipelineLayout(),
    VK_SHADER_STAGE_ALL, 0u, static_cast<u32>(data.size_bytes()),
    std::data(data));
}

void CommandBuffer::SetViewport(const Viewport& viewport)
{
  const auto vkViewport = convert(viewport);
  m_device.api().CmdSetViewport(m_handle, 0u, 1u, &vkViewport);
}

void CommandBuffer::SetScissor(const Rect& rect)
{
  const auto vkRect = convert(rect);
  m_device.api().CmdSetScissor(m_handle, 0u, 1u, &vkRect);
  m_renderArea = vkRect;
}

void CommandBuffer::BindIndexBuffer(
  const Buffer& buffer, IndexType indexType, u64 offset)
{
  VD_MARKER_SCOPED();

  const auto vkIndexType = convert(indexType);
  m_device.api().CmdBindIndexBuffer(
    m_handle, buffer.GetHandle(), offset, vkIndexType);
}

void CommandBuffer::Draw(
  u32 vertexCount, u32 instanceCount, u32 vertexOffset,
  u32 instanceOffset)
{
  VD_MARKER_SCOPED();

  m_device.api().CmdDraw(
    m_handle, vertexCount, instanceCount, vertexOffset, instanceOffset);
}

void CommandBuffer::DrawIndexed(
  u32 indexCount, u32 instanceCount, u32 indexOffset, u32 vertexOffset,
  u32 instanceOffset)
{
  VD_MARKER_SCOPED();

  m_device.api().CmdDrawIndexed(
    m_handle, indexCount, instanceCount, indexOffset, vertexOffset,
    instanceOffset);
}

void CommandBuffer::AddBarrier()
{
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
  AddBarrier(
    res, dstState, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED);
}

void CommandBuffer::AddBarrier(
  Buffer& res, ResourceState dstState, uint32_t srcQueueFamily,
  uint32_t dstQueueFamily)
{
  const auto srcState = res.GetState();
  if(srcState == dstState && srcQueueFamily == dstQueueFamily) return;

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

  m_barriers.buffers.push_back(&res);
  m_barriers.bufferStates.push_back(dstState);
  m_barriers.bufferBarriers.push_back(bar);

  res.SetState(dstState);
}

void CommandBuffer::AddBarrier(Image& res, ResourceState dstState)
{
  AddBarrier(
    res, dstState, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED);
}

void CommandBuffer::AddBarrier(
  Image& res, ResourceState dstState, uint32_t srcQueueFamily,
  uint32_t dstQueueFamily)
{
  const auto srcState = res.GetState();

  if(srcState == dstState && srcQueueFamily == dstQueueFamily) return;

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

  m_barriers.images.push_back(&res);
  m_barriers.imageStates.push_back(dstState);
  m_barriers.imageBarriers.push_back(bar);

  // State is updated immediately because this is used when
  // queueing commands to the command buffer.
  // It is just for bookkeeping, it will match the state in the
  // resource after the command buffer is submitted.
  res.SetState(dstState);
}

void CommandBuffer::FlushBarriers()
{
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

  // Reset barriers
  m_barriers.memoryBarriers.clear();
  m_barriers.buffers.clear();
  m_barriers.bufferStates.clear();
  m_barriers.bufferBarriers.clear();
  m_barriers.images.clear();
  m_barriers.imageStates.clear();
  m_barriers.imageBarriers.clear();
}

void CommandBuffer::Copy(
  const Buffer& src, Buffer& dst, u64 srcOffset, u64 dstOffset,
  u64 size)
{
  if(src.GetSize() == 0u) {
    VDLogW("Copy: source buffer has zero size");
    return;
  }

  if(dst.GetSize() == 0u) {
    VDLogW("Copy: destination buffer has zero size");
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
  const auto& desc = dst.GetDesc();

  auto extent = desc.extent;
  for(u32 idx = 0u; idx < mip; ++idx) {
    if(extent[0] > 1u) extent[0] /= 2u;
    if(extent[1] > 1u) extent[1] /= 2u;
    if(extent[2] > 1u) extent[2] /= 2u;
  }

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
    .imageExtent       = toVkExtent3D(extent)};

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

// void CommandBuffer::Copy(Buffer& src, Image& dst)
//{
//  const auto& desc = dst.GetDesc();
//
//  Arr<VkBufferImageCopy> vkRegions{};
//
//  u64 offset = 0u;
//
//  for(u32 layer = 0; layer < desc.layerCount; ++layer) {
//    auto extent = toVkExtent3D(desc.extent);
//
//    for(u32 mip = 0u; mip < desc.mips; ++mip) {
//      u64 pitch = extent.width * getFormatSize(desc.format);
//      u64 size  = pitch * extent.height * extent.depth;
//
//      VkBufferImageCopy vkRegion{
//        .bufferOffset      = offset,
//        .bufferRowLength   = 0u,
//        .bufferImageHeight = 0u,
//        .imageOffset       = {0u, 0u, 0u},
//        .imageExtent       = extent};
//
//      vkRegion.imageSubresource.aspectMask =
//        getVkFormatAspect(desc.format);
//      vkRegion.imageSubresource.mipLevel       = mip;
//      vkRegion.imageSubresource.baseArrayLayer = layer;
//      vkRegion.imageSubresource.layerCount     = 1u;
//
//      vkRegions.push_back(vkRegion);
//
//      offset += size;
//
//      if(extent.width > 1u) extent.width /= 2u;
//      if(extent.height > 1u) extent.height /= 2u;
//      if(extent.depth > 1u) extent.depth /= 2u;
//    }
//  }
//
//  m_device.api().CmdCopyBufferToImage(
//    m_handle, src.GetHandle(), dst.GetHandle(),
//    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, vd::size32(vkRegions),
//    std::data(vkRegions));
//}

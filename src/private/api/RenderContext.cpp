#include "vuldir/api/RenderContext.hpp"

#include "vuldir/api/Buffer.hpp"
#include "vuldir/api/CommandBuffer.hpp"
#include "vuldir/api/Device.hpp"
#include "vuldir/api/Image.hpp"

using namespace vd;

RenderContext::RenderContext(Device& device, const Desc& desc):
  m_device{device},
  m_desc{desc},
  m_cmdPools{},
  m_cmdBufs{},
  m_transfersFence{device, "Transfer", Fence::Type::Timeline, 0u},
  m_inFlightFences{},
  m_inFlightFenceCount{0u},
  m_stagingBuffers{},
  m_freeStagingBuffers{}
{
  VD_UNUSED(m_desc);

  for(u32 idx = 0u; idx < QueueTypeCount; ++idx) {
    auto pool =
      std::make_unique<CommandPool>(m_device, QueueTypes[idx]);
    auto cmd = std::make_unique<CommandBuffer>(m_device, *pool);

    m_cmdPools[idx] = std::move(pool);
    m_cmdBufs[idx]  = std::move(cmd);
  }
}

RenderContext::~RenderContext()
{
  WaitInFlightOperations();

  // Free commandbuffers before the commandpools.
  for(auto& cmd: m_cmdBufs) cmd = nullptr;
  for(auto& pool: m_cmdPools) pool = nullptr;
}

void RenderContext::Reset()
{
  WaitInFlightOperations();
  for(auto& cmd: m_cmdBufs) cmd->Reset(true);
}

bool RenderContext::Write(Buffer& buffer, Span<u8 const> data)
{
  const auto size = std::size(data);

  VDLogI(
    "Writing %zu bytes to buffer %s", size,
    buffer.GetDesc().name.c_str());

  if(buffer.GetMemoryType() != MemoryType::Main)
    return buffer.Write(data);

  auto& stagingBuffer = getStagingBuffer(std::size(data));
  if(!stagingBuffer.Write(data)) {
    m_freeStagingBuffers.push_back(&stagingBuffer);
    return false;
  }

#ifdef VD_API_VK
  uint32_t transferQueueFamily =
    m_device.GetQueueFamily(QueueType::Copy);
  uint32_t graphicsQueueFamily =
    m_device.GetQueueFamily(QueueType::Graphics);
#endif

  // According to the spec, if we don't care about keeping the buffer contents
  // there is no need to transfer the buffer ownership to the copy queue family.

  auto& transferCmd = GetCmd(QueueType::Copy);

  transferCmd.Begin();
  transferCmd.AddBarrier(stagingBuffer, ResourceState::CopySrc);
  transferCmd.AddBarrier(buffer, ResourceState::CopyDst);
  transferCmd.FlushBarriers();
  transferCmd.Copy(stagingBuffer, buffer, 0, 0, size);

#ifdef VD_API_VK
  // If the transfer queue family is different from the graphics queue family
  // we need to transfer the ownership of the buffer from the transfer queue
  // to the graphics queue with a barrier.
  if(transferQueueFamily != graphicsQueueFamily) {
    transferCmd.AddBarrier(
      buffer, ResourceState::None, transferQueueFamily,
      graphicsQueueFamily);
    transferCmd.FlushBarriers();
  }
#endif

  transferCmd.End();

  m_device.Submit({&transferCmd}, {}, {&m_transfersFence});

#ifdef VD_API_VK
  // If the transfer queue family is different from the graphics queue
  // family we need to wait for the transfer queue to finish before
  // we can use the buffer in the graphics queue.
  // And we also need a barrier to transfer the ownership of the buffer
  // from the transfer queue to the graphics queue.
  if(transferQueueFamily != graphicsQueueFamily) {
    auto& graphicsCmd = GetCmd(QueueType::Graphics);
    graphicsCmd.Begin();
    graphicsCmd.AddBarrier(
      buffer, ResourceState::Undefined, transferQueueFamily,
      graphicsQueueFamily);
    graphicsCmd.FlushBarriers();
    graphicsCmd.End();

    m_device.Submit(
      {&graphicsCmd}, {&m_transfersFence}, {&m_transfersFence});
  }
#endif

  m_transfersFence.Wait();
  transferCmd.Reset();

  m_freeStagingBuffers.push_back(&stagingBuffer);

  return true;
}

bool RenderContext::Write(Image& image, Span<u8 const> data)
{
  const auto size = std::size(data);

  VDLogI(
    "Writing %zu bytes to image %s", size,
    image.GetDesc().name.c_str());

  if(image.GetMemoryType() != MemoryType::Main)
    return image.Write(data);

  auto& stagingBuffer = getStagingBuffer(std::size(data));
  if(!stagingBuffer.Write(data)) {
    m_freeStagingBuffers.push_back(&stagingBuffer);
    return false;
  }

#ifdef VD_API_VK
  uint32_t transferQueueFamily =
    m_device.GetQueueFamily(QueueType::Copy);
  uint32_t graphicsQueueFamily =
    m_device.GetQueueFamily(QueueType::Graphics);
#endif

  // According to the spec, if we don't care about keeping the buffer contents
  // there is no need to transfer the buffer ownership to the copy queue family.

  auto& transferCmd = GetCmd(QueueType::Copy);

  transferCmd.Begin();
  transferCmd.AddBarrier(stagingBuffer, ResourceState::CopySrc);
  transferCmd.AddBarrier(image, ResourceState::CopyDst);
  transferCmd.FlushBarriers();
  transferCmd.Copy(stagingBuffer, image);

#ifdef VD_API_VK
  // If the transfer queue family is different from the graphics queue family
  // we need to transfer the ownership of the image from the transfer queue
  // to the graphics queue with a barrier.
  if(transferQueueFamily != graphicsQueueFamily) {
    transferCmd.AddBarrier(
      image, ResourceState::ShaderResourceGraphics, transferQueueFamily,
      graphicsQueueFamily);
    transferCmd.FlushBarriers();
  }
#endif

  transferCmd.End();

  m_device.Submit({&transferCmd}, {}, {&m_transfersFence});

#ifdef VD_API_VK

  // If the transfer queue family is different from the graphics queue
  // family we need to wait for the transfer queue to finish before
  // we can use the buffer in the graphics queue.
  // And we also need a barrier to transfer the ownership of the buffer
  // from the transfer queue to the graphics queue.
  if(transferQueueFamily != graphicsQueueFamily) {
    auto& graphicsCmd = GetCmd(QueueType::Graphics);
    graphicsCmd.Begin();
    graphicsCmd.AddBarrier(
      image, ResourceState::ShaderResourceGraphics, transferQueueFamily,
      graphicsQueueFamily);
    graphicsCmd.FlushBarriers();
    graphicsCmd.End();

    m_device.Submit(
      {&graphicsCmd}, {&m_transfersFence}, {&m_transfersFence});
  }
#endif

  m_transfersFence.Wait();
  transferCmd.Reset();

  m_freeStagingBuffers.push_back(&stagingBuffer);

  return true;
}

void vd::RenderContext::Submit(
  Arr<CommandBuffer*> cmdbufs, Arr<Fence*> waits, Arr<Fence*> signals,
  SwapchainDep swapchainDep)
{
  auto& submitFence = getInFlightFence();
  m_device.Submit(
    std::move(cmdbufs), std::move(waits), std::move(signals),
    &submitFence, swapchainDep);
}

void vd::RenderContext::WaitInFlightOperations()
{
  for(u32 idx = 0u; idx < m_inFlightFenceCount; ++idx) {
    m_inFlightFences[idx]->Wait();
#ifdef VD_API_VK
    m_inFlightFences[idx]->Reset();
#endif
  }
  m_inFlightFenceCount = 0u;
}

Buffer& RenderContext::getStagingBuffer(u64 size)
{
  for(auto bufIt = std::begin(m_freeStagingBuffers);
      bufIt != std::end(m_freeStagingBuffers); ++bufIt) {
    auto& buf = **bufIt;
    if(buf.GetSize() >= size) {
      m_freeStagingBuffers.erase(bufIt);
      return buf;
    }
  }

  auto buf = std::make_unique<Buffer>(
    m_device, Buffer::Desc{
                .name        = "Staging",
                .usage       = {},
                .size        = size,
                .defaultView = {},
                .memoryType  = MemoryType::Upload,
                .isStaging   = true});
  m_stagingBuffers.push_back(std::move(buf));
  return *m_stagingBuffers.back();
}

Fence& vd::RenderContext::getInFlightFence()
{
  // Reuse fences if possible.
  if(m_inFlightFenceCount < std::size(m_inFlightFences)) {
    return *m_inFlightFences[m_inFlightFenceCount++];
  }

  ++m_inFlightFenceCount;
  auto fence = std::make_unique<Fence>(
    m_device, formatString("InFlightFence%u", m_inFlightFenceCount),
#ifdef VD_API_VK
    Fence::Type::Fence
#elif VD_API_DX
    Fence::Type::Timeline
#endif
  );
  m_inFlightFences.push_back(std::move(fence));

  return *m_inFlightFences.back();
}

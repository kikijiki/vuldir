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
  m_transfersActive{false},
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

bool RenderContext::WithData(
  Span<u8 const> data, const std::function<bool(Buffer&)>& fun)
{
  auto& stagingBuffer = getStagingBuffer(std::size(data));
  if(!stagingBuffer.Write(data)) {
    m_freeStagingBuffers.push_back(&stagingBuffer);
    return false;
  }

  return fun(stagingBuffer);
}

bool RenderContext::Write(Buffer& buffer, Span<u8 const> data)
{
  if(buffer.GetMemoryType() != MemoryType::Main)
    return buffer.Write(data);

  return WithData(data, [this, &buffer](Buffer& src) {
    GetCmd(QueueType::Copy).Copy(src, buffer);
    return true;
  });
}

bool RenderContext::Write(Image& image, Span<u8 const> data)
{
  if(image.GetMemoryType() != MemoryType::Main)
    return image.Write(data);

  return WithData(data, [this, &image](Buffer& src) {
    GetCmd(QueueType::Copy).Copy(src, image, 0u, 0u, 0u);
    return true;
  });
}

void RenderContext::BeginTransfers()
{
  if(m_transfersActive) return;

  auto& cmd = GetCmd(QueueType::Copy);
  cmd.Begin();
  m_transfersActive = true;
}

void RenderContext::EndTransfers()
{
  if(!m_transfersActive) return;

  auto& cmd = GetCmd(QueueType::Copy);
  cmd.End();

  CommandBuffer* cmds[]    = {&cmd};
  Fence*         signals[] = {&m_transfersFence};

  m_transfersFence.Step();
  m_device.Submit(cmds, {}, signals);
  m_transfersFence.Wait();

  m_freeStagingBuffers.clear();
  for(auto& buf: m_stagingBuffers) {
    m_freeStagingBuffers.push_back(buf.get());
  }

  // Smallest -> Largest
  std::sort(
    m_freeStagingBuffers.begin(), m_freeStagingBuffers.end(),
    [](auto& a, auto& b) { return a->GetSize() > b->GetSize(); });

  m_transfersActive = false;
}

void vd::RenderContext::Submit(
  Span<CommandBuffer*> cmdbufs, Span<Fence*> waits,
  Span<Fence*> signals, SwapchainDep swapchainDep)
{
  auto& submitFence = getInFlightFence();
  m_device.Submit(cmdbufs, waits, signals, &submitFence, swapchainDep);
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
    m_device,
    Buffer::Desc{"Staging", {}, size, {}, MemoryType::Upload});
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

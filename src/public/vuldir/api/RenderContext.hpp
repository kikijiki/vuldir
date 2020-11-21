#pragma once

#include "vuldir/api/Common.hpp"
#include "vuldir/api/Fence.hpp"

namespace vd {

class Device;
struct CommandPool;
class CommandBuffer;
class Buffer;
class Image;

class RenderContext
{
public:
  struct Desc {};

  struct Transfer {};

public:
  VD_NONMOVABLE(RenderContext);

  RenderContext(Device& device, const Desc& desc);
  ~RenderContext();

  Device& GetDevice() const { return m_device; }

  void Reset();

  CommandBuffer& GetCmd(QueueType type)
  {
    return *m_cmdBufs[enumValue(type)];
  }

  bool WithData(
    Span<u8 const> data, const std::function<bool(Buffer&)>& fun);

  bool Write(Buffer& buffer, Span<u8 const> data);
  template<typename T>
  bool Write(Buffer& buffer, const T& data)
  {
    return Write(buffer, getBytes(data));
  }

  bool Write(Image& image, Span<u8 const> data);

  void BeginTransfers();
  void EndTransfers();

  void Submit(
    Arr<CommandBuffer*> cmdbufs, Arr<Fence*> waits = {},
    Arr<Fence*>  signals      = {},
    SwapchainDep swapchainDep = SwapchainDep::None);

  void WaitInFlightOperations();

private:
  Buffer& getStagingBuffer(u64 size);
  Fence&  getInFlightFence();

private:
  Device& m_device;
  Desc    m_desc;

  SArr<UPtr<CommandPool>, QueueTypeCount>   m_cmdPools;
  SArr<UPtr<CommandBuffer>, QueueTypeCount> m_cmdBufs;

  bool              m_transfersActive = false;
  Fence             m_transfersFence;
  Arr<UPtr<Fence>>  m_inFlightFences;
  u32               m_inFlightFenceCount;
  Arr<UPtr<Buffer>> m_stagingBuffers;
  Arr<Buffer*>      m_freeStagingBuffers;
};

} // namespace vd

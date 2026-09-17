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
  struct Desc {
    u32 maxFramesInFlight = 2u;
  };

  struct Transfer {};

public:
  VD_NONMOVABLE(RenderContext);

  RenderContext(Device& device, const Desc& desc);
  ~RenderContext();

  Device& GetDevice() const { return m_device; }

  u32 GetMaxFramesInFlight() const { return m_framesInFlight; }
  u32 GetFrameIndex() const { return m_frameIndex; }

  // Prepares the current swapchain frame slot: waits that slot's fence,
  // retires deferred resources, resets that slot's command buffers.
  void Reset();

  CommandBuffer& GetCmd(QueueType type)
  {
    if(!isValid(type))
      throw std::out_of_range("Queue type is invalid");
    return *m_frames[m_frameIndex].cmdBufs[enumValue(type)];
  }

  // Synchronous upload. Records on the current Copy cmdbuf, then uses the
  // Graphics cmdbuf for the final-state transition (and Vulkan QFOT
  // acquire). Graphics must be Ready (not Recording, and not Closed
  // without a Reset).
  bool Write(
    Buffer& buffer, Span<u8 const> data,
    ResourceState finalState = ResourceState::ShaderResourceGraphics);
  template<typename T>
  bool Write(
    Buffer& buffer, const T& data,
    ResourceState finalState = ResourceState::ShaderResourceGraphics)
  {
    return Write(buffer, getBytes(data), finalState);
  }

  bool Write(
    Image& image, Span<u8 const> data,
    ResourceState finalState = ResourceState::ShaderResourceGraphics);
  template<typename T>
  bool Write(
    Image& image, const T& data,
    ResourceState finalState = ResourceState::ShaderResourceGraphics)
  {
    return Write(image, getBytes(data), finalState);
  }

  void Submit(
    Arr<CommandBuffer*> cmdbufs, Arr<Fence*> waits = {},
    Arr<Fence*>  signals      = {},
    SwapchainDep swapchainDep = SwapchainDep::None);

  void WaitInFlightOperations();

private:
  struct FrameSlot {
    SArr<UPtr<CommandPool>, QueueTypeCount>   cmdPools;
    SArr<UPtr<CommandBuffer>, QueueTypeCount> cmdBufs;
    UPtr<Fence>                               fence;
    bool                                      submitted = false;
  };

  Buffer& getStagingBuffer(u64 size);
  void    returnStagingBuffer(Buffer& buffer);
  void    retireFrame(u32 slot);
  void    ensureGraphicsReady(CommandBuffer& graphicsCmd) const;

private:
  Device& m_device;
  Desc    m_desc;

  u32            m_framesInFlight;
  u32            m_frameIndex;
  u64            m_binderContext;
  Arr<FrameSlot> m_frames;

  Fence             m_transfersFence;
  Arr<UPtr<Buffer>> m_stagingBuffers;
  Arr<Buffer*>      m_freeStagingBuffers;
};

} // namespace vd

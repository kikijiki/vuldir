#pragma once

#include "vuldir/api/Common.hpp"
#include "vuldir/api/Image.hpp"

namespace vd {

class Device;
class Buffer;
// class Image;

struct CommandPool {
  CommandPool(Device& device, QueueType type);
  ~CommandPool();

  void Reset();

  Device&   device;
  QueueType type;

#ifdef VD_API_VK
  VkCommandPool handle;
#elif VD_API_DX
  ComPtr<ID3D12CommandAllocator> handle;
  D3D12_COMMAND_LIST_TYPE        dxType;
#endif
};

class CommandBuffer
{
public:
  enum class State { Ready, Closed, Recording };

  struct Attachment {
    const Image::View* view         = nullptr;
    ResourceState      state        = ResourceState::Undefined;
    ResolveMode        resolveMode  = ResolveMode::None;
    const Image::View* resolveView  = nullptr;
    ResourceState      resolveState = ResourceState::Undefined;
    LoadOp             loadOp       = LoadOp::DontCare;
    StoreOp            storeOp      = StoreOp::DontCare;
    ClearValue         clearValue;
  };

public:
  VD_NONMOVABLE(CommandBuffer);

  CommandBuffer(Device& device, CommandPool& pool);
  ~CommandBuffer();

#ifdef VD_API_VK
  VkCommandBuffer GetHandle() const { return m_handle; }
#elif VD_API_DX
  ID3D12GraphicsCommandList5& GetHandle() { return *m_handle.Get(); }
#endif

  void Reset(bool resetPool = false);
  void Begin();
  void End();

  void BeginRendering(
    Span<Attachment const> color        = {},
    const Attachment*      depthStencil = nullptr);
  void EndRendering();

  void PushConstants(Span<u32 const> data);
  template<typename T>
  void PushConstants(const T& data)
  {
    static_assert(std::alignment_of<T>::value % sizeof(u32) == 0u);
    Span<u32 const> span{
      reinterpret_cast<const u32*>(&data), sizeof(T) / sizeof(u32)};
    PushConstants(span);
  }

  void SetViewport(const Viewport& viewport);
  void SetScissor(const Rect& rect);

  void Draw(
    u32 vertexCount, u32 instanceCount = 1u, u32 vertexOffset = 0u,
    u32 instanceOffset = 0u);
  void DrawIndexed(
    u32 vertexCount, u32 instanceCount = 1u, u32 indexOffset = 0u,
    u32 vertexOffset = 0u, u32 instanceOffset = 0u);

  void AddBarrier();
  void AddBarrier(Buffer& res, ResourceState dstState);
  void AddBarrier(Image& res, ResourceState dstState);
  void FlushBarriers();

  void Copy(const Buffer& src, Buffer& dst);
  void Copy(
    const Buffer& src, Buffer& dst, u64 srcOffset, u64 dstOffset,
    u64 size);
  void Copy(
    const Buffer& src, Image& dst, u64 offset = 0u, u32 mip = 0,
    u32 layer = 0);

  QueueType GetQueueType() const { return m_pool.type; }

private:
  struct Barriers {
#ifdef VD_API_VK
    VkPipelineStageFlags srcStage;
    VkPipelineStageFlags dstStage;

    Arr<VkMemoryBarrier> memoryBarriers;

    Arr<const Buffer*>         buffers;
    Arr<ResourceState>         bufferStates;
    Arr<VkBufferMemoryBarrier> bufferBarriers;

    Arr<const Image*>         images;
    Arr<ResourceState>        imageStates;
    Arr<VkImageMemoryBarrier> imageBarriers;
#elif VD_API_DX
    using Resource = Var<std::monostate, Buffer*, Image*>;
    Arr<Resource>               resources;
    Arr<ResourceState>          states;
    Arr<D3D12_RESOURCE_BARRIER> barriers;
#endif
  };

private:
  Device&      m_device;
  CommandPool& m_pool;

  State    m_state;
  Barriers m_barriers;

#ifdef VD_API_VK
  VkCommandBuffer m_handle;
  VkRect2D        m_renderArea;
#elif VD_API_DX
  ComPtr<ID3D12GraphicsCommandList5> m_handle;
#endif
};

} // namespace vd

#pragma once

#include "vuldir/api/Common.hpp"
#include "vuldir/api/Image.hpp"

namespace vd {

class Device;
class Buffer;
class Pipeline;

struct CommandPool {
  CommandPool(Device& device, QueueType type);
  ~CommandPool();

  Device&   device;
  QueueType type;

#ifdef VD_API_VK
  VkCommandPool handle;
#elif VD_API_DX
  ComPtr<ID3D12CommandAllocator> handle;
  D3D12_COMMAND_LIST_TYPE        dxType;
#endif

private:
  friend class CommandBuffer;
  void Reset();
};

class CommandBuffer
{
public:
  static constexpr u64 PushConstantsSize = 16u;

  enum class State { Ready, Closed, Recording };

  // Attachment::state is the state expected at BeginRendering (selects
  // the imageLayout on Vulkan) and must match the barrier the caller
  // issued. The render area always covers the attachments; use
  // SetScissor to restrict draws.
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

  State GetState() const { return m_state; }

  void Reset();
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
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(sizeof(T) % sizeof(u32) == 0u);
    static_assert(sizeof(T) <= PushConstantsSize);
    Span<u32 const> span{
      reinterpret_cast<const u32*>(&data), sizeof(T) / sizeof(u32)};
    PushConstants(span);
  }

  void SetViewport(const Viewport& viewport);
  void SetScissor(const Rect& rect);
  void BindIndexBuffer(
    const Buffer& buffer, IndexType indexType, u64 offset = 0u);

  void Draw(
    u32 vertexCount, u32 instanceCount = 1u, u32 vertexOffset = 0u,
    u32 instanceOffset = 0u);
  void DrawIndexed(
    u32 vertexCount, u32 instanceCount = 1u, u32 indexOffset = 0u,
    u32 vertexOffset = 0u, u32 instanceOffset = 0u);
  void Dispatch(u32 groupCountX, u32 groupCountY = 1u, u32 groupCountZ = 1u);

  void AddBarrier();
  void AddBarrier(Buffer& res, ResourceState dstState);
  void AddBarrier(Image& res, ResourceState dstState);

  void FlushBarriers();

  void Copy(
    const Buffer& src, Buffer& dst, u64 srcOffset, u64 dstOffset,
    u64 size);
  void Copy(
    const Buffer& src, Image& dst, u64 offset = 0u, u32 mip = 0,
    u32 layer = 0);

  QueueType GetQueueType() const { return m_pool.type; }

private:
  friend class Device;
  friend class Pipeline;
  friend class RenderContext;

  void          reset(bool resetPool);
  ResourceState getState(Buffer& res) const;
  ResourceState getState(Image& res) const;
  void          commitResourceStates();
  void          requireRecording(const char* operation) const;

  struct Barriers {
#ifdef VD_API_VK
    Arr<VkMemoryBarrier2> memoryBarriers;
    Arr<VkBufferMemoryBarrier2> bufferBarriers;
    Arr<VkImageMemoryBarrier2> imageBarriers;
#elif VD_API_DX
    Arr<D3D12_RESOURCE_BARRIER> barriers;
#endif

    struct PendingState {
      ResourceState initial;
      ResourceState final;
    };

    // State as seen while recording. Survives FlushBarriers() and is
    // published to resources after submission. The initial state lets
    // Device reject a stale recording if another command buffer committed
    // first.
    Map<Buffer*, PendingState> pendingBuffers;
    Map<Image*, PendingState>  pendingImages;
  };

private:
  Device&      m_device;
  CommandPool& m_pool;

  State    m_state;
  BindPoint m_bindPoint;
  bool      m_pipelineBound;
  bool      m_indexBufferBound;
  bool      m_rendering;
  Barriers m_barriers;

#ifdef VD_API_VK
  VkCommandBuffer m_handle;
#elif VD_API_DX
  ComPtr<ID3D12GraphicsCommandList5> m_handle;
#endif
};

} // namespace vd

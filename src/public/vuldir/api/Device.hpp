#pragma once

#include "vuldir/api/Common.hpp"
#include "vuldir/api/MemoryPool.hpp"
#include "vuldir/api/Swapchain.hpp"

namespace vd {
#ifdef VD_API_VK
class Dispatcher;
#elif VD_API_DX
class CPUDescriptorPool;
#endif

class Binder;
class CommandBuffer;
class Fence;
class HostAllocator;
class PhysicalDevice;
class Swapchain;

class Device
{
public:
  struct Stats {
    u64 gpuMemoryUsed       = 0u;
    u64 gpuMemoryCommitted  = 0u;
    u32 gpuMemoryPools      = 0u;
    u32 descriptorsUsed     = 0u;
    u32 descriptorsCapacity = 0u;
    u32 buffers             = 0u;
    u32 images              = 0u;
    u32 pipelines           = 0u;
    u32 shadersLoaded       = 0u;
  };

  struct Desc {
    Str appName;
    u32 appVersion = 0u;

    bool     dbgEnable              = false;
    bool     dbgBreakOnError        = false;
    bool     dbgBreakOnWarning      = false;
    bool     dbgUseSoftwareRenderer = false;
    bool     dbgUseDRED             = false;
    bool     dbgDumpLog             = false;
    bool     dbgUseRenderdoc        = false;
    LogLevel dbgLogLevel            = LogLevel::Warning;

    u64  memMainPoolSize     = 256_MB;
    u64  memUploadPoolSize   = 64_MB;
    u64  memDownloadPoolSize = 64_MB;
    bool memDebugVerbose     = false;
  };

public:
  VD_NONMOVABLE(Device);

  Device(const Desc& desc, const Swapchain::Desc& swapchainDesc);
  ~Device();

public:
  Binder&          GetBinder() { return *m_binder; }
  const Binder&    GetBinder() const { return *m_binder; }
  Swapchain&       GetSwapchain() { return *m_swapchain; }
  const Swapchain& GetSwapchain() const { return *m_swapchain; }

  void Submit(
    Arr<CommandBuffer*> cmds, Arr<Fence*> waits = {},
    Arr<Fence*> signals = {}, Fence* submitFence = nullptr,
    SwapchainDep swapchainDep = SwapchainDep::None);

  bool Wait(QueueType queue, Fence& fence) const;
  bool Wait(QueueType queue, Fence& fence, u64 value) const;
  bool Signal(QueueType queue, Fence& fence);
  bool Signal(QueueType queue, Fence& fence, u64 value);

  void WaitIdle(QueueType queue);
  void WaitIdle();

  const PhysicalDevice& GetPhysicalDevice() const
  {
    return *m_physicalDevice;
  }

  bool IsDebugEnabled() const { return m_desc.dbgEnable; }

  Stats GetStats();

  MemoryPool::Allocation AllocateMemory(
    MemoryType type, u64 size, u64 alignment, u32 memoryTypeBits = ~0u);

#ifdef VD_API_VK
  Dispatcher& api() { return *m_dispatcher; }

  VkInstance GetInstance() const { return m_instance; }
  VkDevice   GetHandle() const { return m_handle; }
  VkQueue    GetQueueHandle(QueueType queue) const
  {
    if(!isValid(queue))
      throw std::out_of_range("Queue type is invalid");
    return m_queues[enumValue(queue)].handle;
  }
  u32 GetQueueFamily(QueueType queue) const
  {
    if(!isValid(queue))
      throw std::out_of_range("Queue type is invalid");
    return m_queues[enumValue(queue)].family;
  }
  const VkAllocationCallbacks* GetAllocationCallbacks() const;
  VkSurfaceKHR                 GetSurface() const { return m_surface; }
  VkSurfaceCapabilitiesKHR     GetSurfaceCapabilities() const;

  void SetObjectName(u64 handle, u64 type, const char* name);
  template<typename T>
  void SetObjectName(T* handle, u64 type, const char* name)
  {
    SetObjectName(reinterpret_cast<u64>(handle), type, name);
  }
#elif VD_API_DX
  ID3D12Device8&      api() const { return *m_handle.Get(); }
  ID3D12CommandQueue& GetQueueHandle(QueueType queue) const
  {
    if(!isValid(queue))
      throw std::out_of_range("Queue type is invalid");
    return *m_queues[enumValue(queue)].handle.Get();
  }
  CPUDescriptorPool& GetDescriptorPool() { return *m_descriptorPool; }
  void               ReportLiveObjects() const;
#endif

private:
  friend class Buffer;
  friend class Image;
  friend class Pipeline;
  friend class Shader;
  friend class Swapchain;

  void create_api(const Desc& desc);
  void create_physicalDevice(const Desc& desc);
  void create_device(const Desc& desc);
  void onDeviceRemoved();
  void validateResourceStates(const Arr<CommandBuffer*>& cmds) const;

private:
  struct Queue {
    Str name;
    SPtr<std::recursive_mutex> mutex;
#ifdef VD_API_VK
    u32     family;
    u32     index;
    VkQueue handle;
#elif VD_API_DX
    ComPtr<ID3D12CommandQueue> handle;
#endif
  };

  Desc                 m_desc;
  WindowHandle         m_window;
  UPtr<PhysicalDevice> m_physicalDevice;

  UPtr<Swapchain>             m_swapchain;
  UPtr<Binder>                m_binder;
  SArr<Queue, QueueTypeCount> m_queues;
  Arr<UPtr<MemoryPool>>       m_memoryPools;
  u32                         m_memoryPoolSerial = 0u;
  std::mutex                  m_memoryMutex;
  mutable std::mutex          m_resourceStateMutex;
  std::atomic<u32>            m_bufferCount     = 0u;
  std::atomic<u32>            m_imageCount      = 0u;
  std::atomic<u32>            m_pipelineCount   = 0u;
  std::atomic<u32>            m_shaderLoadCount = 0u;

#ifdef VD_API_VK
  mutable UPtr<HostAllocator> m_hostAllocator;
  mutable UPtr<Dispatcher>    m_dispatcher;

  VkInstance   m_instance;
  VkDevice     m_handle;
  VkSurfaceKHR m_surface;
#elif VD_API_DX
  ComPtr<ID3D12Device8>   m_handle;
  UPtr<CPUDescriptorPool> m_descriptorPool;
#endif
};

} // namespace vd

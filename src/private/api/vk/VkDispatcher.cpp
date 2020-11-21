#include "vuldir/api/vk/VkDispatcher.hpp"

#include "vuldir/api/Device.hpp"

using namespace vd;

// extern "C" {
//  VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
//    vkGetInstanceProcAddr(VkInstance, const char*);
//}

#define GetDefaultPFN(NAME)                                     \
  {                                                             \
    const auto fun =                                            \
      pfn.GetInstanceProcAddr(nullptr, "vk" VD_STR(NAME));      \
    if(fun) { pfn.NAME = reinterpret_cast<PFN_vk##NAME>(fun); } \
  }

#define GetInstancePFN(NAME)                                    \
  {                                                             \
    const auto fun =                                            \
      pfn.GetInstanceProcAddr(m_vkInstance, "vk" VD_STR(NAME)); \
    if(fun) { pfn.NAME = reinterpret_cast<PFN_vk##NAME>(fun); } \
  }

#define GetDevicePFN(NAME)                                      \
  {                                                             \
    const auto fun =                                            \
      pfn.GetDeviceProcAddr(m_vkDevice, "vk" VD_STR(NAME));     \
    if(fun) { pfn.NAME = reinterpret_cast<PFN_vk##NAME>(fun); } \
  }

Dispatcher::Dispatcher():
  m_library{m_libraryPath},
  m_vkInstance{},
  m_vkDevice{},
  m_callbacks{},
  m_source{Source::None},
  pfn{}
{
  loadDefault();
}

Dispatcher::Dispatcher(Device& device): Dispatcher() { Load(device); }

Dispatcher::~Dispatcher()
{
  clear();

  m_vkInstance = nullptr;
  m_vkDevice   = nullptr;
  m_callbacks  = nullptr;
}

void Dispatcher::Load(Device& device)
{
  // Already loaded.
  if(m_source == Source::Device) return;

  m_callbacks = device.GetAllocationCallbacks();

  auto vkInstance = device.GetInstance();
  auto vkDevice   = device.GetHandle();

  if(vkDevice) loadDevice(vkDevice);
  else if(vkInstance)
    loadInstance(vkInstance);
}

void Dispatcher::loadDefault()
{
  m_source = Source::Default;

  clear();

  pfn.GetInstanceProcAddr =
    m_library.GetFunction<PFN_vkGetInstanceProcAddr>(
      "vkGetInstanceProcAddr");

  GetDefaultPFN(GetDeviceProcAddr);

  // Vulkan core ////////////////////////////////////////////////////
  GetDefaultPFN(EnumerateInstanceExtensionProperties);
  GetDefaultPFN(EnumerateInstanceLayerProperties);
  GetDefaultPFN(EnumerateInstanceVersion);
  GetDefaultPFN(EnumeratePhysicalDevices);
  GetDefaultPFN(CreateInstance);
  GetDefaultPFN(DestroyInstance);
  GetDefaultPFN(CreateDevice);
  GetDefaultPFN(DestroyDevice);
  GetDefaultPFN(DeviceWaitIdle);
  GetDefaultPFN(GetPhysicalDeviceProperties2);
  GetDefaultPFN(GetPhysicalDeviceFeatures2);
  GetDefaultPFN(GetPhysicalDeviceQueueFamilyProperties2);
  GetDefaultPFN(GetPhysicalDeviceMemoryProperties2);
  GetDefaultPFN(GetPhysicalDeviceFormatProperties);
  GetDefaultPFN(GetPhysicalDeviceFormatProperties2);
  GetDefaultPFN(GetPhysicalDeviceImageFormatProperties);
  GetDefaultPFN(GetDeviceQueue);
  GetDefaultPFN(QueueSubmit);
  GetDefaultPFN(QueueWaitIdle);
  GetDefaultPFN(QueueBindSparse);
  GetDefaultPFN(CreateBuffer);
  GetDefaultPFN(DestroyBuffer);
  GetDefaultPFN(GetBufferMemoryRequirements);
  GetDefaultPFN(BindBufferMemory);
  GetDefaultPFN(CreateImage);
  GetDefaultPFN(DestroyImage);
  GetDefaultPFN(GetImageMemoryRequirements);
  GetDefaultPFN(BindImageMemory);
  GetDefaultPFN(CreateBufferView);
  GetDefaultPFN(DestroyBufferView);
  GetDefaultPFN(CreateImageView);
  GetDefaultPFN(DestroyImageView);
  GetDefaultPFN(AllocateMemory);
  GetDefaultPFN(FreeMemory);
  GetDefaultPFN(MapMemory);
  GetDefaultPFN(UnmapMemory);
  GetDefaultPFN(FlushMappedMemoryRanges);
  GetDefaultPFN(CreateFramebuffer);
  GetDefaultPFN(DestroyFramebuffer);
  GetDefaultPFN(CreateShaderModule);
  GetDefaultPFN(DestroyShaderModule);
  GetDefaultPFN(CreateCommandPool);
  GetDefaultPFN(DestroyCommandPool);
  GetDefaultPFN(ResetCommandPool);
  GetDefaultPFN(TrimCommandPool);
  GetDefaultPFN(AllocateCommandBuffers);
  GetDefaultPFN(FreeCommandBuffers);
  GetDefaultPFN(BeginCommandBuffer);
  GetDefaultPFN(EndCommandBuffer);
  GetDefaultPFN(ResetCommandBuffer);
  GetDefaultPFN(CreateRenderPass);
  GetDefaultPFN(CreateRenderPass2);
  GetDefaultPFN(DestroyRenderPass);
  GetDefaultPFN(CreateFence);
  GetDefaultPFN(DestroyFence);
  GetDefaultPFN(ResetFences);
  GetDefaultPFN(WaitForFences);
  GetDefaultPFN(GetFenceStatus);
  GetDefaultPFN(CreateSemaphore);
  GetDefaultPFN(DestroySemaphore);
  GetDefaultPFN(WaitSemaphores);
  GetDefaultPFN(SignalSemaphore);
  GetDefaultPFN(GetSemaphoreCounterValue);
  GetDefaultPFN(CreateGraphicsPipelines);
  GetDefaultPFN(CreateComputePipelines);
  GetDefaultPFN(DestroyPipeline);
  GetDefaultPFN(CreatePipelineCache);
  GetDefaultPFN(DestroyPipelineCache);
  GetDefaultPFN(GetPipelineCacheData);
  GetDefaultPFN(MergePipelineCaches);
  GetDefaultPFN(CreatePipelineLayout);
  GetDefaultPFN(DestroyPipelineLayout);
  GetDefaultPFN(CreateDescriptorSetLayout);
  GetDefaultPFN(DestroyDescriptorSetLayout);
  GetDefaultPFN(CreateDescriptorPool);
  GetDefaultPFN(DestroyDescriptorPool);
  GetDefaultPFN(ResetDescriptorPool);
  GetDefaultPFN(AllocateDescriptorSets);
  GetDefaultPFN(FreeDescriptorSets);
  GetDefaultPFN(UpdateDescriptorSets);
  GetDefaultPFN(CreateSampler);
  GetDefaultPFN(DestroySampler);

  // Commands ///////////////////////////////////////////////////////
  GetDefaultPFN(CmdExecuteCommands);
  GetDefaultPFN(CmdBeginRendering);
  GetDefaultPFN(CmdEndRendering);
  GetDefaultPFN(CmdBeginRenderPass);
  GetDefaultPFN(CmdEndRenderPass);
  GetDefaultPFN(CmdNextSubpass);
  GetDefaultPFN(CmdBeginRenderPass2);
  GetDefaultPFN(CmdEndRenderPass2);
  GetDefaultPFN(CmdNextSubpass2);
  GetDefaultPFN(CmdBindPipeline);
  GetDefaultPFN(CmdBindVertexBuffers);
  GetDefaultPFN(CmdBindIndexBuffer);
  GetDefaultPFN(CmdBindDescriptorSets);
  GetDefaultPFN(CmdPushConstants);
  GetDefaultPFN(CmdSetBlendConstants);
  GetDefaultPFN(CmdSetDepthBias);
  GetDefaultPFN(CmdSetDepthBounds);
  GetDefaultPFN(CmdSetScissor);
  GetDefaultPFN(CmdSetStencilCompareMask);
  GetDefaultPFN(CmdSetStencilReference);
  GetDefaultPFN(CmdSetStencilWriteMask);
  GetDefaultPFN(CmdSetViewport);
  GetDefaultPFN(CmdCopyBuffer);
  GetDefaultPFN(CmdCopyImage);
  GetDefaultPFN(CmdCopyImageToBuffer);
  GetDefaultPFN(CmdCopyBufferToImage);
  GetDefaultPFN(CmdBlitImage);
  GetDefaultPFN(CmdResolveImage);
  GetDefaultPFN(CmdUpdateBuffer);
  GetDefaultPFN(CmdDraw);
  GetDefaultPFN(CmdDrawIndirect);
  GetDefaultPFN(CmdDrawIndexed);
  GetDefaultPFN(CmdDrawIndexedIndirect);
  GetDefaultPFN(CmdDispatch);
  GetDefaultPFN(CmdDispatchBase);
  GetDefaultPFN(CmdDispatchIndirect);
  GetDefaultPFN(CmdClearAttachments);
  GetDefaultPFN(CmdClearColorImage);
  GetDefaultPFN(CmdClearDepthStencilImage);
  GetDefaultPFN(CmdFillBuffer);
  GetDefaultPFN(CmdPipelineBarrier);
  GetDefaultPFN(CmdSetEvent);
  GetDefaultPFN(CmdResetEvent);
  GetDefaultPFN(CmdWaitEvents);
  GetDefaultPFN(CmdBeginQuery);
  GetDefaultPFN(CmdEndQuery);
  GetDefaultPFN(CmdCopyQueryPoolResults);
  GetDefaultPFN(CmdResetQueryPool);
  GetDefaultPFN(CmdWriteTimestamp);

  // Extensions /////////////////////////////////////////////////////
  // - skip -
}

void Dispatcher::loadInstance(VkInstance instance)
{
  m_source     = Source::Instance;
  m_vkInstance = instance;

  GetInstancePFN(GetDeviceProcAddr);

  // Vulkan core ////////////////////////////////////////////////////
  GetInstancePFN(EnumerateInstanceExtensionProperties);
  GetInstancePFN(EnumerateInstanceLayerProperties);
  GetInstancePFN(EnumerateInstanceVersion);
  GetInstancePFN(EnumeratePhysicalDevices);
  GetInstancePFN(CreateInstance);
  GetInstancePFN(DestroyInstance);
  GetInstancePFN(CreateDevice);
  GetInstancePFN(DestroyDevice);
  GetInstancePFN(DeviceWaitIdle);
  GetInstancePFN(GetPhysicalDeviceProperties2);
  GetInstancePFN(GetPhysicalDeviceFeatures2);
  GetInstancePFN(GetPhysicalDeviceQueueFamilyProperties2);
  GetInstancePFN(GetPhysicalDeviceMemoryProperties2);
  GetInstancePFN(GetPhysicalDeviceFormatProperties);
  GetInstancePFN(GetPhysicalDeviceFormatProperties2);
  GetInstancePFN(GetPhysicalDeviceImageFormatProperties);
  GetInstancePFN(GetDeviceQueue);
  GetInstancePFN(QueueSubmit);
  GetInstancePFN(QueueWaitIdle);
  GetInstancePFN(QueueBindSparse);
  GetInstancePFN(CreateBuffer);
  GetInstancePFN(DestroyBuffer);
  GetInstancePFN(GetBufferMemoryRequirements);
  GetInstancePFN(BindBufferMemory);
  GetInstancePFN(CreateImage);
  GetInstancePFN(DestroyImage);
  GetInstancePFN(GetImageMemoryRequirements);
  GetInstancePFN(BindImageMemory);
  GetInstancePFN(CreateBufferView);
  GetInstancePFN(DestroyBufferView);
  GetInstancePFN(CreateImageView);
  GetInstancePFN(DestroyImageView);
  GetInstancePFN(AllocateMemory);
  GetInstancePFN(FreeMemory);
  GetInstancePFN(MapMemory);
  GetInstancePFN(UnmapMemory);
  GetInstancePFN(FlushMappedMemoryRanges);
  GetInstancePFN(CreateFramebuffer);
  GetInstancePFN(DestroyFramebuffer);
  GetInstancePFN(CreateShaderModule);
  GetInstancePFN(DestroyShaderModule);
  GetInstancePFN(CreateCommandPool);
  GetInstancePFN(DestroyCommandPool);
  GetInstancePFN(ResetCommandPool);
  GetInstancePFN(TrimCommandPool);
  GetInstancePFN(AllocateCommandBuffers);
  GetInstancePFN(FreeCommandBuffers);
  GetInstancePFN(BeginCommandBuffer);
  GetInstancePFN(EndCommandBuffer);
  GetInstancePFN(ResetCommandBuffer);
  GetInstancePFN(CreateRenderPass);
  GetInstancePFN(CreateRenderPass2);
  GetInstancePFN(DestroyRenderPass);
  GetInstancePFN(CreateFence);
  GetInstancePFN(DestroyFence);
  GetInstancePFN(ResetFences);
  GetInstancePFN(WaitForFences);
  GetInstancePFN(GetFenceStatus);
  GetInstancePFN(CreateSemaphore);
  GetInstancePFN(DestroySemaphore);
  GetInstancePFN(WaitSemaphores);
  GetInstancePFN(SignalSemaphore);
  GetInstancePFN(GetSemaphoreCounterValue);
  GetInstancePFN(CreateGraphicsPipelines);
  GetInstancePFN(CreateComputePipelines);
  GetInstancePFN(DestroyPipeline);
  GetInstancePFN(CreatePipelineCache);
  GetInstancePFN(DestroyPipelineCache);
  GetInstancePFN(GetPipelineCacheData);
  GetInstancePFN(MergePipelineCaches);
  GetInstancePFN(CreatePipelineLayout);
  GetInstancePFN(DestroyPipelineLayout);
  GetInstancePFN(CreateDescriptorSetLayout);
  GetInstancePFN(DestroyDescriptorSetLayout);
  GetInstancePFN(CreateDescriptorPool);
  GetInstancePFN(DestroyDescriptorPool);
  GetInstancePFN(ResetDescriptorPool);
  GetInstancePFN(AllocateDescriptorSets);
  GetInstancePFN(FreeDescriptorSets);
  GetInstancePFN(UpdateDescriptorSets);
  GetInstancePFN(CreateSampler);
  GetInstancePFN(DestroySampler);

  // Commands ///////////////////////////////////////////////////////
  GetInstancePFN(CmdExecuteCommands);
  GetInstancePFN(CmdBeginRendering);
  GetInstancePFN(CmdEndRendering);
  GetInstancePFN(CmdBeginRenderPass);
  GetInstancePFN(CmdEndRenderPass);
  GetInstancePFN(CmdNextSubpass);
  GetInstancePFN(CmdBeginRenderPass2);
  GetInstancePFN(CmdEndRenderPass2);
  GetInstancePFN(CmdNextSubpass2);
  GetInstancePFN(CmdBindPipeline);
  GetInstancePFN(CmdBindVertexBuffers);
  GetInstancePFN(CmdBindIndexBuffer);
  GetInstancePFN(CmdBindDescriptorSets);
  GetInstancePFN(CmdPushConstants);
  GetInstancePFN(CmdSetBlendConstants);
  GetInstancePFN(CmdSetDepthBias);
  GetInstancePFN(CmdSetDepthBounds);
  GetInstancePFN(CmdSetScissor);
  GetInstancePFN(CmdSetStencilCompareMask);
  GetInstancePFN(CmdSetStencilReference);
  GetInstancePFN(CmdSetStencilWriteMask);
  GetInstancePFN(CmdSetViewport);
  GetInstancePFN(CmdCopyBuffer);
  GetInstancePFN(CmdCopyImage);
  GetInstancePFN(CmdCopyImageToBuffer);
  GetInstancePFN(CmdCopyBufferToImage);
  GetInstancePFN(CmdBlitImage);
  GetInstancePFN(CmdResolveImage);
  GetInstancePFN(CmdUpdateBuffer);
  GetInstancePFN(CmdDraw);
  GetInstancePFN(CmdDrawIndirect);
  GetInstancePFN(CmdDrawIndexed);
  GetInstancePFN(CmdDrawIndexedIndirect);
  GetInstancePFN(CmdDispatch);
  GetInstancePFN(CmdDispatchBase);
  GetInstancePFN(CmdDispatchIndirect);
  GetInstancePFN(CmdClearAttachments);
  GetInstancePFN(CmdClearColorImage);
  GetInstancePFN(CmdClearDepthStencilImage);
  GetInstancePFN(CmdFillBuffer);
  GetInstancePFN(CmdPipelineBarrier);
  GetInstancePFN(CmdSetEvent);
  GetInstancePFN(CmdResetEvent);
  GetInstancePFN(CmdWaitEvents);
  GetInstancePFN(CmdBeginQuery);
  GetInstancePFN(CmdEndQuery);
  GetInstancePFN(CmdCopyQueryPoolResults);
  GetInstancePFN(CmdResetQueryPool);
  GetInstancePFN(CmdWriteTimestamp);

  // Extensions /////////////////////////////////////////////////////

  // Debug Utils
  GetInstancePFN(CreateDebugUtilsMessengerEXT);
  GetInstancePFN(DestroyDebugUtilsMessengerEXT);
  GetInstancePFN(SetDebugUtilsObjectNameEXT);
  GetInstancePFN(SetDebugUtilsObjectTagEXT);
  GetInstancePFN(QueueBeginDebugUtilsLabelEXT);
  GetInstancePFN(QueueEndDebugUtilsLabelEXT);
  GetInstancePFN(QueueInsertDebugUtilsLabelEXT);
  GetInstancePFN(CmdBeginDebugUtilsLabelEXT);
  GetInstancePFN(CmdEndDebugUtilsLabelEXT);
  GetInstancePFN(CmdInsertDebugUtilsLabelEXT);

  // Surface
#ifdef VD_OS_WINDOWS
  GetInstancePFN(CreateWin32SurfaceKHR);
  GetInstancePFN(GetPhysicalDeviceWin32PresentationSupportKHR);
#endif
#ifdef VD_OS_LINUX
  GetInstancePFN(CreateXcbSurfaceKHR);
  GetInstancePFN(GetPhysicalDeviceXcbPresentationSupportKHR);
#endif
#ifdef VD_OS_ANDROID
  GetInstancePFN(CreateAndroidSurfaceKHR);
  GetInstancePFN(GetPhysicalDeviceAndroidPresentationSupportKHR);
#endif
  GetInstancePFN(DestroySurfaceKHR);

  GetInstancePFN(GetPhysicalDeviceSurfaceSupportKHR);
  GetInstancePFN(GetPhysicalDeviceSurfaceCapabilitiesKHR);
  GetInstancePFN(GetPhysicalDeviceSurfaceFormatsKHR);
  GetInstancePFN(GetPhysicalDeviceSurfacePresentModesKHR);

  GetInstancePFN(CreateSwapchainKHR);
  GetInstancePFN(DestroySwapchainKHR);
  GetInstancePFN(GetSwapchainImagesKHR);
  GetInstancePFN(AcquireNextImageKHR);
  GetInstancePFN(AcquireNextImage2KHR);
  GetInstancePFN(QueuePresentKHR);
}

void Dispatcher::loadDevice(VkDevice device)
{
  m_source   = Source::Device;
  m_vkDevice = device;

  // GetDevicePFN(GetDeviceProcAddr);

  // Vulkan core ////////////////////////////////////////////////////
  GetDevicePFN(EnumerateInstanceExtensionProperties);
  GetDevicePFN(EnumerateInstanceLayerProperties);
  GetDevicePFN(EnumerateInstanceVersion);
  GetDevicePFN(EnumeratePhysicalDevices);
  GetDevicePFN(CreateInstance);
  GetDevicePFN(DestroyInstance);
  GetDevicePFN(CreateDevice);
  GetDevicePFN(DestroyDevice);
  GetDevicePFN(DeviceWaitIdle);
  GetDevicePFN(GetPhysicalDeviceProperties2);
  GetDevicePFN(GetPhysicalDeviceFeatures2);
  GetDevicePFN(GetPhysicalDeviceQueueFamilyProperties2);
  GetDevicePFN(GetPhysicalDeviceMemoryProperties2);
  GetDevicePFN(GetPhysicalDeviceFormatProperties);
  GetDevicePFN(GetPhysicalDeviceFormatProperties2);
  GetDevicePFN(GetPhysicalDeviceImageFormatProperties);
  GetDevicePFN(GetDeviceQueue);
  GetDevicePFN(QueueSubmit);
  GetDevicePFN(QueueWaitIdle);
  GetDevicePFN(QueueBindSparse);
  GetDevicePFN(CreateBuffer);
  GetDevicePFN(DestroyBuffer);
  GetDevicePFN(GetBufferMemoryRequirements);
  GetDevicePFN(BindBufferMemory);
  GetDevicePFN(CreateImage);
  GetDevicePFN(DestroyImage);
  GetDevicePFN(GetImageMemoryRequirements);
  GetDevicePFN(BindImageMemory);
  GetDevicePFN(CreateBufferView);
  GetDevicePFN(DestroyBufferView);
  GetDevicePFN(CreateImageView);
  GetDevicePFN(DestroyImageView);
  GetDevicePFN(AllocateMemory);
  GetDevicePFN(FreeMemory);
  GetDevicePFN(MapMemory);
  GetDevicePFN(UnmapMemory);
  GetDevicePFN(FlushMappedMemoryRanges);
  GetDevicePFN(CreateFramebuffer);
  GetDevicePFN(DestroyFramebuffer);
  GetDevicePFN(CreateShaderModule);
  GetDevicePFN(DestroyShaderModule);
  GetDevicePFN(CreateCommandPool);
  GetDevicePFN(DestroyCommandPool);
  GetDevicePFN(ResetCommandPool);
  GetDevicePFN(TrimCommandPool);
  GetDevicePFN(AllocateCommandBuffers);
  GetDevicePFN(FreeCommandBuffers);
  GetDevicePFN(BeginCommandBuffer);
  GetDevicePFN(EndCommandBuffer);
  GetDevicePFN(ResetCommandBuffer);
  GetDevicePFN(CreateRenderPass);
  GetDevicePFN(CreateRenderPass2);
  GetDevicePFN(DestroyRenderPass);
  GetDevicePFN(CreateFence);
  GetDevicePFN(DestroyFence);
  GetDevicePFN(ResetFences);
  GetDevicePFN(WaitForFences);
  GetDevicePFN(GetFenceStatus);
  GetDevicePFN(CreateSemaphore);
  GetDevicePFN(DestroySemaphore);
  GetDevicePFN(WaitSemaphores);
  GetDevicePFN(SignalSemaphore);
  GetDevicePFN(GetSemaphoreCounterValue);
  GetDevicePFN(CreateGraphicsPipelines);
  GetDevicePFN(CreateComputePipelines);
  GetDevicePFN(DestroyPipeline);
  GetDevicePFN(CreatePipelineCache);
  GetDevicePFN(DestroyPipelineCache);
  GetDevicePFN(GetPipelineCacheData);
  GetDevicePFN(MergePipelineCaches);
  GetDevicePFN(CreatePipelineLayout);
  GetDevicePFN(DestroyPipelineLayout);
  GetDevicePFN(CreateDescriptorSetLayout);
  GetDevicePFN(DestroyDescriptorSetLayout);
  GetDevicePFN(CreateDescriptorPool);
  GetDevicePFN(DestroyDescriptorPool);
  GetDevicePFN(ResetDescriptorPool);
  GetDevicePFN(AllocateDescriptorSets);
  GetDevicePFN(FreeDescriptorSets);
  GetDevicePFN(UpdateDescriptorSets);
  GetDevicePFN(CreateSampler);
  GetDevicePFN(DestroySampler);

  // Commands ///////////////////////////////////////////////////////
  GetDevicePFN(CmdExecuteCommands);
  GetDevicePFN(CmdBeginRendering);
  GetDevicePFN(CmdEndRendering);
  GetDevicePFN(CmdBeginRenderPass);
  GetDevicePFN(CmdEndRenderPass);
  GetDevicePFN(CmdNextSubpass);
  GetDevicePFN(CmdBeginRenderPass2);
  GetDevicePFN(CmdEndRenderPass2);
  GetDevicePFN(CmdNextSubpass2);
  GetDevicePFN(CmdBindPipeline);
  GetDevicePFN(CmdBindVertexBuffers);
  GetDevicePFN(CmdBindIndexBuffer);
  GetDevicePFN(CmdBindDescriptorSets);
  GetDevicePFN(CmdPushConstants);
  GetDevicePFN(CmdSetBlendConstants);
  GetDevicePFN(CmdSetDepthBias);
  GetDevicePFN(CmdSetDepthBounds);
  GetDevicePFN(CmdSetScissor);
  GetDevicePFN(CmdSetStencilCompareMask);
  GetDevicePFN(CmdSetStencilReference);
  GetDevicePFN(CmdSetStencilWriteMask);
  GetDevicePFN(CmdSetViewport);
  GetDevicePFN(CmdCopyBuffer);
  GetDevicePFN(CmdCopyImage);
  GetDevicePFN(CmdCopyImageToBuffer);
  GetDevicePFN(CmdCopyBufferToImage);
  GetDevicePFN(CmdBlitImage);
  GetDevicePFN(CmdResolveImage);
  GetDevicePFN(CmdUpdateBuffer);
  GetDevicePFN(CmdDraw);
  GetDevicePFN(CmdDrawIndirect);
  GetDevicePFN(CmdDrawIndexed);
  GetDevicePFN(CmdDrawIndexedIndirect);
  GetDevicePFN(CmdDispatch);
  GetDevicePFN(CmdDispatchBase);
  GetDevicePFN(CmdDispatchIndirect);
  GetDevicePFN(CmdClearAttachments);
  GetDevicePFN(CmdClearColorImage);
  GetDevicePFN(CmdClearDepthStencilImage);
  GetDevicePFN(CmdFillBuffer);
  GetDevicePFN(CmdPipelineBarrier);
  GetDevicePFN(CmdSetEvent);
  GetDevicePFN(CmdResetEvent);
  GetDevicePFN(CmdWaitEvents);
  GetDevicePFN(CmdBeginQuery);
  GetDevicePFN(CmdEndQuery);
  GetDevicePFN(CmdCopyQueryPoolResults);
  GetDevicePFN(CmdResetQueryPool);
  GetDevicePFN(CmdWriteTimestamp);

  // Extensions /////////////////////////////////////////////////////

  // Debug Utils
  GetDevicePFN(CreateDebugUtilsMessengerEXT);
  GetDevicePFN(DestroyDebugUtilsMessengerEXT);
  GetDevicePFN(SetDebugUtilsObjectNameEXT);
  GetDevicePFN(SetDebugUtilsObjectTagEXT);
  GetDevicePFN(QueueBeginDebugUtilsLabelEXT);
  GetDevicePFN(QueueEndDebugUtilsLabelEXT);
  GetDevicePFN(QueueInsertDebugUtilsLabelEXT);
  GetDevicePFN(CmdBeginDebugUtilsLabelEXT);
  GetDevicePFN(CmdEndDebugUtilsLabelEXT);
  GetDevicePFN(CmdInsertDebugUtilsLabelEXT);

  // Surface
#ifdef VD_OS_WINDOWS
  GetDevicePFN(CreateWin32SurfaceKHR);
  GetDevicePFN(GetPhysicalDeviceWin32PresentationSupportKHR);
#endif
#ifdef VD_OS_LINUX
  GetDevicePFN(CreateXcbSurfaceKHR);
  GetDevicePFN(GetPhysicalDeviceXcbPresentationSupportKHR);
#endif
#ifdef VD_OS_ANDROID
  GetDevicePFN(CreateAndroidSurfaceKHR);
  GetDevicePFN(GetPhysicalDeviceAndroidPresentationSupportKHR);
#endif
  GetDevicePFN(DestroySurfaceKHR);

  GetDevicePFN(GetPhysicalDeviceSurfaceSupportKHR);
  GetDevicePFN(GetPhysicalDeviceSurfaceCapabilitiesKHR);
  GetDevicePFN(GetPhysicalDeviceSurfaceFormatsKHR);
  GetDevicePFN(GetPhysicalDeviceSurfacePresentModesKHR);

  GetDevicePFN(CreateSwapchainKHR);
  GetDevicePFN(DestroySwapchainKHR);
  GetDevicePFN(GetSwapchainImagesKHR);
  GetDevicePFN(AcquireNextImageKHR);
  GetDevicePFN(AcquireNextImage2KHR);
  GetDevicePFN(QueuePresentKHR);
}

void Dispatcher::clear()
{
  memset(&pfn, 0u, sizeof(pfn));
  m_source = Source::None;
}

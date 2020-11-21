#pragma once

#include "vuldir/api/Common.hpp"

namespace vd {
class Device;

// https://www.khronos.org/registry/vulkan/specs/1.2/html/vkspec.html
class Dispatcher
{
public:
  enum class Source { None, Default, Instance, Device };

public:
  VD_NONMOVABLE(Dispatcher);

  Dispatcher();
  Dispatcher(Device& device);
  ~Dispatcher();

public:
  void Load(Device& device);

  Source GetSource() const { return m_source; }

private:
  void loadDefault();
  void loadInstance(VkInstance instance);
  void loadDevice(VkDevice device);

private:
  void clear();

private:
#ifdef VD_OS_WINDOWS
  static constexpr char m_libraryPath[] = "vulkan-1.dll";
#else
  static constexpr char m_libraryPath[] = "libvulkan.so";
#endif

  Library                      m_library;
  VkInstance                   m_vkInstance;
  VkDevice                     m_vkDevice;
  const VkAllocationCallbacks* m_callbacks;

  Source m_source;
  ///////////////////////////////////////////////////////////////////////
  // Vulkan core
  ///////////////////////////////////////////////////////////////////////
public:
  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkEnumerateInstanceVersion.html
  VkResult EnumerateInstanceVersion(u32* pVersion)
  {
    return pfn.EnumerateInstanceVersion(pVersion);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkEnumeratePhysicalDevices.html
  VkResult EnumeratePhysicalDevices(
    u32* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices)
  {
    return pfn.EnumeratePhysicalDevices(
      m_vkInstance, pPhysicalDeviceCount, pPhysicalDevices);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkEnumerateInstanceLayerProperties.html
  VkResult EnumerateInstanceLayerProperties(
    u32* pPropertyCount, VkLayerProperties* pProperties)
  {
    return pfn.EnumerateInstanceLayerProperties(
      pPropertyCount, pProperties);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkEnumerateInstanceExtensionProperties.html
  VkResult EnumerateInstanceExtensionProperties(
    const char* pLayerName, u32* pPropertyCount,
    VkExtensionProperties* pProperties)
  {
    return pfn.EnumerateInstanceExtensionProperties(
      pLayerName, pPropertyCount, pProperties);
  }

  /// @link ://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateInstance.html
  VkResult CreateInstance(
    const VkInstanceCreateInfo* pCreateInfo, VkInstance* pInstance)
  {
    return pfn.CreateInstance(pCreateInfo, m_callbacks, pInstance);
  }

  /// @link ://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkDestroyInstance.html
  void DestroyInstance(VkInstance instance)
  {
    pfn.DestroyInstance(instance, m_callbacks);
  }

  /// @link ://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateDevice.html
  VkResult CreateDevice(
    VkPhysicalDevice          physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo, VkDevice* pDevice)
  {
    return pfn.CreateDevice(
      physicalDevice, pCreateInfo, m_callbacks, pDevice);
  }

  /// @link ://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkDestroyDevice.html
  void DestroyDevice(VkDevice device)
  {
    pfn.DestroyDevice(device, m_callbacks);
  }

  /// @link ://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkDeviceWaitIdle.html
  void DeviceWaitIdle() { pfn.DeviceWaitIdle(m_vkDevice); }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkGetPhysicalDeviceProperties2.html
  void GetPhysicalDeviceProperties2(
    VkPhysicalDevice             physicalDevice,
    VkPhysicalDeviceProperties2* pProperties)
  {
    pfn.GetPhysicalDeviceProperties2(physicalDevice, pProperties);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkGetPhysicalDeviceFeatures2.html
  void GetPhysicalDeviceFeatures2(
    VkPhysicalDevice           physicalDevice,
    VkPhysicalDeviceFeatures2* pFeatures)
  {
    pfn.GetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkGetPhysicalDeviceQueueFamilyProperties2.html
  void GetPhysicalDeviceQueueFamilyProperties2(
    VkPhysicalDevice physicalDevice, u32* pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2* pQueueFamilyProperties)
  {
    pfn.GetPhysicalDeviceQueueFamilyProperties2(
      physicalDevice, pQueueFamilyPropertyCount,
      pQueueFamilyProperties);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkGetPhysicalDeviceMemoryProperties2.html
  void GetPhysicalDeviceMemoryProperties2(
    VkPhysicalDevice                   vkPhysicalDevice,
    VkPhysicalDeviceMemoryProperties2* pMemoryProperties)
  {
    return pfn.GetPhysicalDeviceMemoryProperties2(
      vkPhysicalDevice, pMemoryProperties);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/GetPhysicalDeviceFormatProperties.html
  void GetPhysicalDeviceFormatProperties(
    VkPhysicalDevice vkPhysicalDevice, VkFormat format,
    VkFormatProperties* pFormatProperties)
  {
    return pfn.GetPhysicalDeviceFormatProperties(
      vkPhysicalDevice, format, pFormatProperties);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/GetPhysicalDeviceFormatProperties2.html
  void GetPhysicalDeviceFormatProperties2(
    VkPhysicalDevice vkPhysicalDevice, VkFormat format,
    VkFormatProperties2* pFormatProperties)
  {
    return pfn.GetPhysicalDeviceFormatProperties2(
      vkPhysicalDevice, format, pFormatProperties);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/GetPhysicalDeviceImageFormatProperties.html
  VkResult GetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice vkPhysicalDevice, VkFormat format,
    VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage,
    VkImageCreateFlags       flags,
    VkImageFormatProperties* pImageFormatProperties)
  {
    return pfn.GetPhysicalDeviceImageFormatProperties(
      vkPhysicalDevice, format, type, tiling, usage, flags,
      pImageFormatProperties);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkGetDeviceQueue.html
  void
  GetDeviceQueue(u32 queueFamilyIndex, u32 queueIndex, VkQueue* pQueue)
  {
    return pfn.GetDeviceQueue(
      m_vkDevice, queueFamilyIndex, queueIndex, pQueue);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkQueueSubmit.html
  VkResult QueueSubmit(
    VkQueue queue, u32 submitCount, const VkSubmitInfo* pSubmits,
    VkFence fence)
  {
    return pfn.QueueSubmit(queue, submitCount, pSubmits, fence);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkQueueWaitIdle.html
  VkResult QueueWaitIdle(VkQueue queue)
  {
    return pfn.QueueWaitIdle(queue);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkQueueBindSparse.html
  VkResult QueueBindSparse(
    VkQueue queue, u32 bindInfoCount, const VkBindSparseInfo* pBindInfo,
    VkFence fence)
  {
    return pfn.QueueBindSparse(queue, bindInfoCount, pBindInfo, fence);
  }

  /// @link ://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateBuffer.html
  VkResult
  CreateBuffer(const VkBufferCreateInfo* pCreateInfo, VkBuffer* pBuffer)
  {
    return pfn.CreateBuffer(
      m_vkDevice, pCreateInfo, m_callbacks, pBuffer);
  }

  /// @link ://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkDestroyBuffer.html
  void DestroyBuffer(VkBuffer buffer)
  {
    pfn.DestroyBuffer(m_vkDevice, buffer, m_callbacks);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkGetBufferMemoryRequirements.html
  void GetBufferMemoryRequirements(
    const VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements)
  {
    return pfn.GetBufferMemoryRequirements(
      m_vkDevice, buffer, pMemoryRequirements);
  }

  /// @link ://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkBindBufferMemory.html
  VkResult BindBufferMemory(
    const VkBuffer buffer, const VkDeviceMemory memory,
    const VkDeviceSize memoryOffset)
  {
    return pfn.BindBufferMemory(
      m_vkDevice, buffer, memory, memoryOffset);
  }

  /// @link ://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateImage.html
  VkResult
  CreateImage(const VkImageCreateInfo* pCreateInfo, VkImage* pImage)
  {
    return pfn.CreateImage(
      m_vkDevice, pCreateInfo, m_callbacks, pImage);
  }

  /// @link ://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkDestroyImage.html
  void DestroyImage(VkImage image)
  {
    pfn.DestroyImage(m_vkDevice, image, m_callbacks);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkGetImageMemoryRequirements.html
  void GetImageMemoryRequirements(
    const VkImage image, VkMemoryRequirements* pMemoryRequirements)
  {
    return pfn.GetImageMemoryRequirements(
      m_vkDevice, image, pMemoryRequirements);
  }

  /// @link ://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkBindImageMemory.html
  VkResult BindImageMemory(
    const VkImage image, const VkDeviceMemory memory,
    const VkDeviceSize memoryOffset)
  {
    return pfn.BindImageMemory(m_vkDevice, image, memory, memoryOffset);
  }

  /// @link ://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateBufferView.html
  VkResult CreateBufferView(
    const VkBufferViewCreateInfo* pCreateInfo, VkBufferView* pView)
  {
    return pfn.CreateBufferView(
      m_vkDevice, pCreateInfo, m_callbacks, pView);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkDestroyBufferView.html
  void DestroyBufferView(VkBufferView imageView)
  {
    return pfn.DestroyBufferView(m_vkDevice, imageView, m_callbacks);
  }

  /// @link ://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateImageView.html
  VkResult CreateImageView(
    const VkImageViewCreateInfo* pCreateInfo, VkImageView* pView)
  {
    return pfn.CreateImageView(
      m_vkDevice, pCreateInfo, m_callbacks, pView);
  }

  /// @link ://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkDestroyImageView.html
  void DestroyImageView(VkImageView imageView)
  {
    return pfn.DestroyImageView(m_vkDevice, imageView, m_callbacks);
  }

  /// @link ://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkAllocateMemory.html
  VkResult AllocateMemory(
    const VkMemoryAllocateInfo* pAllocateInfo, VkDeviceMemory* pMemory)
  {
    return pfn.AllocateMemory(
      m_vkDevice, pAllocateInfo, m_callbacks, pMemory);
  }

  /// @link ://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkFreeMemory.html
  void FreeMemory(VkDeviceMemory memory)
  {
    pfn.FreeMemory(m_vkDevice, memory, m_callbacks);
  }

  /// @link ://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkMapMemory.html
  VkResult MapMemory(
    VkDeviceMemory memory, const VkDeviceSize offset,
    const VkDeviceSize size, const VkMemoryMapFlags flags,
    void** ppData)
  {
    return pfn.MapMemory(
      m_vkDevice, memory, offset, size, flags, ppData);
  }

  /// @link ://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkUnmapMemory.html
  void UnmapMemory(VkDeviceMemory memory)
  {
    return pfn.UnmapMemory(m_vkDevice, memory);
  }

  /// @link ://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkFlushMappedMemoryRanges.html
  VkResult FlushMappedMemoryRanges(
    const u32                  memoryRangeCount,
    const VkMappedMemoryRange* pMemoryRanges)
  {
    return pfn.FlushMappedMemoryRanges(
      m_vkDevice, memoryRangeCount, pMemoryRanges);
  }

  VkResult CreateFramebuffer(
    const VkFramebufferCreateInfo* pCreateInfo,
    VkFramebuffer*                 pFramebuffer)
  {
    return pfn.CreateFramebuffer(
      m_vkDevice, pCreateInfo, m_callbacks, pFramebuffer);
  }

  void DestroyFramebuffer(VkFramebuffer framebuffer)
  {
    return pfn.DestroyFramebuffer(m_vkDevice, framebuffer, m_callbacks);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateShaderModule.html
  VkResult CreateShaderModule(
    const VkShaderModuleCreateInfo* pCreateInfo,
    VkShaderModule*                 pShaderModule)
  {
    return pfn.CreateShaderModule(
      m_vkDevice, pCreateInfo, m_callbacks, pShaderModule);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkDestroyShaderModule.html
  void DestroyShaderModule(VkShaderModule shaderModule)
  {
    return pfn.DestroyShaderModule(
      m_vkDevice, shaderModule, m_callbacks);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateCommandPool.html
  VkResult CreateCommandPool(
    const VkCommandPoolCreateInfo* pCreateInfo,
    VkCommandPool*                 pCommandPool)
  {
    return pfn.CreateCommandPool(
      m_vkDevice, pCreateInfo, m_callbacks, pCommandPool);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkDestroyCommandPool.html
  void DestroyCommandPool(const VkCommandPool commandPool)
  {
    return pfn.DestroyCommandPool(m_vkDevice, commandPool, m_callbacks);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkResetCommandPool.html
  void ResetCommandPool(
    const VkCommandPool commandPool, VkCommandPoolResetFlags flags)
  {
    pfn.ResetCommandPool(m_vkDevice, commandPool, flags);
  }

  /// @link ://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkTrimCommandPool.html
  void TrimCommandPool(
    const VkCommandPool commandPool, const VkCommandPoolTrimFlags flags)
  {
    return pfn.TrimCommandPool(m_vkDevice, commandPool, flags);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkAllocateCommandBuffers.html
  VkResult AllocateCommandBuffers(
    const VkCommandBufferAllocateInfo* pAllocateInfo,
    VkCommandBuffer*                   pCommandBuffers)
  {
    return pfn.AllocateCommandBuffers(
      m_vkDevice, pAllocateInfo, pCommandBuffers);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkFreeCommandBuffers.html
  void FreeCommandBuffers(
    const VkCommandPool commandPool, const u32 commandBufferCount,
    const VkCommandBuffer* pCommandBuffers)
  {
    return pfn.FreeCommandBuffers(
      m_vkDevice, commandPool, commandBufferCount, pCommandBuffers);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkBeginCommandBuffer.html
  VkResult BeginCommandBuffer(
    const VkCommandBuffer           commandBuffer,
    const VkCommandBufferBeginInfo* pBeginInfo)
  {
    return pfn.BeginCommandBuffer(commandBuffer, pBeginInfo);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkEndCommandBuffer.html
  VkResult EndCommandBuffer(const VkCommandBuffer commandBuffer)
  {
    return pfn.EndCommandBuffer(commandBuffer);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkResetCommandBuffer.html
  VkResult ResetCommandBuffer(
    const VkCommandBuffer     commandBuffer,
    VkCommandBufferResetFlags flags)
  {
    return pfn.ResetCommandBuffer(commandBuffer, flags);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateRenderPass.html
  VkResult CreateRenderPass(
    const VkRenderPassCreateInfo* pCreateInfo,
    VkRenderPass*                 pRenderPass)
  {
    return pfn.CreateRenderPass(
      m_vkDevice, pCreateInfo, m_callbacks, pRenderPass);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateRenderPass2.html
  VkResult CreateRenderPass2(
    const VkRenderPassCreateInfo2* pCreateInfo,
    VkRenderPass*                  pRenderPass)
  {
    return pfn.CreateRenderPass2(
      m_vkDevice, pCreateInfo, m_callbacks, pRenderPass);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkDestroyRenderPass.html
  void DestroyRenderPass(VkRenderPass renderPass)
  {
    return pfn.DestroyRenderPass(m_vkDevice, renderPass, m_callbacks);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateFence.html
  VkResult
  CreateFence(const VkFenceCreateInfo* pCreateInfo, VkFence* pFence)
  {
    return pfn.CreateFence(
      m_vkDevice, pCreateInfo, m_callbacks, pFence);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkDestroyFence.html
  void DestroyFence(VkFence fence)
  {
    pfn.DestroyFence(m_vkDevice, fence, m_callbacks);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkResetFences.html
  VkResult ResetFences(const u32 fenceCount, const VkFence* pFences)
  {
    return pfn.ResetFences(m_vkDevice, fenceCount, pFences);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkWaitForFences.html
  VkResult WaitForFences(
    const u32 fenceCount, const VkFence* pFences, const bool waitAll,
    const u64 timeout)
  {
    return pfn.WaitForFences(
      m_vkDevice, fenceCount, pFences, waitAll, timeout);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkGetFenceStatus.html
  VkResult GetFenceStatus(VkFence fence)
  {
    return pfn.GetFenceStatus(m_vkDevice, fence);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateSemaphore.html
  VkResult CreateSemaphore(
    const VkSemaphoreCreateInfo* pCreateInfo, VkSemaphore* pSemaphore)
  {
    return pfn.CreateSemaphore(
      m_vkDevice, pCreateInfo, m_callbacks, pSemaphore);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkDestroySemaphore.html
  void DestroySemaphore(VkSemaphore semaphore)
  {
    pfn.DestroySemaphore(m_vkDevice, semaphore, m_callbacks);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkWaitSemaphores.html
  VkResult
  WaitSemaphores(const VkSemaphoreWaitInfo* pWaitInfo, uint64_t timeout)
  {
    return pfn.WaitSemaphores(m_vkDevice, pWaitInfo, timeout);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkSignalSemaphore.html
  VkResult SignalSemaphore(const VkSemaphoreSignalInfo* pSignalInfo)
  {
    return pfn.SignalSemaphore(m_vkDevice, pSignalInfo);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkGetSemaphoreCounterValue.html
  VkResult
  GetSemaphoreCounterValue(VkSemaphore semaphore, uint64_t* pValue)
  {
    return pfn.GetSemaphoreCounterValue(m_vkDevice, semaphore, pValue);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateGraphicsPipelines.html
  VkResult CreateGraphicsPipelines(
    VkPipelineCache pipelineCache, uint32_t createInfoCount,
    const VkGraphicsPipelineCreateInfo* pCreateInfos,
    VkPipeline*                         pPipelines)
  {
    return pfn.CreateGraphicsPipelines(
      m_vkDevice, pipelineCache, createInfoCount, pCreateInfos,
      m_callbacks, pPipelines);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateComputePipelines.html
  VkResult CreateComputePipelines(
    VkPipelineCache pipelineCache, uint32_t createInfoCount,
    const VkComputePipelineCreateInfo* pCreateInfos,
    VkPipeline*                        pPipelines)
  {
    return pfn.CreateComputePipelines(
      m_vkDevice, pipelineCache, createInfoCount, pCreateInfos,
      m_callbacks, pPipelines);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkDestroyPipeline.html
  void DestroyPipeline(VkPipeline pipeline)
  {
    pfn.DestroyPipeline(m_vkDevice, pipeline, m_callbacks);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreatePipelineCache.html
  VkResult CreatePipelineCache(
    const VkPipelineCacheCreateInfo* pCreateInfo,
    VkPipelineCache*                 pPipelineCache)
  {
    return pfn.CreatePipelineCache(
      m_vkDevice, pCreateInfo, m_callbacks, pPipelineCache);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkDestroyPipelineCache.html
  void DestroyPipelineCache(VkPipelineCache pipelineCache)
  {
    pfn.DestroyPipelineCache(m_vkDevice, pipelineCache, m_callbacks);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkGetPipelineCacheData.html
  VkResult GetPipelineCacheData(
    VkPipelineCache pipelineCache, size_t* pDataSize, void* pData)
  {
    return pfn.GetPipelineCacheData(
      m_vkDevice, pipelineCache, pDataSize, pData);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkMergePipelineCaches.html
  VkResult MergePipelineCaches(
    VkPipelineCache dstCache, uint32_t srcCacheCount,
    const VkPipelineCache* pSrcCaches)
  {
    return pfn.MergePipelineCaches(
      m_vkDevice, dstCache, srcCacheCount, pSrcCaches);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreatePipelineLayout.html
  VkResult CreatePipelineLayout(
    const VkPipelineLayoutCreateInfo* pCreateInfo,
    VkPipelineLayout*                 pPipelineLayout)
  {
    return pfn.CreatePipelineLayout(
      m_vkDevice, pCreateInfo, m_callbacks, pPipelineLayout);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkDestroyPipelineLayout.html
  void DestroyPipelineLayout(VkPipelineLayout pipelineLayout)
  {
    pfn.DestroyPipelineLayout(m_vkDevice, pipelineLayout, m_callbacks);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateDescriptorSetLayout.html
  VkResult CreateDescriptorSetLayout(
    const VkDescriptorSetLayoutCreateInfo* pCreateInfo,
    VkDescriptorSetLayout*                 pSetLayout)
  {
    return pfn.CreateDescriptorSetLayout(
      m_vkDevice, pCreateInfo, m_callbacks, pSetLayout);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkDestroyDescriptorSetLayout.html
  void
  DestroyDescriptorSetLayout(VkDescriptorSetLayout descriptorSetLayout)
  {
    pfn.DestroyDescriptorSetLayout(
      m_vkDevice, descriptorSetLayout, m_callbacks);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateDescriptorPool.html
  VkResult CreateDescriptorPool(
    const VkDescriptorPoolCreateInfo* pCreateInfo,
    VkDescriptorPool*                 pDescriptorPool)
  {
    return pfn.CreateDescriptorPool(
      m_vkDevice, pCreateInfo, m_callbacks, pDescriptorPool);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkDestroyDescriptorPool.html
  void DestroyDescriptorPool(VkDescriptorPool descriptorPool)
  {
    pfn.DestroyDescriptorPool(m_vkDevice, descriptorPool, m_callbacks);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkResetDescriptorPool.html
  VkResult ResetDescriptorPool(
    VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags)
  {
    return pfn.ResetDescriptorPool(m_vkDevice, descriptorPool, flags);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkAllocateDescriptorSets.html
  VkResult AllocateDescriptorSets(
    const VkDescriptorSetAllocateInfo* pAllocateInfo,
    VkDescriptorSet*                   pDescriptorSets)
  {
    return pfn.AllocateDescriptorSets(
      m_vkDevice, pAllocateInfo, pDescriptorSets);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkFreeDescriptorSets.html
  void FreeDescriptorSets(
    VkDescriptorPool descriptorPool, uint32_t descriptorSetCount,
    const VkDescriptorSet* pDescriptorSets)
  {
    pfn.FreeDescriptorSets(
      m_vkDevice, descriptorPool, descriptorSetCount, pDescriptorSets);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkUpdateDescriptorSets.html
  void UpdateDescriptorSets(
    uint32_t                    descriptorWriteCount,
    const VkWriteDescriptorSet* pDescriptorWrites,
    uint32_t                    descriptorCopyCount,
    const VkCopyDescriptorSet*  pDescriptorCopies)
  {
    pfn.UpdateDescriptorSets(
      m_vkDevice, descriptorWriteCount, pDescriptorWrites,
      descriptorCopyCount, pDescriptorCopies);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateSampler.html
  VkResult CreateSampler(
    const VkSamplerCreateInfo* pCreateInfo, VkSampler* pSampler)
  {
    return pfn.CreateSampler(
      m_vkDevice, pCreateInfo, m_callbacks, pSampler);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkDestroySampler.html
  void DestroySampler(VkSampler sampler)
  {
    pfn.DestroySampler(m_vkDevice, sampler, m_callbacks);
  }

  // Commands ///////////////////////////////////////////////////////

  /// Execute a secondary command buffer from a primary command buffer.
  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdExecuteCommands.html
  /// @param[in] commandBuffer      is a handle to a primary command
  /// buffer that the secondary command buffers are executed in.
  /// @param[in] commandBufferCount is the length of the pCommandBuffers
  /// array.
  /// @param[in] pCommandBuffers    is an array of secondary command
  /// buffer handles, which are recorded to execute in the primary
  /// command buffer in the order they are listed in the array.
  void CmdExecuteCommands(
    const VkCommandBuffer commandBuffer, const u32 commandBufferCount,
    const VkCommandBuffer* pCommandBuffers)
  {
    return pfn.CmdExecuteCommands(
      commandBuffer, commandBufferCount, pCommandBuffers);
  }

  /// Begin a dynamic render pass instance.
  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdBeginRendering.html
  void CmdBeginRendering(
    VkCommandBuffer        commandBuffer,
    const VkRenderingInfo* pRenderingInfo)
  {
    pfn.CmdBeginRendering(commandBuffer, pRenderingInfo);
  }

  /// End a dynamic render pass instance.
  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdEndRendering.html
  void CmdEndRendering(VkCommandBuffer commandBuffer)
  {
    pfn.CmdEndRendering(commandBuffer);
  }

  /// Begin a new render pass.
  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdBeginRenderPass.html
  void CmdBeginRenderPass(
    const VkCommandBuffer        commandBuffer,
    const VkRenderPassBeginInfo* pRenderPassBegin,
    VkSubpassContents            contents)
  {
    return pfn.CmdBeginRenderPass(
      commandBuffer, pRenderPassBegin, contents);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdEndRenderPass.html
  void CmdEndRenderPass(const VkCommandBuffer commandBuffer)
  {
    return pfn.CmdEndRenderPass(commandBuffer);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdNextSubpass.html
  void CmdNextSubpass(
    const VkCommandBuffer commandBuffer, VkSubpassContents contents)
  {
    return pfn.CmdNextSubpass(commandBuffer, contents);
  }

  /// Begin a new render pass.
  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdBeginRenderPass2.html
  void CmdBeginRenderPass2(
    const VkCommandBuffer        commandBuffer,
    const VkRenderPassBeginInfo* pRenderPassBegin,
    const VkSubpassBeginInfo*    pSubpassBeginInfo)
  {
    return pfn.CmdBeginRenderPass2(
      commandBuffer, pRenderPassBegin, pSubpassBeginInfo);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdEndRenderPass2.html
  void CmdEndRenderPass2(
    const VkCommandBuffer   commandBuffer,
    const VkSubpassEndInfo* pSubpassEndInfo)
  {
    return pfn.CmdEndRenderPass2(commandBuffer, pSubpassEndInfo);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdNextSubpass2.html
  void CmdNextSubpass2(
    const VkCommandBuffer     commandBuffer,
    const VkSubpassBeginInfo* pSubpassBeginInfo,
    const VkSubpassEndInfo*   pSubpassEndInfo)
  {
    return pfn.CmdNextSubpass2(
      commandBuffer, pSubpassBeginInfo, pSubpassEndInfo);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdBindPipeline.html
  void CmdBindPipeline(
    const VkCommandBuffer commandBuffer,
    VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline)
  {
    return pfn.CmdBindPipeline(
      commandBuffer, pipelineBindPoint, pipeline);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdBindVertexBuffers.html
  void CmdBindVertexBuffers(
    const VkCommandBuffer commandBuffer, const u32 firstBinding,
    const u32 bindingCount, const VkBuffer* pBuffers,
    const VkDeviceSize* pOffsets)
  {
    return pfn.CmdBindVertexBuffers(
      commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdBindIndexBuffer.html
  void CmdBindIndexBuffer(
    const VkCommandBuffer commandBuffer, VkBuffer buffer,
    VkDeviceSize offset, VkIndexType indexType)
  {
    return pfn.CmdBindIndexBuffer(
      commandBuffer, buffer, offset, indexType);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdBindDescriptorSets.html
  void CmdBindDescriptorSets(
    const VkCommandBuffer commandBuffer,
    VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout,
    const u32 firstSet, const u32 descriptorSetCount,
    const VkDescriptorSet* pDescriptorSets,
    const u32 dynamicOffsetCount, const u32* pDynamicOffsets)
  {
    return pfn.CmdBindDescriptorSets(
      commandBuffer, pipelineBindPoint, layout, firstSet,
      descriptorSetCount, pDescriptorSets, dynamicOffsetCount,
      pDynamicOffsets);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdPushConstants.html
  void CmdPushConstants(
    const VkCommandBuffer commandBuffer, VkPipelineLayout layout,
    VkShaderStageFlags stageFlags, const u32 offset, const u32 size,
    const void* pValues)
  {
    return pfn.CmdPushConstants(
      commandBuffer, layout, stageFlags, offset, size, pValues);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdSetBlendConstants.html
  void CmdSetBlendConstants(
    const VkCommandBuffer commandBuffer, const f32 blendConstants[4])
  {
    return pfn.CmdSetBlendConstants(commandBuffer, blendConstants);
  }

  void CmdSetDepthBias(
    const VkCommandBuffer commandBuffer, f32 depthBiasConstantFactor,
    f32 depthBiasClamp, f32 depthBiasSlopeFactor)
  {
    return pfn.CmdSetDepthBias(
      commandBuffer, depthBiasConstantFactor, depthBiasClamp,
      depthBiasSlopeFactor);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdSetDepthBounds.html
  void CmdSetDepthBounds(
    const VkCommandBuffer commandBuffer, f32 minDepthBounds,
    f32 maxDepthBounds)
  {
    return pfn.CmdSetDepthBounds(
      commandBuffer, minDepthBounds, maxDepthBounds);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdSetScissor.html
  void CmdSetScissor(
    const VkCommandBuffer commandBuffer, const u32 firstScissor,
    const u32 scissorCount, const VkRect2D* pScissors)
  {
    return pfn.CmdSetScissor(
      commandBuffer, firstScissor, scissorCount, pScissors);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdSetStencilCompareMask.html
  void CmdSetStencilCompareMask(
    const VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
    const u32 compareMask)
  {
    return pfn.CmdSetStencilCompareMask(
      commandBuffer, faceMask, compareMask);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdSetStencilReference.html
  void CmdSetStencilReference(
    const VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
    const u32 reference)
  {
    return pfn.CmdSetStencilReference(
      commandBuffer, faceMask, reference);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdSetStencilWriteMask.html
  void CmdSetStencilWriteMask(
    const VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
    const u32 writeMask)
  {
    return pfn.CmdSetStencilWriteMask(
      commandBuffer, faceMask, writeMask);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdSetViewport.html
  void CmdSetViewport(
    const VkCommandBuffer commandBuffer, const u32 firstViewport,
    const u32 viewportCount, const VkViewport* pViewports)
  {
    return pfn.CmdSetViewport(
      commandBuffer, firstViewport, viewportCount, pViewports);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdCopyBuffer.html
  void CmdCopyBuffer(
    const VkCommandBuffer commandBuffer, VkBuffer srcBuffer,
    VkBuffer dstBuffer, const u32 regionCount,
    const VkBufferCopy* pRegions)
  {
    return pfn.CmdCopyBuffer(
      commandBuffer, srcBuffer, dstBuffer, regionCount, pRegions);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdCopyImage.html
  void CmdCopyImage(
    const VkCommandBuffer commandBuffer, VkImage srcImage,
    VkImageLayout srcImageLayout, VkImage dstImage,
    VkImageLayout dstImageLayout, const u32 regionCount,
    const VkImageCopy* pRegions)
  {
    return pfn.CmdCopyImage(
      commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout,
      regionCount, pRegions);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdCopyImageToBuffer.html
  void CmdCopyImageToBuffer(
    const VkCommandBuffer commandBuffer, VkImage srcImage,
    VkImageLayout srcImageLayout, VkBuffer dstBuffer,
    const u32 regionCount, const VkBufferImageCopy* pRegions)
  {
    return pfn.CmdCopyImageToBuffer(
      commandBuffer, srcImage, srcImageLayout, dstBuffer, regionCount,
      pRegions);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdCopyBufferToImage.html
  void CmdCopyBufferToImage(
    const VkCommandBuffer commandBuffer, VkBuffer srcBuffer,
    VkImage dstImage, VkImageLayout dstImageLayout,
    const u32 regionCount, const VkBufferImageCopy* pRegions)
  {
    return pfn.CmdCopyBufferToImage(
      commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount,
      pRegions);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdBlitImage.html
  void CmdBlitImage(
    const VkCommandBuffer commandBuffer, VkImage srcImage,
    VkImageLayout srcImageLayout, VkImage dstImage,
    VkImageLayout dstImageLayout, uint32_t regionCount,
    const VkImageBlit* pRegions, VkFilter filter)
  {
    return pfn.CmdBlitImage(
      commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout,
      regionCount, pRegions, filter);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdResolveImage.html
  void CmdResolveImage(
    const VkCommandBuffer commandBuffer, VkImage srcImage,
    VkImageLayout srcImageLayout, VkImage dstImage,
    VkImageLayout dstImageLayout, uint32_t regionCount,
    const VkImageResolve* pRegions)
  {
    return pfn.CmdResolveImage(
      commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout,
      regionCount, pRegions);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdUpdateBuffer.html
  void CmdUpdateBuffer(
    const VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
    VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* pData)
  {
    return pfn.CmdUpdateBuffer(
      commandBuffer, dstBuffer, dstOffset, dataSize, pData);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdDraw.html
  void CmdDraw(
    const VkCommandBuffer commandBuffer, uint32_t vertexCount,
    uint32_t instanceCount, uint32_t firstVertex,
    uint32_t firstInstance)
  {
    return pfn.CmdDraw(
      commandBuffer, vertexCount, instanceCount, firstVertex,
      firstInstance);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdDrawIndirect.html
  void CmdDrawIndirect(
    const VkCommandBuffer commandBuffer, VkBuffer buffer,
    VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
  {
    return pfn.CmdDrawIndirect(
      commandBuffer, buffer, offset, drawCount, stride);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdDrawIndexed.html
  void CmdDrawIndexed(
    const VkCommandBuffer commandBuffer, uint32_t indexCount,
    uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset,
    uint32_t firstInstance)
  {
    return pfn.CmdDrawIndexed(
      commandBuffer, indexCount, instanceCount, firstIndex,
      vertexOffset, firstInstance);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdDrawIndexedIndirect.html
  void CmdDrawIndexedIndirect(
    const VkCommandBuffer commandBuffer, VkBuffer buffer,
    VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
  {
    return pfn.CmdDrawIndexedIndirect(
      commandBuffer, buffer, offset, drawCount, stride);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdDispatch.html
  void CmdDispatch(
    const VkCommandBuffer commandBuffer, uint32_t groupCountX,
    uint32_t groupCountY, uint32_t groupCountZ)
  {
    return pfn.CmdDispatch(
      commandBuffer, groupCountX, groupCountY, groupCountZ);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdDispatchBase.html
  void CmdDispatchBase(
    const VkCommandBuffer commandBuffer, const u32 baseGroupX,
    const u32 baseGroupY, const u32 baseGroupZ, const u32 groupCountX,
    const u32 groupCountY, const u32 groupCountZ)
  {
    return pfn.CmdDispatchBase(
      commandBuffer, baseGroupX, baseGroupY, baseGroupZ, groupCountX,
      groupCountY, groupCountZ);
  }
  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdDispatchIndirect.html
  void CmdDispatchIndirect(
    const VkCommandBuffer commandBuffer, VkBuffer buffer,
    VkDeviceSize offset)
  {
    return pfn.CmdDispatchIndirect(commandBuffer, buffer, offset);
  }
  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdClearAttachments.html
  void CmdClearAttachments(
    const VkCommandBuffer commandBuffer, const u32 attachmentCount,
    const VkClearAttachment* pAttachments, const u32 rectCount,
    const VkClearRect* pRects)
  {
    return pfn.CmdClearAttachments(
      commandBuffer, attachmentCount, pAttachments, rectCount, pRects);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdClearColorImage.html
  void CmdClearColorImage(
    const VkCommandBuffer commandBuffer, VkImage image,
    VkImageLayout imageLayout, const VkClearColorValue* pColor,
    const u32 rangeCount, const VkImageSubresourceRange* pRanges)
  {
    return pfn.CmdClearColorImage(
      commandBuffer, image, imageLayout, pColor, rangeCount, pRanges);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdClearDepthStencilImage.html
  void CmdClearDepthStencilImage(
    const VkCommandBuffer commandBuffer, VkImage image,
    VkImageLayout                   imageLayout,
    const VkClearDepthStencilValue* pDepthStencil, const u32 rangeCount,
    const VkImageSubresourceRange* pRanges)
  {
    return pfn.CmdClearDepthStencilImage(
      commandBuffer, image, imageLayout, pDepthStencil, rangeCount,
      pRanges);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdFillBuffer.html
  void CmdFillBuffer(
    const VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
    VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data)
  {
    return pfn.CmdFillBuffer(
      commandBuffer, dstBuffer, dstOffset, size, data);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdPipelineBarrier.html
  void CmdPipelineBarrier(
    const VkCommandBuffer commandBuffer,
    VkPipelineStageFlags  srcStageMask,
    VkPipelineStageFlags  dstStageMask,
    VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount,
    const VkMemoryBarrier*       pMemoryBarriers,
    uint32_t                     bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier* pBufferMemoryBarriers,
    uint32_t                     imageMemoryBarrierCount,
    const VkImageMemoryBarrier*  pImageMemoryBarriers)
  {
    return pfn.CmdPipelineBarrier(
      commandBuffer, srcStageMask, dstStageMask, dependencyFlags,
      memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount,
      pBufferMemoryBarriers, imageMemoryBarrierCount,
      pImageMemoryBarriers);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdSetEvent.html
  void CmdSetEvent(
    const VkCommandBuffer commandBuffer, VkEvent event,
    VkPipelineStageFlags stageMask)
  {
    return pfn.CmdSetEvent(commandBuffer, event, stageMask);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdResetEvent.html
  void CmdResetEvent(
    const VkCommandBuffer commandBuffer, VkEvent event,
    VkPipelineStageFlags stageMask)
  {
    return pfn.CmdResetEvent(commandBuffer, event, stageMask);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdWaitEvents.html
  void CmdWaitEvents(
    const VkCommandBuffer commandBuffer, uint32_t eventCount,
    const VkEvent* pEvents, VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount,
    const VkMemoryBarrier*       pMemoryBarriers,
    uint32_t                     bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier* pBufferMemoryBarriers,
    uint32_t                     imageMemoryBarrierCount,
    const VkImageMemoryBarrier*  pImageMemoryBarriers)
  {
    return pfn.CmdWaitEvents(
      commandBuffer, eventCount, pEvents, srcStageMask, dstStageMask,
      memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount,
      pBufferMemoryBarriers, imageMemoryBarrierCount,
      pImageMemoryBarriers);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdBeginQuery.html
  void CmdBeginQuery(
    const VkCommandBuffer commandBuffer, VkQueryPool queryPool,
    uint32_t query, VkQueryControlFlags flags)
  {
    return pfn.CmdBeginQuery(commandBuffer, queryPool, query, flags);
  }
  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdEndQuery.html
  void CmdEndQuery(
    const VkCommandBuffer commandBuffer, VkQueryPool queryPool,
    uint32_t query)
  {
    return pfn.CmdEndQuery(commandBuffer, queryPool, query);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdCopyQueryPoolResults.html
  void CmdCopyQueryPoolResults(
    const VkCommandBuffer commandBuffer, VkQueryPool queryPool,
    uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer,
    VkDeviceSize dstOffset, VkDeviceSize stride,
    VkQueryResultFlags flags)
  {
    return pfn.CmdCopyQueryPoolResults(
      commandBuffer, queryPool, firstQuery, queryCount, dstBuffer,
      dstOffset, stride, flags);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdResetQueryPool.html
  void CmdResetQueryPool(
    const VkCommandBuffer commandBuffer, VkQueryPool queryPool,
    uint32_t firstQuery, uint32_t queryCount)
  {
    return pfn.CmdResetQueryPool(
      commandBuffer, queryPool, firstQuery, queryCount);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdWriteTimestamp.html
  void CmdWriteTimestamp(
    const VkCommandBuffer   commandBuffer,
    VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool,
    uint32_t query)
  {
    return pfn.CmdWriteTimestamp(
      commandBuffer, pipelineStage, queryPool, query);
  }

  ///////////////////////////////////////////////////////////////////////
  // Debug Utils
  ///////////////////////////////////////////////////////////////////////
public:
  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateDebugUtilsMessengerEXT.html
  VkResult CreateDebugUtilsMessengerEXT(
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    VkDebugUtilsMessengerEXT*                 pMessenger)
  {
    return pfn.CreateDebugUtilsMessengerEXT(
      m_vkInstance, pCreateInfo, m_callbacks, pMessenger);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkDestroyDebugUtilsMessengerEXT.html
  void DestroyDebugUtilsMessengerEXT(VkDebugUtilsMessengerEXT messenger)
  {
    pfn.DestroyDebugUtilsMessengerEXT(
      m_vkInstance, messenger, m_callbacks);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkSubmitDebugUtilsMessageEXT.html
  void SubmitDebugUtilsMessageEXT(
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData)
  {
    pfn.SubmitDebugUtilsMessageEXT(
      m_vkInstance, messageSeverity, messageTypes, pCallbackData);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkSetDebugUtilsObjectNameEXT.html
  VkResult SetDebugUtilsObjectNameEXT(
    const VkDebugUtilsObjectNameInfoEXT* pNameInfo)
  {
    return pfn.SetDebugUtilsObjectNameEXT(m_vkDevice, pNameInfo);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkSetDebugUtilsObjectTagEXT.html
  VkResult SetDebugUtilsObjectTagEXT(
    const VkDebugUtilsObjectTagInfoEXT* pTagInfo)
  {
    return pfn.SetDebugUtilsObjectTagEXT(m_vkDevice, pTagInfo);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkQueueBeginDebugUtilsLabel.html
  void QueueBeginDebugUtilsLabel(
    VkQueue queue, const VkDebugUtilsLabelEXT* pLabelInfo)
  {
    pfn.QueueBeginDebugUtilsLabelEXT(queue, pLabelInfo);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkQueueEndDebugUtilsLabel.html
  void QueueEndDebugUtilsLabel(VkQueue queue)
  {
    pfn.QueueEndDebugUtilsLabelEXT(queue);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkQueueInsertDebugUtilsLabel.html
  void QueueInsertDebugUtilsLabel(
    VkQueue queue, const VkDebugUtilsLabelEXT* pLabelInfo)
  {
    pfn.QueueInsertDebugUtilsLabelEXT(queue, pLabelInfo);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdBeginDebugUtilsLabelEXT.html
  void CmdBeginDebugUtilsLabelEXT(
    VkCommandBuffer             commandBuffer,
    const VkDebugUtilsLabelEXT* pLabelInfo)
  {
    pfn.CmdBeginDebugUtilsLabelEXT(commandBuffer, pLabelInfo);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdEndDebugUtilsLabelEXT.html
  void CmdEndDebugUtilsLabelEXT(VkCommandBuffer commandBuffer)
  {
    pfn.CmdEndDebugUtilsLabelEXT(commandBuffer);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCmdInsertDebugUtilsLabelEXT.html
  void CmdInsertDebugUtilsLabelEXT(
    VkCommandBuffer             commandBuffer,
    const VkDebugUtilsLabelEXT* pLabelInfo)
  {
    pfn.CmdInsertDebugUtilsLabelEXT(commandBuffer, pLabelInfo);
  }

  ///////////////////////////////////////////////////////////////////////
  // Surface and presentation
  ///////////////////////////////////////////////////////////////////////
public:
#ifdef VD_OS_WINDOWS
  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateWin32SurfaceKHR.html
  VkResult CreateWin32SurfaceKHR(
    const VkWin32SurfaceCreateInfoKHR* pCreateInfo,
    VkSurfaceKHR*                      pSurface)
  {
    return pfn.CreateWin32SurfaceKHR(
      m_vkInstance, pCreateInfo, m_callbacks, pSurface);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkGetPhysicalDeviceWin32PresentationSupportKHR.html
  VkBool32 GetPhysicalDeviceWin32PresentationSupportKHR(
    VkPhysicalDevice physicalDevice, const u32 queueFamilyIndex)
  {
    return pfn.GetPhysicalDeviceWin32PresentationSupportKHR(
      physicalDevice, queueFamilyIndex);
  }
#endif

#ifdef VD_OS_LINUX
  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateXcbSurfaceKHR.html
  VkResult CreateXcbSurfaceKHR(
    const VkXcbSurfaceCreateInfoKHR* pCreateInfo,
    VkSurfaceKHR*                    pSurface)
  {
    return pfn.CreateXcbSurfaceKHR(
      m_vkInstance, pCreateInfo, m_callbacks, pSurface);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateXcbSurfaceKHR.html
  bool GetPhysicalDeviceXcbPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
    xcb_connection_t* connection, xcb_visualid_t visual_id)
  {
    return pfn.GetPhysicalDeviceXcbPresentationSupportKHR(
      physicalDevice, queueFamilyIndex, connection, visual_id);
  }
#endif

#ifdef VD_OS_ANDROID
  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateAndroidSurfaceKHR.html
  VkResult CreateAndroidSurfaceKHR(
    const VkAndroidSurfaceCreateInfoKHR* pCreateInfo,
    VkSurfaceKHR*                        pSurface)
  {
    return pfn.CreateAndroidSurfaceKHR(
      m_vkInstance, pCreateInfo, m_callbacks, pSurface);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateAndroidSurfaceKHR.html
  bool GetPhysicalDeviceAndroidPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, const u32 queueFamilyIndex)
  {
    return pfn.GetPhysicalDeviceAndroidPresentationSupportKHR(
      physicalDevice, queueFamilyIndex);
  }
#endif

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkDestroySurfaceKHR.html
  void DestroySurfaceKHR(VkSurfaceKHR surface)
  {
    pfn.DestroySurfaceKHR(m_vkInstance, surface, m_callbacks);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkGetPhysicalDeviceSurfaceSupportKHR.html
  VkResult GetPhysicalDeviceSurfaceSupportKHR(
    VkPhysicalDevice physicalDevice, const u32 queueFamilyIndex,
    VkSurfaceKHR surface, VkBool32* pSupported)
  {
    return pfn.GetPhysicalDeviceSurfaceSupportKHR(
      physicalDevice, queueFamilyIndex, surface, pSupported);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkGetPhysicalDeviceSurfaceCapabilitiesKHR.html
  VkResult GetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR* pSurfaceCapabilities)
  {
    return pfn.GetPhysicalDeviceSurfaceCapabilitiesKHR(
      physicalDevice, surface, pSurfaceCapabilities);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkGetPhysicalDeviceSurfaceFormatsKHR.html
  VkResult GetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    u32* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats)
  {
    return pfn.GetPhysicalDeviceSurfaceFormatsKHR(
      physicalDevice, surface, pSurfaceFormatCount, pSurfaceFormats);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkGetPhysicalDeviceSurfacePresentModesKHR.html
  VkResult GetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    u32* pPresentModeCount, VkPresentModeKHR* pPresentModes)
  {
    return pfn.GetPhysicalDeviceSurfacePresentModesKHR(
      physicalDevice, surface, pPresentModeCount, pPresentModes);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkCreateSwapchainKHR.html
  VkResult CreateSwapchainKHR(
    const VkSwapchainCreateInfoKHR* pCreateInfo,
    VkSwapchainKHR*                 pSwapchain)
  {
    return pfn.CreateSwapchainKHR(
      m_vkDevice, pCreateInfo, m_callbacks, pSwapchain);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkDestroySwapchainKHR.html
  void DestroySwapchainKHR(VkSwapchainKHR swapchain)
  {
    return pfn.DestroySwapchainKHR(m_vkDevice, swapchain, m_callbacks);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkGetSwapchainImagesKHR.html
  VkResult GetSwapchainImagesKHR(
    VkSwapchainKHR swapchain, u32* pSwapchainImageCount,
    VkImage* pSwapchainImages)
  {
    return pfn.GetSwapchainImagesKHR(
      m_vkDevice, swapchain, pSwapchainImageCount, pSwapchainImages);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkAcquireNextImageKHR.html
  VkResult AcquireNextImageKHR(
    VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore,
    VkFence fence, u32* pImageIndex)
  {
    return pfn.AcquireNextImageKHR(
      m_vkDevice, swapchain, timeout, semaphore, fence, pImageIndex);
  }

  /// @link https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkAcquireNextImage2KHR.html
  VkResult AcquireNextImage2KHR(
    const VkAcquireNextImageInfoKHR* pAcquireInfo, u32* pImageIndex)
  {
    return pfn.AcquireNextImage2KHR(
      m_vkDevice, pAcquireInfo, pImageIndex);
  }

  /// @link ://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/vkQueuePresentKHR.html
  VkResult
  QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
  {
    return pfn.QueuePresentKHR(queue, pPresentInfo);
  }

  ///////////////////////////////////////////////////////////////////////
  // Function pointers
  ///////////////////////////////////////////////////////////////////////
public:
  struct {
    // Vulkan core ////////////////////////////////////////////////////

    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr   GetDeviceProcAddr;

    PFN_vkEnumerateInstanceVersion EnumerateInstanceVersion;
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices;
    PFN_vkEnumerateInstanceLayerProperties
      EnumerateInstanceLayerProperties;
    PFN_vkEnumerateInstanceExtensionProperties
      EnumerateInstanceExtensionProperties;

    PFN_vkCreateInstance  CreateInstance;
    PFN_vkDestroyInstance DestroyInstance;
    PFN_vkCreateDevice    CreateDevice;
    PFN_vkDestroyDevice   DestroyDevice;
    PFN_vkDeviceWaitIdle  DeviceWaitIdle;

    PFN_vkGetPhysicalDeviceProperties2 GetPhysicalDeviceProperties2;
    PFN_vkGetPhysicalDeviceFeatures2   GetPhysicalDeviceFeatures2;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties2
      GetPhysicalDeviceQueueFamilyProperties2;
    PFN_vkGetPhysicalDeviceMemoryProperties2
      GetPhysicalDeviceMemoryProperties2;
    PFN_vkGetPhysicalDeviceFormatProperties
      GetPhysicalDeviceFormatProperties;
    PFN_vkGetPhysicalDeviceFormatProperties2
      GetPhysicalDeviceFormatProperties2;
    PFN_vkGetPhysicalDeviceImageFormatProperties
      GetPhysicalDeviceImageFormatProperties;

    PFN_vkGetDeviceQueue  GetDeviceQueue;
    PFN_vkQueueSubmit     QueueSubmit;
    PFN_vkQueueWaitIdle   QueueWaitIdle;
    PFN_vkQueueBindSparse QueueBindSparse;

    PFN_vkCreateBuffer                CreateBuffer;
    PFN_vkDestroyBuffer               DestroyBuffer;
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements;
    PFN_vkBindBufferMemory            BindBufferMemory;

    PFN_vkCreateImage                CreateImage;
    PFN_vkDestroyImage               DestroyImage;
    PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements;
    PFN_vkBindImageMemory            BindImageMemory;

    PFN_vkCreateBufferView  CreateBufferView;
    PFN_vkDestroyBufferView DestroyBufferView;
    PFN_vkCreateImageView   CreateImageView;
    PFN_vkDestroyImageView  DestroyImageView;

    PFN_vkAllocateMemory          AllocateMemory;
    PFN_vkFreeMemory              FreeMemory;
    PFN_vkMapMemory               MapMemory;
    PFN_vkUnmapMemory             UnmapMemory;
    PFN_vkFlushMappedMemoryRanges FlushMappedMemoryRanges;

    PFN_vkCreateFramebuffer  CreateFramebuffer;
    PFN_vkDestroyFramebuffer DestroyFramebuffer;

    PFN_vkCreateShaderModule  CreateShaderModule;
    PFN_vkDestroyShaderModule DestroyShaderModule;

    PFN_vkCreateCommandPool  CreateCommandPool;
    PFN_vkDestroyCommandPool DestroyCommandPool;
    PFN_vkResetCommandPool   ResetCommandPool;
    PFN_vkTrimCommandPool    TrimCommandPool;

    PFN_vkAllocateCommandBuffers AllocateCommandBuffers;
    PFN_vkFreeCommandBuffers     FreeCommandBuffers;
    PFN_vkBeginCommandBuffer     BeginCommandBuffer;
    PFN_vkEndCommandBuffer       EndCommandBuffer;
    PFN_vkResetCommandBuffer     ResetCommandBuffer;

    PFN_vkCreateRenderPass  CreateRenderPass;
    PFN_vkCreateRenderPass2 CreateRenderPass2;
    PFN_vkDestroyRenderPass DestroyRenderPass;

    PFN_vkCreateFence    CreateFence;
    PFN_vkDestroyFence   DestroyFence;
    PFN_vkResetFences    ResetFences;
    PFN_vkWaitForFences  WaitForFences;
    PFN_vkGetFenceStatus GetFenceStatus;

    PFN_vkCreateSemaphore          CreateSemaphore;
    PFN_vkDestroySemaphore         DestroySemaphore;
    PFN_vkWaitSemaphores           WaitSemaphores;
    PFN_vkSignalSemaphore          SignalSemaphore;
    PFN_vkGetSemaphoreCounterValue GetSemaphoreCounterValue;

    PFN_vkCreateGraphicsPipelines CreateGraphicsPipelines;
    PFN_vkCreateComputePipelines  CreateComputePipelines;
    PFN_vkDestroyPipeline         DestroyPipeline;
    PFN_vkCreatePipelineCache     CreatePipelineCache;
    PFN_vkDestroyPipelineCache    DestroyPipelineCache;
    PFN_vkGetPipelineCacheData    GetPipelineCacheData;
    PFN_vkMergePipelineCaches     MergePipelineCaches;
    PFN_vkCreatePipelineLayout    CreatePipelineLayout;
    PFN_vkDestroyPipelineLayout   DestroyPipelineLayout;

    PFN_vkCreateDescriptorSetLayout  CreateDescriptorSetLayout;
    PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout;
    PFN_vkCreateDescriptorPool       CreateDescriptorPool;
    PFN_vkDestroyDescriptorPool      DestroyDescriptorPool;
    PFN_vkResetDescriptorPool        ResetDescriptorPool;
    PFN_vkAllocateDescriptorSets     AllocateDescriptorSets;
    PFN_vkFreeDescriptorSets         FreeDescriptorSets;
    PFN_vkUpdateDescriptorSets       UpdateDescriptorSets;

    PFN_vkCreateSampler  CreateSampler;
    PFN_vkDestroySampler DestroySampler;

    // Commands ///////////////////////////////////////////////////////

    PFN_vkCmdExecuteCommands  CmdExecuteCommands;
    PFN_vkCmdBeginRendering   CmdBeginRendering;
    PFN_vkCmdEndRendering     CmdEndRendering;
    PFN_vkCmdBeginRenderPass  CmdBeginRenderPass;
    PFN_vkCmdEndRenderPass    CmdEndRenderPass;
    PFN_vkCmdNextSubpass      CmdNextSubpass;
    PFN_vkCmdBeginRenderPass2 CmdBeginRenderPass2;
    PFN_vkCmdEndRenderPass2   CmdEndRenderPass2;
    PFN_vkCmdNextSubpass2     CmdNextSubpass2;

    PFN_vkCmdBindPipeline       CmdBindPipeline;
    PFN_vkCmdBindVertexBuffers  CmdBindVertexBuffers;
    PFN_vkCmdBindIndexBuffer    CmdBindIndexBuffer;
    PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets;
    PFN_vkCmdPushConstants      CmdPushConstants;

    PFN_vkCmdSetBlendConstants     CmdSetBlendConstants;
    PFN_vkCmdSetDepthBias          CmdSetDepthBias;
    PFN_vkCmdSetDepthBounds        CmdSetDepthBounds;
    PFN_vkCmdSetScissor            CmdSetScissor;
    PFN_vkCmdSetStencilCompareMask CmdSetStencilCompareMask;
    PFN_vkCmdSetStencilReference   CmdSetStencilReference;
    PFN_vkCmdSetStencilWriteMask   CmdSetStencilWriteMask;
    PFN_vkCmdSetViewport           CmdSetViewport;

    PFN_vkCmdCopyBuffer        CmdCopyBuffer;
    PFN_vkCmdCopyImage         CmdCopyImage;
    PFN_vkCmdCopyImageToBuffer CmdCopyImageToBuffer;
    PFN_vkCmdCopyBufferToImage CmdCopyBufferToImage;
    PFN_vkCmdBlitImage         CmdBlitImage;
    PFN_vkCmdResolveImage      CmdResolveImage;
    PFN_vkCmdUpdateBuffer      CmdUpdateBuffer;

    PFN_vkCmdDraw                CmdDraw;
    PFN_vkCmdDrawIndirect        CmdDrawIndirect;
    PFN_vkCmdDrawIndexed         CmdDrawIndexed;
    PFN_vkCmdDrawIndexedIndirect CmdDrawIndexedIndirect;

    PFN_vkCmdDispatch         CmdDispatch;
    PFN_vkCmdDispatchBase     CmdDispatchBase;
    PFN_vkCmdDispatchIndirect CmdDispatchIndirect;

    PFN_vkCmdClearAttachments       CmdClearAttachments;
    PFN_vkCmdClearColorImage        CmdClearColorImage;
    PFN_vkCmdClearDepthStencilImage CmdClearDepthStencilImage;
    PFN_vkCmdFillBuffer             CmdFillBuffer;

    PFN_vkCmdPipelineBarrier CmdPipelineBarrier;
    PFN_vkCmdSetEvent        CmdSetEvent;
    PFN_vkCmdResetEvent      CmdResetEvent;
    PFN_vkCmdWaitEvents      CmdWaitEvents;

    PFN_vkCmdBeginQuery           CmdBeginQuery;
    PFN_vkCmdEndQuery             CmdEndQuery;
    PFN_vkCmdCopyQueryPoolResults CmdCopyQueryPoolResults;
    PFN_vkCmdResetQueryPool       CmdResetQueryPool;
    PFN_vkCmdWriteTimestamp       CmdWriteTimestamp;

    // Extensions /////////////////////////////////////////////////////

    // Debug Utils
    PFN_vkCreateDebugUtilsMessengerEXT  CreateDebugUtilsMessengerEXT;
    PFN_vkDestroyDebugUtilsMessengerEXT DestroyDebugUtilsMessengerEXT;
    PFN_vkSubmitDebugUtilsMessageEXT    SubmitDebugUtilsMessageEXT;

    PFN_vkSetDebugUtilsObjectNameEXT    SetDebugUtilsObjectNameEXT;
    PFN_vkSetDebugUtilsObjectTagEXT     SetDebugUtilsObjectTagEXT;
    PFN_vkQueueBeginDebugUtilsLabelEXT  QueueBeginDebugUtilsLabelEXT;
    PFN_vkQueueEndDebugUtilsLabelEXT    QueueEndDebugUtilsLabelEXT;
    PFN_vkQueueInsertDebugUtilsLabelEXT QueueInsertDebugUtilsLabelEXT;
    PFN_vkCmdBeginDebugUtilsLabelEXT    CmdBeginDebugUtilsLabelEXT;
    PFN_vkCmdEndDebugUtilsLabelEXT      CmdEndDebugUtilsLabelEXT;
    PFN_vkCmdInsertDebugUtilsLabelEXT   CmdInsertDebugUtilsLabelEXT;

    // Surface and presentation
#ifdef VD_OS_WINDOWS
    PFN_vkCreateWin32SurfaceKHR CreateWin32SurfaceKHR;
    PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR
      GetPhysicalDeviceWin32PresentationSupportKHR;
#endif

#ifdef VD_OS_LINUX
    PFN_vkCreateXcbSurfaceKHR CreateXcbSurfaceKHR;
    PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR
      GetPhysicalDeviceXcbPresentationSupportKHR;
#endif

#ifdef VD_OS_ANDROID
    PFN_vkCreateAndroidSurfaceKHR CreateAndroidSurfaceKHR;
    PFN_vkGetPhysicalDeviceAndroidPresentationSupportKHR
      GetPhysicalDeviceAndroidPresentationSupportKHR;
#endif
    PFN_vkDestroySurfaceKHR DestroySurfaceKHR;

    PFN_vkGetPhysicalDeviceSurfaceSupportKHR
      GetPhysicalDeviceSurfaceSupportKHR;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR
      GetPhysicalDeviceSurfaceCapabilitiesKHR;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR
      GetPhysicalDeviceSurfaceFormatsKHR;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR
      GetPhysicalDeviceSurfacePresentModesKHR;

    PFN_vkCreateSwapchainKHR    CreateSwapchainKHR;
    PFN_vkDestroySwapchainKHR   DestroySwapchainKHR;
    PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR;
    PFN_vkAcquireNextImageKHR   AcquireNextImageKHR;
    PFN_vkAcquireNextImage2KHR  AcquireNextImage2KHR;
    PFN_vkQueuePresentKHR       QueuePresentKHR;
  } pfn;
};
} // namespace vd

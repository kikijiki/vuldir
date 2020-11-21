#pragma once

#include "vuldir/api/Common.hpp"

#ifdef VD_API_VK
  #include "vuldir/api/vk/VkDispatcher.hpp"
#endif

namespace vd {

class PhysicalDevice
{
public:
  ~PhysicalDevice();

public:
  Str GetDescription() const;
  u64 GetDedicatedMemorySize() const;

#ifdef VD_API_VK
public:
  struct Properties {
    VkPhysicalDeviceProperties2        properties   = {};
    VkPhysicalDeviceVulkan11Properties properties11 = {};
    VkPhysicalDeviceVulkan12Properties properties12 = {};

    Properties()
    {
      properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
      properties11.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
      properties12.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;

      properties.pNext   = &properties11;
      properties11.pNext = &properties12;
    }

    const VkPhysicalDeviceProperties* operator->() const
    {
      return &properties.properties;
    }
  };

  struct MemoryProperties {
    VkPhysicalDeviceMemoryProperties2 properties = {};

    MemoryProperties()
    {
      properties.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
      properties.pNext = nullptr;
    }

    const VkPhysicalDeviceMemoryProperties* operator->() const
    {
      return &properties.memoryProperties;
    }
  };

  struct Features {
    VkPhysicalDeviceFeatures2                 features = {};
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline = {};

    VkPhysicalDeviceDynamicRenderingFeatures    dynamicRendering   = {};
    VkPhysicalDeviceDescriptorIndexingFeatures  descriptorIndexing = {};
    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddress =
      {};

    Features()
    {
      features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
      timeline.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
      dynamicRendering.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
      descriptorIndexing.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
      bufferDeviceAddress.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;

      features.pNext           = &timeline;
      timeline.pNext           = &dynamicRendering;
      dynamicRendering.pNext   = &descriptorIndexing;
      descriptorIndexing.pNext = &bufferDeviceAddress;
    }

    const VkPhysicalDeviceFeatures* operator->() const
    {
      return &features.features;
    }
  };

public:
  PhysicalDevice(Dispatcher& vk, VkPhysicalDevice handle);

  VkPhysicalDevice GetHandle() const { return m_handle; }

  bool HasGraphicsCapabilities() const;
  bool HasPresentCapabilities(VkSurfaceKHR surface) const;
  bool HasRequiredFeatures(bool dbgEnable, bool dbgUseRenderdoc) const;

  VkSurfaceCapabilitiesKHR
  GetPresentCapabilities(VkSurfaceKHR surface) const;
  Arr<VkSurfaceFormatKHR> GetPresentFormats(VkSurfaceKHR surface) const;
  bool                    IsPresentFormatSupported(
                       VkSurfaceKHR surface, const VkSurfaceFormatKHR format) const;
  Arr<VkPresentModeKHR> GetPresentModes(VkSurfaceKHR surface) const;
  bool                  IsPresentModeSupported(
                     VkSurfaceKHR, const VkPresentModeKHR presentMode) const;

  u32                      GetQueueFamilyCount() const;
  VkQueueFamilyProperties2 GetQueueFamily(u32 index) const;
  bool                     HasGraphics(u32 index) const;
  bool HasPresent(u32 index, VkSurfaceKHR surface) const;
  bool HasTransfer(u32 index) const;
  bool HasCompute(u32 index) const;

  const Properties&       GetProperties() const { return m_properties; }
  const Features&         GetFeatures() const { return m_features; }
  const MemoryProperties& GetMemoryProperties() const
  {
    return m_memoryProperties;
  }

private:
  Dispatcher& m_vk;

  VkPhysicalDevice              m_handle;
  Arr<VkQueueFamilyProperties2> m_queueFamilies;

  Properties       m_properties;
  Features         m_features;
  MemoryProperties m_memoryProperties;
#endif

#ifdef VD_API_DX
public:
  PhysicalDevice(ComPtr<IDXGIAdapter4> handle);

  IDXGIAdapter4* GetHandle() const { return m_handle.Get(); }

private:
  ComPtr<IDXGIAdapter4> m_handle;
  DXGI_ADAPTER_DESC3    m_desc;
#endif
};

} // namespace vd

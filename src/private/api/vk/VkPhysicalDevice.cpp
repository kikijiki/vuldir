#include "vuldir/api/PhysicalDevice.hpp"
#include "vuldir/api/vk/VkUti.hpp"

using namespace vd;

PhysicalDevice::PhysicalDevice(Dispatcher& vk, VkPhysicalDevice handle):
  m_vk{vk},
  m_handle{handle},
  m_queueFamilies{},
  m_properties{},
  m_features{},
  m_memoryProperties{}
{
  u32 queueFamilyCount;
  m_vk.GetPhysicalDeviceQueueFamilyProperties2(
    m_handle, &queueFamilyCount, nullptr);

  m_queueFamilies.resize(
    queueFamilyCount,
    {VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2, nullptr, {}});
  m_vk.GetPhysicalDeviceQueueFamilyProperties2(
    m_handle, &queueFamilyCount, m_queueFamilies.data());

  vk.GetPhysicalDeviceProperties2(m_handle, &m_properties.properties);
  vk.GetPhysicalDeviceFeatures2(m_handle, &m_features.features);
  m_vk.GetPhysicalDeviceMemoryProperties2(
    m_handle, &m_memoryProperties.properties);
}

PhysicalDevice::~PhysicalDevice() {}

Str PhysicalDevice::GetDescription() const
{
  return m_properties->deviceName;
}

u64 PhysicalDevice::GetDedicatedMemorySize() const
{
  u64 totalSize = 0u;
  for(u32 idx = 0u; idx < m_memoryProperties->memoryHeapCount; ++idx) {
    const auto& heap = m_memoryProperties->memoryHeaps[idx];
    if(vd::hasFlag(heap.flags, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT))
      totalSize += heap.size;
  }
  return totalSize;
}

bool PhysicalDevice::HasGraphicsCapabilities() const
{
  for(u32 idx = 0u; idx < GetQueueFamilyCount(); ++idx)
    if(HasGraphics(idx)) return true;

  return false;
}

bool PhysicalDevice::HasPresentCapabilities(VkSurfaceKHR surface) const
{
  for(u32 idx = 0u; idx < GetQueueFamilyCount(); ++idx)
    if(HasPresent(idx, surface)) return true;

  return false;
}

VkSurfaceCapabilitiesKHR
PhysicalDevice::GetPresentCapabilities(VkSurfaceKHR surface) const
{
  VkSurfaceCapabilitiesKHR capabilities{};
  VDVkTry(m_vk.GetPhysicalDeviceSurfaceCapabilitiesKHR(
    m_handle, surface, &capabilities));
  return capabilities;
}

Arr<VkSurfaceFormatKHR>
PhysicalDevice::GetPresentFormats(VkSurfaceKHR surface) const
{
  u32 formatCount = 0u;
  VDVkTry(m_vk.GetPhysicalDeviceSurfaceFormatsKHR(
    m_handle, surface, &formatCount, nullptr));
  Arr<VkSurfaceFormatKHR> formats(formatCount);
  VDVkTry(m_vk.GetPhysicalDeviceSurfaceFormatsKHR(
    m_handle, surface, &formatCount, formats.data()));
  return formats;
}

bool PhysicalDevice::IsPresentFormatSupported(
  VkSurfaceKHR surface, const VkSurfaceFormatKHR format) const
{
  const auto availablePresentFormats = GetPresentFormats(surface);

  // No preferred format
  if(
    availablePresentFormats.size() == 1u &&
    availablePresentFormats.front().format == VK_FORMAT_UNDEFINED)
    return true;

  for(auto& f: availablePresentFormats)
    if(f.format == format.format && f.colorSpace == format.colorSpace)
      return true;

  return false;
}

Arr<VkPresentModeKHR>
PhysicalDevice::GetPresentModes(VkSurfaceKHR surface) const
{
  u32 modesCount = 0u;
  VDVkTry(m_vk.GetPhysicalDeviceSurfacePresentModesKHR(
    m_handle, surface, &modesCount, nullptr));
  Arr<VkPresentModeKHR> modes(modesCount);
  VDVkTry(m_vk.GetPhysicalDeviceSurfacePresentModesKHR(
    m_handle, surface, &modesCount, modes.data()));
  return modes;
}

bool PhysicalDevice::IsPresentModeSupported(
  VkSurfaceKHR surface, const VkPresentModeKHR presentMode) const
{
  const auto availablePresentModes = GetPresentModes(surface);
  return std::find(
           availablePresentModes.cbegin(), availablePresentModes.cend(),
           presentMode) != availablePresentModes.cend();
}

bool PhysicalDevice::HasRequiredFeatures(
  bool dbgEnable, bool dbgUseRenderdoc) const
{
  if(dbgEnable && dbgUseRenderdoc) {
    if(
      m_features.bufferDeviceAddress.bufferDeviceAddressCaptureReplay !=
      VK_TRUE) {
      return false;
    }
  }

  return m_features.timeline.timelineSemaphore == VK_TRUE &&
         m_features.synchronization2.synchronization2 == VK_TRUE &&
         m_features.dynamicRendering.dynamicRendering == VK_TRUE &&
         m_features.descriptorIndexing
             .shaderSampledImageArrayNonUniformIndexing == VK_TRUE &&
         m_features.descriptorIndexing.runtimeDescriptorArray ==
           VK_TRUE &&
         m_features.descriptorIndexing
             .descriptorBindingVariableDescriptorCount == VK_TRUE &&
         m_features.descriptorIndexing
             .descriptorBindingPartiallyBound == VK_TRUE &&
         m_features.descriptorIndexing
             .descriptorBindingSampledImageUpdateAfterBind == VK_TRUE &&
         m_features.descriptorIndexing
             .descriptorBindingStorageImageUpdateAfterBind == VK_TRUE &&
         m_features.descriptorIndexing
             .descriptorBindingStorageBufferUpdateAfterBind == VK_TRUE;
}

u32 PhysicalDevice::GetQueueFamilyCount() const
{
  return vd::size32(m_queueFamilies);
}

VkQueueFamilyProperties2 PhysicalDevice::GetQueueFamily(u32 index) const
{
  return m_queueFamilies[index];
}

bool PhysicalDevice::HasGraphics(u32 index) const
{
  return vd::hasFlag(
    m_queueFamilies[index].queueFamilyProperties.queueFlags,
    VK_QUEUE_GRAPHICS_BIT);
}

bool PhysicalDevice::HasPresent(u32 index, VkSurfaceKHR surface) const
{
  VkBool32 hasPresent = VK_FALSE;
  VDVkTry(m_vk.GetPhysicalDeviceSurfaceSupportKHR(
    m_handle, index, surface, &hasPresent));
  return hasPresent == VK_TRUE;
}

bool PhysicalDevice::HasTransfer(u32 index) const
{
  return vd::hasFlag(
    m_queueFamilies[index].queueFamilyProperties.queueFlags,
    VK_QUEUE_TRANSFER_BIT);
}

bool PhysicalDevice::HasCompute(u32 index) const
{
  return vd::hasFlag(
    m_queueFamilies[index].queueFamilyProperties.queueFlags,
    VK_QUEUE_COMPUTE_BIT);
}

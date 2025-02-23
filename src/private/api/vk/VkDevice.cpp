#include "api/vk/VkHostAllocator.hpp"
#include "vuldir/api/Binder.hpp"
#include "vuldir/api/CommandBuffer.hpp"
#include "vuldir/api/Device.hpp"
#include "vuldir/api/Fence.hpp"
#include "vuldir/api/MemoryPool.hpp"
#include "vuldir/api/PhysicalDevice.hpp"
#include "vuldir/api/vk/VkDispatcher.hpp"
#include "vuldir/api/vk/VkUti.hpp"

using namespace vd;

struct QueueInfo {
  Arr<VkDeviceQueueCreateInfo> CIs;

  static constexpr SArr<f32, QueueTypeCount> priorities = {
    1.0f, 1.0f, 1.0f};
};

QueueInfo makeQueueInfo(
  const PhysicalDevice& physicalDevice, VkSurfaceKHR surface);

bool checkLayerExtensionSupport(
  Dispatcher& vk, const Arr<const char*>& requiredLayers,
  const Arr<const char*>& requiredExtensions);

Device::Device(const Desc& desc, const Swapchain::Desc& swapchainDesc):
  m_desc{desc},
  m_window{swapchainDesc.window},
  m_physicalDevice{},
  m_swapchain{},
  m_binder{},
  m_queues{},
  m_memoryPools{},
  m_memoryMutex{},
  m_hostAllocator{},
  m_dispatcher{},
  m_instance{},
  m_handle{},
  m_surface{}
{
  create_api(desc);
  create_physicalDevice(desc);
  create_device(desc);

  m_swapchain = std::make_unique<Swapchain>(*this, swapchainDesc);
}

Device::~Device()
{
  // Order is important here.

  m_swapchain = nullptr; // Must go before destroying binder.
  m_binder    = nullptr;

  m_memoryPools.clear();

  if(m_surface) m_dispatcher->DestroySurfaceKHR(m_surface);
  if(m_handle) m_dispatcher->DestroyDevice(m_handle);
  if(m_instance) m_dispatcher->DestroyInstance(m_instance);

  m_surface  = nullptr;
  m_handle   = nullptr;
  m_instance = nullptr;

  m_physicalDevice = nullptr;
}

VkSurfaceCapabilitiesKHR Device::GetSurfaceCapabilities() const
{
  VkSurfaceCapabilitiesKHR cap = {};
  m_dispatcher->GetPhysicalDeviceSurfaceCapabilitiesKHR(
    m_physicalDevice->GetHandle(), m_surface, &cap);
  return cap;
}

void Device::SetObjectName(u64 handle, u64 type, const char* name)
{
  if(!IsDebugEnabled()) return;
  if(!handle || !name) return;

  VkDebugUtilsObjectNameInfoEXT info{};
  info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
  info.objectHandle = handle;
  info.objectType   = static_cast<VkObjectType>(type);
  info.pObjectName  = name;

  m_dispatcher->SetDebugUtilsObjectNameEXT(&info);
}

const VkAllocationCallbacks* Device::GetAllocationCallbacks() const
{
  return m_hostAllocator->GetAllocationCallbacks();
}

void Device::Submit(
  Span<CommandBuffer*> cmds, Span<Fence*> waits, Span<Fence*> signals,
  Fence* submitFence, SwapchainDep swapchainDep)
{
  if(cmds.size() == 0u) return;

  const auto  queueType = cmds[0]->GetQueueType();
  const auto& queue     = m_queues[enumValue(queueType)];

  Arr<VkCommandBuffer>      vkCmds;
  Arr<VkSemaphore>          vkWaitHandles;
  Arr<u64>                  vkWaitValues;
  Arr<VkPipelineStageFlags> vkWaitStages;
  Arr<VkSemaphore>          vkSignalHandles;
  Arr<u64>                  vkSignalValues;

  VDLogV(
    "Submitting %u command buffers on queue %s", cmds.size(),
    queue.name.c_str());

  for(auto& cmd: cmds) {
    if(cmd->GetQueueType() != queueType)
      throw std::runtime_error("Command buffer queue mismatch");
    vkCmds.push_back(cmd->GetHandle());
  }

  for(auto& wait: waits) {
    vkWaitHandles.push_back(wait->GetSemaphoreHandle());
    vkWaitStages.push_back(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); // TODO
    vkWaitValues.push_back(wait->GetTarget());
    VDLogV("- Wait: %s", wait->GetName().c_str());
  }

  if(
    swapchainDep == SwapchainDep::Acquire ||
    swapchainDep == SwapchainDep::AcquireRelease) {
    vkWaitHandles.push_back(
      m_swapchain->GetAcquireFence().GetSemaphoreHandle());
    vkWaitStages.push_back(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); // TODO
    VDLogV(
      "- Wait SC: %s",
      m_swapchain->GetAcquireFence().GetName().c_str());
  }

  for(auto& signal: signals) {
    vkSignalHandles.push_back(signal->GetSemaphoreHandle());
    vkSignalValues.push_back(signal->GetTarget());
    VDLogV("- Signal: %s", signal->GetName().c_str());
  }

  if(
    swapchainDep == SwapchainDep::Release ||
    swapchainDep == SwapchainDep::AcquireRelease) {
    vkSignalHandles.push_back(
      m_swapchain->GetReleaseFence().GetSemaphoreHandle());
    VDLogV(
      "- Signal SC: %s",
      m_swapchain->GetReleaseFence().GetName().c_str());
  }

  VkTimelineSemaphoreSubmitInfo vkTimelineInfo = {
    .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
    .pNext = nullptr,
    .waitSemaphoreValueCount   = size32(vkWaitValues),
    .pWaitSemaphoreValues      = std::data(vkWaitValues),
    .signalSemaphoreValueCount = size32(vkSignalValues),
    .pSignalSemaphoreValues    = std::data(vkSignalValues)};

  // https://github.com/SaschaWillems/Vulkan/issues/433
  VkSubmitInfo vkInfo{
    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext                = &vkTimelineInfo,
    .waitSemaphoreCount   = size32(vkWaitHandles),
    .pWaitSemaphores      = std::data(vkWaitHandles),
    .pWaitDstStageMask    = std::data(vkWaitStages),
    .commandBufferCount   = size32(vkCmds),
    .pCommandBuffers      = std::data(vkCmds),
    .signalSemaphoreCount = size32(vkSignalHandles),
    .pSignalSemaphores    = std::data(vkSignalHandles)};

  auto vkSubmitFence =
    submitFence ? submitFence->GetFenceHandle() : VK_NULL_HANDLE;

  VDVkTry(m_dispatcher->QueueSubmit(
    queue.handle, 1u, &vkInfo, vkSubmitFence));
}

void Device::WaitIdle(QueueType queue)
{
  m_dispatcher->QueueWaitIdle(GetQueueHandle(queue));
}

void Device::WaitIdle()
{
  for(auto queue: QueueTypes)
    m_dispatcher->QueueWaitIdle(GetQueueHandle(queue));
}

void Device::create_api(const Desc& desc)
{
  m_hostAllocator = std::make_unique<HostAllocator>();
  m_dispatcher    = std::make_unique<Dispatcher>(*this);

  u32 apiVersion;
  VDVkTry(m_dispatcher->EnumerateInstanceVersion(&apiVersion));
  if(apiVersion < VK_API_VERSION_1_3) {
    throw std::logic_error("Vulkan API 1.3.0 or greater required");
  }

  Arr<const char*> layers;
  Arr<const char*> extensions;

  // struct VkLayerSettingEXT {
  //     const char*              pLayerName;
  //     const char*              pSettingName;
  //     VkLayerSettingTypeEXT    type;
  //     uint32_t                 valueCount;
  //     const void*              pValues;
  // }
  // Arr<VkLayerSettingEXT> layerSettings;
  //VkLayerSettingsCreateInfoEXT layerSettingsCI{
  //  .sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT};
  //const u32 BOOL32_TRUE = 1u;

  { // Debug
    if(desc.dbgEnable) {
      layers.push_back("VK_LAYER_KHRONOS_validation");
      layers.push_back("VK_LAYER_LUNARG_monitor");

      if(desc.dbgDumpLog) layers.push_back("VK_LAYER_LUNARG_api_dump");

      //if(desc.dbgUseRenderdoc)
      //  layers.push_back("VK_LAYER_RENDERDOC_Capture");

      // const char* settingNames[] = {
      //   "VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT",
      //   "VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT",
      //   "VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT",
      //   "VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT"};

      // for(const auto& settingName: settingNames) {
      //   layerSettings.push_back(VkLayerSettingEXT{
      //     .pLayerName   = "VK_LAYER_KHRONOS_validation",
      //     .pSettingName = settingName,
      //     .type         = VK_LAYER_SETTING_TYPE_BOOL32_EXT,
      //     .valueCount   = 1u,
      //     .pValues      = &BOOL32_TRUE});
      // }

      // layerSettingsCI.settingCount = vd::size32(layerSettings),
      // layerSettingsCI.pSettings    = std::data(layerSettings);

      //Deprecated by VK_EXT_layer_settings
      //extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);

      //extensions.push_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
  }

  { // Swapchain
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);

#if defined(VD_OS_WINDOWS)
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(VD_OS_LINUX)
    extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#elif defined(VD_OS_ANDROID)
    extensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#endif
  }

  if(!checkLayerExtensionSupport(*m_dispatcher, layers, extensions))
    throw std::runtime_error(
      "Could not find all the required layers and extensions.");

  VkApplicationInfo appInfo{};
  appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pEngineName        = "VULDIR";
  appInfo.engineVersion      = 0;
  appInfo.pApplicationName   = desc.appName.c_str();
  appInfo.applicationVersion = desc.appVersion;
  appInfo.apiVersion         = VK_API_VERSION_1_3;

  VkInstanceCreateInfo instanceCI{};
  instanceCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  //instanceCI.pNext             = &layerSettingsCI;
  instanceCI.pApplicationInfo        = &appInfo;
  instanceCI.enabledLayerCount       = vd::size32(layers);
  instanceCI.ppEnabledLayerNames     = std::data(layers);
  instanceCI.enabledExtensionCount   = vd::size32(extensions);
  instanceCI.ppEnabledExtensionNames = std::data(extensions);

  VDVkTry(m_dispatcher->CreateInstance(&instanceCI, &m_instance));
  m_dispatcher->Load(*this);

  // TODO: re-enable Debug stuff.
  // if (desc.dbgEnabled)
  //  m_debug.Attach(*this, desc.dbgLogLevel, desc.dbgDumpLog);
}

void Device::create_physicalDevice(const Desc& desc)
{
  VD_UNUSED(desc);

#if defined(VD_OS_WINDOWS)
  VkWin32SurfaceCreateInfoKHR ci{
    .sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
    .hinstance = m_window.hInstance,
    .hwnd      = m_window.hWnd};
  VDVkTry(m_dispatcher->CreateWin32SurfaceKHR(&ci, &m_surface));
#elif defined(VD_OS_LINUX)
  VkXcbSurfaceCreateInfoKHR ci{
    .sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
    .pNext      = nullptr,
    .flags      = 0u,
    .connection = m_window.connection,
    .window     = m_window.window};
  VDVkTry(m_dispatcher->CreateXcbSurfaceKHR(&ci, &m_surface));
#else
  #error NOT_IMPLEMENTED
#endif

  Arr<UPtr<PhysicalDevice>> physicalDevices;
  { // Enumerate physical devices
    u32 deviceCount = 0u;
    VDVkTry(
      m_dispatcher->EnumeratePhysicalDevices(&deviceCount, nullptr));
    Arr<VkPhysicalDevice> devices(deviceCount);
    VDVkTry(m_dispatcher->EnumeratePhysicalDevices(
      &deviceCount, devices.data()));
    physicalDevices.reserve(deviceCount);

    for(auto& deviceHandle: devices)
      physicalDevices.emplace_back(
        std::make_unique<PhysicalDevice>(*m_dispatcher, deviceHandle));
  }

  { // Select physical device
    u64 maxMemory = 0u;
    for(auto& physicalDevice: physicalDevices) {
      if(!physicalDevice->HasGraphicsCapabilities()) continue;
      if(!physicalDevice->HasPresentCapabilities(m_surface)) continue;
      if(!physicalDevice->HasRequiredFeatures(
           desc.dbgEnable, desc.dbgUseRenderdoc))
        continue;
      if(physicalDevice->GetDedicatedMemorySize() <= maxMemory)
        continue;

      m_physicalDevice = std::move(physicalDevice);
      maxMemory        = m_physicalDevice->GetDedicatedMemorySize();
    }
  }

  if(!m_physicalDevice)
    throw std::runtime_error("No compatible physical device found.");
}

void Device::create_device([[maybe_unused]] const Desc& desc)
{
  // Features
  PhysicalDevice::Features features;
  features.timeline.timelineSemaphore = VK_TRUE;

  features.dynamicRendering.dynamicRendering = VK_TRUE;

  features.descriptorIndexing
    .shaderSampledImageArrayNonUniformIndexing       = VK_TRUE;
  features.descriptorIndexing.runtimeDescriptorArray = VK_TRUE;
  features.descriptorIndexing.descriptorBindingVariableDescriptorCount =
    VK_TRUE;
  features.descriptorIndexing.descriptorBindingPartiallyBound = VK_TRUE;

  if(m_desc.dbgUseRenderdoc) {
    features.bufferDeviceAddress.bufferDeviceAddressCaptureReplay =
      VK_TRUE;
  }

  // Extensions
  Arr<const char*> extensions;
  extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);

  // Queues
  const auto queueInfo = makeQueueInfo(*m_physicalDevice, m_surface);

  VkDeviceCreateInfo deviceCI{};
  deviceCI.sType                 = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceCI.pNext                 = &features.features;
  deviceCI.enabledExtensionCount = vd::size32(extensions);
  deviceCI.ppEnabledExtensionNames = std::data(extensions);
  deviceCI.queueCreateInfoCount    = vd::size32(queueInfo.CIs);
  deviceCI.pQueueCreateInfos       = std::data(queueInfo.CIs);

  VDLogI(
    "Creating device: %s", m_physicalDevice->GetDescription().c_str());

  VDVkTry(m_dispatcher->CreateDevice(
    m_physicalDevice->GetHandle(), &deviceCI, &m_handle));
  m_dispatcher->Load(*this);

  auto queueTypeStr = [](u32 type) {
    switch(type) {
      case 0:
        return "Graphics";
      case 1:
        return "Compute";
      case 2:
        return "Copy";
      default:
        return "Unknown";
    }
  };

  // Queues
  u32 queueIdx = 0u;
  for(auto& info: queueInfo.CIs) {
    for(u32 idx = 0u; idx < info.queueCount; ++idx) {
      auto& queue = m_queues[queueIdx];
      auto  name =
        formatString("%s queue %u", queueTypeStr(queueIdx), idx);

      queue.name   = name;
      queue.family = info.queueFamilyIndex;
      queue.index  = idx;
      m_dispatcher->GetDeviceQueue(
        info.queueFamilyIndex, idx, &queue.handle);

      queueIdx++;
    }
  }

  m_binder = std::make_unique<Binder>(*this, Binder::Desc{});
}

bool checkLayerExtensionSupport(
  Dispatcher& vk, const Arr<const char*>& requiredLayers,
  const Arr<const char*>& requiredExtensions)
{
  Arr<VkLayerProperties>     layers;
  Arr<VkExtensionProperties> extensions;
  u64                        foundLayers     = 0u;
  u64                        foundExtensions = 0u;

  { // Enumerate
    u32 layerCount = 0u;
    VDVkTry(vk.EnumerateInstanceLayerProperties(&layerCount, nullptr));
    layers.resize(layerCount);
    VDVkTry(
      vk.EnumerateInstanceLayerProperties(&layerCount, layers.data()));

    u32 extensionCount = 0u;
    VDVkTry(vk.EnumerateInstanceExtensionProperties(
      nullptr, &extensionCount, nullptr));
    extensions.resize(extensionCount);
    VDVkTry(vk.EnumerateInstanceExtensionProperties(
      nullptr, &extensionCount, extensions.data()));

    for(auto& layer: layers) {
      u32 layerExtensionCount = 0u;
      VDVkTry(vk.EnumerateInstanceExtensionProperties(
        layer.layerName, &extensionCount, nullptr));
      Arr<VkExtensionProperties> layerExtensions(layerExtensionCount);
      VDVkTry(vk.EnumerateInstanceExtensionProperties(
        layer.layerName, &extensionCount, layerExtensions.data()));
      for(auto& extension: layerExtensions) {
        extensions.push_back(extension);
      }
    }

    std::sort(
      extensions.begin(), extensions.end(),
      [](const auto& a, const auto& b) {
        return strcmp(a.extensionName, b.extensionName) < 0;
      });
    extensions.erase(
      std::unique(
        extensions.begin(), extensions.end(),
        [](const auto& a, const auto& b) {
          return strcmp(a.extensionName, b.extensionName) == 0;
        }),
      extensions.end());
  }

  { // Verify
    for(auto& requiredLayerName: requiredLayers) {
      bool found = false;
      for(auto& layer: layers) {
        if(strcmp(requiredLayerName, layer.layerName) == 0) {
          VDLogI("Found required layer: %s", requiredLayerName);

          found = true;
          foundLayers++;
          break;
        }
      }

      if(!found)
        throw vd::makeError<std::runtime_error>(
          "Required layer not found: %s", requiredLayerName);
    }

    for(auto& requiredExtensionName: requiredExtensions) {
      bool found = false;
      for(auto& extension: extensions) {
        if(
          strcmp(requiredExtensionName, extension.extensionName) == 0) {
          VDLogI("Found required extension: %s", requiredExtensionName);

          found = true;
          foundExtensions++;
          break;
        }
      }

      if(!found)
        throw vd::makeError<std::runtime_error>(
          "Required instance extension not found: %s",
          requiredExtensionName);
    }

    return foundLayers == requiredLayers.size() &&
           foundExtensions == requiredExtensions.size();
  }
}

QueueInfo makeQueueInfo(
  const PhysicalDevice& physicalDevice, VkSurfaceKHR surface)
{
  QueueInfo info{};

  Opt<u32> graphicsFamily;
  Opt<u32> computeFamily;
  Opt<u32> transferFamily;

  // I make the following simplifying assumptions:
  // - presentQueue == graphicsQueue
  // - transfers are executed on a graphics or compute queue.
  // From the spec:
  // All commands that are allowed on a queue that supports transfer
  // operations are also allowed on a queue that supports either
  // graphics or compute operations. Thus, if the capabilities of a
  // queue family include VK_QUEUE_GRAPHICS_BIT or VK_QUEUE_COMPUTE_BIT,
  // then reporting the VK_QUEUE_TRANSFER_BIT capability separately for
  // that queue family is optional.
  for(u32 familyIdx{0u};
      familyIdx < physicalDevice.GetQueueFamilyCount(); familyIdx++) {
    u32 queuesLeft = physicalDevice.GetQueueFamily(familyIdx)
                       .queueFamilyProperties.queueCount;

    // Graphics+Present
    if(
      !graphicsFamily && physicalDevice.HasGraphics(familyIdx) &&
      physicalDevice.HasPresent(familyIdx, surface)) {
      graphicsFamily = familyIdx;
      queuesLeft--;
      if(queuesLeft == 0u) continue;
    }

    // Initialize if unassigned, might find something better later.
    if(!transferFamily && physicalDevice.HasTransfer(familyIdx)) {
      transferFamily = familyIdx;
      queuesLeft--;
      if(queuesLeft == 0u) continue;
    }

    // Compute
    if(!computeFamily && physicalDevice.HasCompute(familyIdx)) {
      computeFamily = familyIdx;
      queuesLeft--;
      if(queuesLeft == 0u) continue;
    }

    // Exclusive transfer
    if(
      physicalDevice.HasTransfer(familyIdx) &&
      !physicalDevice.HasGraphics(familyIdx) &&
      !physicalDevice.HasCompute(familyIdx)) {
      transferFamily = familyIdx;
      queuesLeft--;
      if(queuesLeft == 0u) continue;
    }
  }

  if(!graphicsFamily)
    throw std::runtime_error("No graphics queue family found");
  if(!computeFamily)
    throw std::runtime_error("No compute queue family found");
  if(!transferFamily)
    throw std::runtime_error("No transfer queue family found");

  VkDeviceQueueCreateInfo queueCI{};

  queueCI.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueCI.queueFamilyIndex = *graphicsFamily;
  queueCI.queueCount       = 1u;
  queueCI.pQueuePriorities = &info.priorities[0];

  if(graphicsFamily == computeFamily) {
    queueCI.queueCount++;
    computeFamily = std::nullopt;
  }

  if(graphicsFamily == transferFamily) {
    queueCI.queueCount++;
    transferFamily = std::nullopt;
  }

  info.CIs.push_back(queueCI);

  if(
    computeFamily && transferFamily &&
    computeFamily == transferFamily) {
    queueCI.queueFamilyIndex = *computeFamily;
    queueCI.queueCount       = 2u;
    queueCI.pQueuePriorities = &info.priorities[1];
    info.CIs.push_back(queueCI);
  } else {
    if(computeFamily) {
      queueCI.queueFamilyIndex = *computeFamily;
      queueCI.queueCount       = 1u;
      queueCI.pQueuePriorities = &info.priorities[1];
      info.CIs.push_back(queueCI);
    }

    if(transferFamily) {
      queueCI.queueFamilyIndex = *transferFamily;
      queueCI.queueCount       = 1u;
      queueCI.pQueuePriorities = &info.priorities[2];
      info.CIs.push_back(queueCI);
    }
  }

  return info;
}

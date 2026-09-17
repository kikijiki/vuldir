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
  SArr<u32, QueueTypeCount>    families = {};
  SArr<u32, QueueTypeCount>    indices  = {};

  // Enough slots for up to QueueTypeCount queues per unique family.
  SArr<f32, QueueTypeCount> priorities = {1.0f, 1.0f, 1.0f};
};

QueueInfo makeQueueInfo(
  const PhysicalDevice& physicalDevice, VkSurfaceKHR surface);

bool checkLayerExtensionSupport(
  Dispatcher& vk, const Arr<const char*>& requiredLayers,
  const Arr<const char*>& requiredExtensions);

namespace {
u32 getPhysicalDevicePreference(VkPhysicalDeviceType type)
{
  switch(type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 4u;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 3u;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 2u;
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return 1u;
    case VK_PHYSICAL_DEVICE_TYPE_OTHER:
    default: return 0u;
  }
}
} // namespace

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
  try {
    create_api(desc);
    create_physicalDevice(desc);
    create_device(desc);

    m_swapchain = std::make_unique<Swapchain>(*this, swapchainDesc);
  } catch(...) {
    // A throwing constructor does not run Device::~Device. Tear down only
    // the objects whose handles have already been published by each stage.
    m_swapchain = nullptr;
    m_binder    = nullptr;
    m_memoryPools.clear();
    if(m_dispatcher) {
      if(m_surface) m_dispatcher->DestroySurfaceKHR(m_surface);
      if(m_handle) m_dispatcher->DestroyDevice(m_handle);
      if(m_instance) m_dispatcher->DestroyInstance(m_instance);
    }
    m_surface        = nullptr;
    m_handle         = nullptr;
    m_instance       = nullptr;
    m_physicalDevice = nullptr;
    throw;
  }
}

Device::~Device()
{
  // Order is important here.
  try {
    WaitIdle();
  } catch(const std::exception& error) {
    VDLogE("Device teardown could not wait for idle: %s", error.what());
  }

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
  VDVkTry(m_dispatcher->GetPhysicalDeviceSurfaceCapabilitiesKHR(
    m_physicalDevice->GetHandle(), m_surface, &cap));
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
  Arr<CommandBuffer*> cmds, Arr<Fence*> waits, Arr<Fence*> signals,
  Fence* submitFence, SwapchainDep swapchainDep)
{
  if(cmds.size() == 0u) return;
  if(!cmds[0])
    throw std::invalid_argument("Cannot submit a null command buffer");

  const auto  queueType = cmds[0]->GetQueueType();
  const auto& queue     = m_queues[enumValue(queueType)];
  std::scoped_lock queueLock(*queue.mutex);
  std::scoped_lock stateLock(m_resourceStateMutex);

  Arr<VkCommandBufferSubmitInfo> vkCmdInfos;
  Arr<VkSemaphoreSubmitInfo>     vkWaitSemInfos;
  Arr<VkSemaphoreSubmitInfo>     vkSignalSemInfos;

  for(auto& cmd: cmds) {
    if(!cmd)
      throw std::invalid_argument("Cannot submit a null command buffer");
    if(cmd->GetQueueType() != queueType)
      throw std::runtime_error("Command buffer queue mismatch");
    if(&cmd->m_device != this)
      throw std::invalid_argument(
        "Command buffer belongs to a different device");
    if(cmd->GetState() != CommandBuffer::State::Closed)
      throw std::runtime_error(
        "Only closed command buffers can be submitted");

    vkCmdInfos.push_back(
      VkCommandBufferSubmitInfo{
        .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .pNext         = nullptr,
        .commandBuffer = cmd->GetHandle(),
        .deviceMask    = 0});
  }
  validateResourceStates(cmds);

  for(const auto* wait: waits) {
    if(!wait)
      throw std::invalid_argument("Cannot wait on a null fence");
    if(&wait->m_device != this)
      throw std::invalid_argument("Wait fence belongs to a different device");
    if(wait->GetType() == Fence::Type::Fence)
      throw std::invalid_argument("Cannot queue-wait on a Vulkan fence");
  }
  for(const auto* signal: signals) {
    if(!signal)
      throw std::invalid_argument("Cannot signal a null fence");
    if(&signal->m_device != this)
      throw std::invalid_argument(
        "Signal fence belongs to a different device");
    if(signal->GetType() == Fence::Type::Fence)
      throw std::invalid_argument("Cannot queue-signal a Vulkan fence");
    if(std::ranges::count(signals, signal) > 1)
      throw std::invalid_argument("A fence cannot be signaled twice in one submit");
  }
  if(submitFence && &submitFence->m_device != this)
    throw std::invalid_argument(
      "Submit fence belongs to a different device");
  if(submitFence && submitFence->GetType() != Fence::Type::Fence)
    throw std::invalid_argument(
      "Vulkan submit fence must have the fence type");

  for(auto& wait: waits) {
    vkWaitSemInfos.push_back(
      VkSemaphoreSubmitInfo{
        .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .pNext       = nullptr,
        .semaphore   = wait->GetSemaphoreHandle(),
        .value       = wait->GetTarget(),
        .stageMask   = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .deviceIndex = 0});
    VDLogV(
      "- Wait: %s (%llu)", wait->GetName().c_str(), wait->GetTarget());
  }

  if(
    swapchainDep == SwapchainDep::Acquire ||
    swapchainDep == SwapchainDep::AcquireRelease) {
    vkWaitSemInfos.push_back(
      VkSemaphoreSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .pNext = nullptr,
        .semaphore =
          m_swapchain->GetAcquireFence().GetSemaphoreHandle(),
        .value       = 0, // Binary semaphore
        .stageMask   = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .deviceIndex = 0});
  }

  Arr<std::unique_lock<std::mutex>> signalTargetLocks;
  Arr<std::pair<Fence*, u64>>       pendingSignalTargets;
  signalTargetLocks.reserve(signals.size());
  pendingSignalTargets.reserve(signals.size());
  for(auto& signal: signals) {
    u64 signalValue = 0u;
    if(signal->GetType() == Fence::Type::Timeline) {
      signalTargetLocks.emplace_back(signal->m_targetMutex);
      const u64 target = signal->m_target.load();
      if(target == MaxU64)
        throw std::overflow_error("Timeline fence target overflow");
      signalValue = target + 1u;
      pendingSignalTargets.emplace_back(signal, signalValue);
    }

    vkSignalSemInfos.push_back(
      VkSemaphoreSubmitInfo{
        .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .pNext       = nullptr,
        .semaphore   = signal->GetSemaphoreHandle(),
        .value       = signalValue,
        .stageMask   = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .deviceIndex = 0});
  }

  if(
    swapchainDep == SwapchainDep::Release ||
    swapchainDep == SwapchainDep::AcquireRelease) {
    vkSignalSemInfos.push_back(
      VkSemaphoreSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .pNext = nullptr,
        .semaphore =
          m_swapchain->GetReleaseFence().GetSemaphoreHandle(),
        .value       = 0, // Binary semaphore
        .stageMask   = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .deviceIndex = 0});
  }

  VkSubmitInfo2 submitInfo{
    .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
    .pNext                    = nullptr,
    .flags                    = 0,
    .waitSemaphoreInfoCount   = vd::size32(vkWaitSemInfos),
    .pWaitSemaphoreInfos      = std::data(vkWaitSemInfos),
    .commandBufferInfoCount   = vd::size32(vkCmdInfos),
    .pCommandBufferInfos      = std::data(vkCmdInfos),
    .signalSemaphoreInfoCount = vd::size32(vkSignalSemInfos),
    .pSignalSemaphoreInfos    = std::data(vkSignalSemInfos)};

  auto vkSubmitFence = submitFence ? submitFence->GetFenceHandle()
                                   : VK_NULL_HANDLE;

  VDVkTry(m_dispatcher->QueueSubmit2(
    queue.handle, 1u, &submitInfo, vkSubmitFence));

  for(const auto& [fence, target]: pendingSignalTargets)
    fence->m_target.store(target);
  for(auto* cmd: cmds) cmd->commitResourceStates();
}

bool Device::Wait(QueueType queue, Fence& fence) const
{
  if(!isValid(queue))
    throw std::invalid_argument("Queue type is invalid");
  if(&fence.m_device != this)
    throw std::invalid_argument(
      "Wait fence belongs to a different device");
  if(fence.GetType() == Fence::Type::Fence)
    throw std::invalid_argument(
      "A Vulkan queue cannot wait on a host fence");

  const u64 value = fence.GetType() == Fence::Type::Timeline
                      ? fence.GetTarget()
                      : 0u;
  const auto& queueState = m_queues[enumValue(queue)];
  std::scoped_lock queueLock(*queueState.mutex);
  VkSemaphoreSubmitInfo waitInfo{
    .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
    .semaphore   = fence.GetSemaphoreHandle(),
    .value       = value,
    .stageMask   = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    .deviceIndex = 0u};
  VkSubmitInfo2 submitInfo{
    .sType                  = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
    .waitSemaphoreInfoCount = 1u,
    .pWaitSemaphoreInfos    = &waitInfo};
  return m_dispatcher->QueueSubmit2(
           queueState.handle, 1u, &submitInfo, VK_NULL_HANDLE) ==
         VK_SUCCESS;
}

bool Device::Wait(QueueType queue, Fence& fence, u64 value) const
{
  if(!isValid(queue))
    throw std::invalid_argument("Queue type is invalid");
  if(&fence.m_device != this)
    throw std::invalid_argument(
      "Wait fence belongs to a different device");
  if(fence.GetType() != Fence::Type::Timeline)
    throw std::invalid_argument(
      "Explicit queue wait values require a timeline fence");

  const auto& queueState = m_queues[enumValue(queue)];
  std::scoped_lock queueLock(*queueState.mutex);
  VkSemaphoreSubmitInfo waitInfo{
    .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
    .semaphore   = fence.GetSemaphoreHandle(),
    .value       = value,
    .stageMask   = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    .deviceIndex = 0u};
  VkSubmitInfo2 submitInfo{
    .sType                  = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
    .waitSemaphoreInfoCount = 1u,
    .pWaitSemaphoreInfos    = &waitInfo};
  return m_dispatcher->QueueSubmit2(
           queueState.handle, 1u, &submitInfo, VK_NULL_HANDLE) ==
         VK_SUCCESS;
}

bool Device::Signal(QueueType queue, Fence& fence)
{
  if(!isValid(queue))
    throw std::invalid_argument("Queue type is invalid");
  if(&fence.m_device != this)
    throw std::invalid_argument(
      "Signal fence belongs to a different device");
  if(fence.GetType() == Fence::Type::Fence)
    throw std::invalid_argument(
      "A Vulkan queue cannot signal a host fence");

  const auto& queueState = m_queues[enumValue(queue)];
  std::scoped_lock queueLock(*queueState.mutex);
  std::unique_lock targetLock(fence.m_targetMutex, std::defer_lock);
  u64 value = 0u;
  if(fence.GetType() == Fence::Type::Timeline) {
    targetLock.lock();
    const u64 target = fence.m_target.load();
    if(target == MaxU64)
      throw std::overflow_error("Timeline fence target overflow");
    value = target + 1u;
  }

  VkSemaphoreSubmitInfo signalInfo{
    .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
    .semaphore   = fence.GetSemaphoreHandle(),
    .value       = value,
    .stageMask   = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    .deviceIndex = 0u};
  VkSubmitInfo2 submitInfo{
    .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
    .signalSemaphoreInfoCount = 1u,
    .pSignalSemaphoreInfos    = &signalInfo};
  if(
    m_dispatcher->QueueSubmit2(
      queueState.handle, 1u, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
    return false;
  if(fence.GetType() == Fence::Type::Timeline)
    fence.m_target.store(value);
  return true;
}

bool Device::Signal(QueueType queue, Fence& fence, u64 value)
{
  if(!isValid(queue))
    throw std::invalid_argument("Queue type is invalid");
  if(&fence.m_device != this)
    throw std::invalid_argument(
      "Signal fence belongs to a different device");
  if(fence.GetType() != Fence::Type::Timeline)
    throw std::invalid_argument(
      "Explicit queue signal values require a timeline fence");

  const auto& queueState = m_queues[enumValue(queue)];
  std::scoped_lock queueLock(*queueState.mutex);
  std::scoped_lock targetLock(fence.m_targetMutex);
  if(value < fence.m_target.load())
    throw std::invalid_argument(
      "Timeline fence target cannot move backwards");

  VkSemaphoreSubmitInfo signalInfo{
    .sType       = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
    .semaphore   = fence.GetSemaphoreHandle(),
    .value       = value,
    .stageMask   = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    .deviceIndex = 0u};
  VkSubmitInfo2 submitInfo{
    .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
    .signalSemaphoreInfoCount = 1u,
    .pSignalSemaphoreInfos    = &signalInfo};
  if(
    m_dispatcher->QueueSubmit2(
      queueState.handle, 1u, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
    return false;
  fence.m_target.store(value);
  return true;
}

void Device::WaitIdle(QueueType queue)
{
  if(!isValid(queue))
    throw std::invalid_argument("Queue type is invalid");
  const auto& queueState = m_queues[enumValue(queue)];
  std::scoped_lock queueLock(*queueState.mutex);
  VDVkTry(m_dispatcher->QueueWaitIdle(queueState.handle));
}

void Device::WaitIdle()
{
  for(auto queue: QueueTypes) WaitIdle(queue);
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

  { // Debug
    if(desc.dbgEnable) {
      layers.push_back("VK_LAYER_KHRONOS_validation");
      layers.push_back("VK_LAYER_LUNARG_monitor");

      if(desc.dbgDumpLog) layers.push_back("VK_LAYER_LUNARG_api_dump");

      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
  }

  { // Swapchain
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);

#if defined(VD_OS_WINDOWS)
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(VD_OS_LINUX) && defined(VD_WINDOW_WAYLAND)
    extensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
#elif defined(VD_OS_LINUX) && defined(VD_WINDOW_XCB)
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
  instanceCI.pApplicationInfo        = &appInfo;
  instanceCI.enabledLayerCount       = vd::size32(layers);
  instanceCI.ppEnabledLayerNames     = std::data(layers);
  instanceCI.enabledExtensionCount   = vd::size32(extensions);
  instanceCI.ppEnabledExtensionNames = std::data(extensions);

  VDVkTry(m_dispatcher->CreateInstance(&instanceCI, &m_instance));
  m_dispatcher->Load(*this);

  // TODO: re-enable Debug stuff.
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
#elif defined(VD_OS_LINUX) && defined(VD_WINDOW_WAYLAND)
  VkWaylandSurfaceCreateInfoKHR ci{
    .sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
    .pNext   = nullptr,
    .flags   = 0u,
    .display = m_window.display,
    .surface = m_window.surface};
  VDVkTry(m_dispatcher->CreateWaylandSurfaceKHR(&ci, &m_surface));
#elif defined(VD_OS_LINUX) && defined(VD_WINDOW_XCB)
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
    bool hasBestDevice = false;
    u32  bestType      = 0u;
    u64  bestMemory    = 0u;
    for(auto& physicalDevice: physicalDevices) {
      if(!physicalDevice->HasGraphicsCapabilities()) continue;
      if(!physicalDevice->HasPresentCapabilities(m_surface)) continue;
      if(!physicalDevice->HasRequiredFeatures(
           desc.dbgEnable, desc.dbgUseRenderdoc))
        continue;

      const u32 type = getPhysicalDevicePreference(
        physicalDevice->GetProperties()->deviceType);
      const u64 memory = physicalDevice->GetDedicatedMemorySize();
      if(
        hasBestDevice &&
        (type < bestType || (type == bestType && memory <= bestMemory)))
        continue;

      m_physicalDevice = std::move(physicalDevice);
      hasBestDevice    = true;
      bestType         = type;
      bestMemory       = memory;
    }
  }

  if(!m_physicalDevice)
    throw std::runtime_error("No compatible physical device found.");
}

void Device::create_device([[maybe_unused]] const Desc& desc)
{
  PhysicalDevice::Features features;
  features.features.features.samplerAnisotropy =
    m_physicalDevice->GetFeatures()->samplerAnisotropy;
  features.features.features.depthClamp =
    m_physicalDevice->GetFeatures()->depthClamp;
  features.features.features.depthBounds =
    m_physicalDevice->GetFeatures()->depthBounds;
  features.features.features.fillModeNonSolid =
    m_physicalDevice->GetFeatures()->fillModeNonSolid;
  features.features.features.wideLines =
    m_physicalDevice->GetFeatures()->wideLines;
  features.features.features.alphaToOne =
    m_physicalDevice->GetFeatures()->alphaToOne;
  features.features.features.logicOp =
    m_physicalDevice->GetFeatures()->logicOp;
  features.timeline.timelineSemaphore        = VK_TRUE;
  features.synchronization2.synchronization2 = VK_TRUE;
  features.dynamicRendering.dynamicRendering = VK_TRUE;
  features.descriptorIndexing
    .shaderSampledImageArrayNonUniformIndexing       = VK_TRUE;
  features.descriptorIndexing.runtimeDescriptorArray = VK_TRUE;
  features.descriptorIndexing.descriptorBindingVariableDescriptorCount =
    VK_TRUE;
  features.descriptorIndexing.descriptorBindingPartiallyBound = VK_TRUE;
  features.descriptorIndexing
    .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
  features.descriptorIndexing
    .descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
  features.descriptorIndexing
    .descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;

  if(m_desc.dbgUseRenderdoc) {
    features.bufferDeviceAddress.bufferDeviceAddressCaptureReplay =
      VK_TRUE;
  }

  Arr<const char*> extensions;
  extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);

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

  for(u32 queueIdx = 0u; queueIdx < QueueTypeCount; ++queueIdx) {
    auto& queue = m_queues[queueIdx];
    auto  name  = formatString(
      "%s queue %u", queueTypeStr(queueIdx),
      queueInfo.indices[queueIdx]);

    queue.name   = name;
    queue.family = queueInfo.families[queueIdx];
    queue.index  = queueInfo.indices[queueIdx];
    queue.mutex  = std::make_shared<std::recursive_mutex>();
    for(u32 previousIdx = 0u; previousIdx < queueIdx; ++previousIdx) {
      const auto& previous = m_queues[previousIdx];
      if(
        previous.family == queue.family &&
        previous.index == queue.index) {
        queue.mutex = previous.mutex;
        break;
      }
    }
    m_dispatcher->GetDeviceQueue(
      queue.family, queue.index, &queue.handle);
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
        layer.layerName, &layerExtensionCount, nullptr));
      Arr<VkExtensionProperties> layerExtensions(layerExtensionCount);
      VDVkTry(vk.EnumerateInstanceExtensionProperties(
        layer.layerName, &layerExtensionCount, layerExtensions.data()));
      layerExtensions.resize(layerExtensionCount);
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

  // Prefer dedicated families. Some GPUs expose one queue per family, so
  // consuming a compute+transfer family for transfer could leave compute
  // unassigned.
  for(u32 familyIdx{0u};
      familyIdx < physicalDevice.GetQueueFamilyCount(); familyIdx++) {
    if(
      !graphicsFamily && physicalDevice.HasGraphics(familyIdx) &&
      physicalDevice.HasPresent(familyIdx, surface)) {
      graphicsFamily = familyIdx;
    }

    if(
      physicalDevice.HasCompute(familyIdx) &&
      !physicalDevice.HasGraphics(familyIdx)) {
      computeFamily = familyIdx;
    }

  }

  if(!graphicsFamily)
    throw std::runtime_error("No graphics queue family found");

  if(!computeFamily) {
    for(u32 familyIdx{0u};
        familyIdx < physicalDevice.GetQueueFamilyCount(); familyIdx++) {
      if(physicalDevice.HasCompute(familyIdx)) {
        computeFamily = familyIdx;
        break;
      }
    }
  }
  if(!computeFamily) computeFamily = graphicsFamily;

  // Uploads may transition a resource from a graphics state to CopyDst.
  // Keep the copy role on a graphics-capable family so those stages are
  // valid and no graphics-to-copy ownership transfer is needed. A separate
  // queue index is still used when the family has one.
  const u32 transferFamily = *graphicsFamily;

  info.families[enumValue(QueueType::Graphics)] = *graphicsFamily;
  info.families[enumValue(QueueType::Compute)]  = *computeFamily;
  info.families[enumValue(QueueType::Copy)]     = transferFamily;

  // Allocate queue indices per family without exceeding queueCount.
  // If a family is oversubscribed, share index 0.
  Arr<u32> nextIndex(physicalDevice.GetQueueFamilyCount(), 0u);
  for(u32 role = 0u; role < QueueTypeCount; ++role) {
    const u32 family    = info.families[role];
    const u32 available = physicalDevice.GetQueueFamily(family)
                            .queueFamilyProperties.queueCount;
    if(nextIndex[family] < available) {
      info.indices[role] = nextIndex[family]++;
    } else {
      info.indices[role] = 0u;
    }
  }

  // One DeviceQueueCreateInfo per unique family, capped to available.
  Arr<bool> seen(physicalDevice.GetQueueFamilyCount(), false);
  for(u32 role = 0u; role < QueueTypeCount; ++role) {
    const u32 family = info.families[role];
    if(seen[family]) continue;
    seen[family] = true;

    const u32 available = physicalDevice.GetQueueFamily(family)
                            .queueFamilyProperties.queueCount;
    u32 needed = 0u;
    for(u32 other = 0u; other < QueueTypeCount; ++other) {
      if(info.families[other] == family)
        needed = std::max(needed, info.indices[other] + 1u);
    }

    VkDeviceQueueCreateInfo queueCI{};
    queueCI.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCI.queueFamilyIndex = family;
    queueCI.queueCount       = std::min(needed, available);
    queueCI.pQueuePriorities = info.priorities.data();
    info.CIs.push_back(queueCI);
  }

  return info;
}

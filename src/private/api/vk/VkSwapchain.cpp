#include "vuldir/api/Device.hpp"
#include "vuldir/api/Fence.hpp"
#include "vuldir/api/PhysicalDevice.hpp"
#include "vuldir/api/Swapchain.hpp"
#include "vuldir/api/vk/VkUti.hpp"

#include <limits>

using namespace vd;

Swapchain::Swapchain(Device& device, const Desc& desc):
  m_device{device},
  m_desc{desc},
  m_images{},
  m_extent{},
  m_imageCount{},
  m_imageIndex{0u},
  m_frameIndex{0u},
  m_needsRecreate{false},
  m_acquireFences{},
  m_releaseFences{},
  m_waitFence{},
  m_handle{}
{
  m_desc.maxFramesInFlight = std::max(1u, m_desc.maxFramesInFlight);
  try {
    create();
  } catch(...) {
    // A throwing constructor does not run Swapchain::~Swapchain.
    destroy();
    throw;
  }
}

Swapchain::~Swapchain() { destroy(); }

void Swapchain::Resize(Opt<UInt2> size)
{
  m_desc.size     = size;
  m_needsRecreate = false;
  create();
}

bool Swapchain::IsSurfaceExtentStale() const
{
  const auto    capabilities    = m_device.GetSurfaceCapabilities();
  constexpr u32 kExtentFlexible = std::numeric_limits<uint32_t>::max();
  if(capabilities.currentExtent.width == kExtentFlexible) return false;
  return capabilities.currentExtent.width != m_extent[0] ||
         capabilities.currentExtent.height != m_extent[1];
}

Image* Swapchain::AcquireNextImage(bool wait)
{
  auto waitFence = wait ? m_waitFence->GetFenceHandle() : nullptr;

  const VkResult result = m_device.api().AcquireNextImageKHR(
    m_handle, MaxU64, GetAcquireFence().GetSemaphoreHandle(), waitFence,
    &m_imageIndex);

  if(result == VK_ERROR_OUT_OF_DATE_KHR) {
    m_needsRecreate = true;
    return nullptr;
  }

  if(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    VDVkTry(result);
  }

  if(result == VK_SUBOPTIMAL_KHR) { m_needsRecreate = true; }

  if(wait) {
    if(!m_waitFence->Wait(MaxU64))
      throw std::runtime_error("Failed to wait for acquired image");
    m_waitFence->Reset();
  }

  return m_images[m_imageIndex].get();
}

u32 vd::Swapchain::NextFrame()
{
  m_frameIndex = (m_frameIndex + 1u) % m_desc.maxFramesInFlight;
  return m_frameIndex;
}

void Swapchain::Present()
{
  VkSemaphore waitHandle = GetReleaseFence().GetSemaphoreHandle();
  VkResult    result     = VK_SUCCESS;

  VkPresentInfoKHR info{
    .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .pNext              = nullptr,
    .waitSemaphoreCount = 1u,
    .pWaitSemaphores    = &waitHandle,
    .swapchainCount     = 1u,
    .pSwapchains        = &m_handle,
    .pImageIndices      = &m_imageIndex,
    .pResults           = &result};

  const auto& queue =
    m_device.m_queues[enumValue(QueueType::Graphics)];
  std::scoped_lock queueLock(*queue.mutex);
  const VkResult presentResult =
    m_device.api().QueuePresentKHR(queue.handle, &info);

  if(
    presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
    presentResult == VK_SUBOPTIMAL_KHR ||
    result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    m_needsRecreate = true;
    return;
  }

  if(presentResult != VK_SUCCESS) { VDVkTry(presentResult); }
  if(result != VK_SUCCESS) { VDVkTry(result); }
}

void Swapchain::create()
{
  const auto surface      = m_device.GetSurface();
  const auto capabilities = m_device.GetSurfaceCapabilities();
  const auto familyIndex = m_device.GetQueueFamily(QueueType::Graphics);
  const auto availableModes =
    m_device.GetPhysicalDevice().GetPresentModes(surface);
  const auto minImageCount = capabilities.maxImageCount == 0u
                               ? std::max(
                                   m_desc.minImageCount,
                                   capabilities.minImageCount)
                               : vd::clamp(
                                   m_desc.minImageCount,
                                   capabilities.minImageCount,
                                   capabilities.maxImageCount);

  // A fixed currentExtent is mandatory: images that don't match the
  // surface size corrupt presentation.
  constexpr u32 kExtentFlexible = std::numeric_limits<uint32_t>::max();
  if(capabilities.currentExtent.width != kExtentFlexible) {
    m_extent[0] = capabilities.currentExtent.width;
    m_extent[1] = capabilities.currentExtent.height;
  } else {
    // Surface leaves the size to us (Wayland). Use the requested size, else
    // keep the current extent: minImageExtent is 1x1 and would collapse
    // the swapchain on a Resize() with no explicit size.
    UInt2 requested{
      capabilities.minImageExtent.width,
      capabilities.minImageExtent.height};
    if(m_desc.size) requested = *m_desc.size;
    else if(m_extent[0] != 0u && m_extent[1] != 0u)
      requested = m_extent;

    m_extent[0] = vd::clamp(
      requested[0], capabilities.minImageExtent.width,
      capabilities.maxImageExtent.width);
    m_extent[1] = vd::clamp(
      requested[1], capabilities.minImageExtent.height,
      capabilities.maxImageExtent.height);
  }

  VDLogV(
    "Swapchain create: currentExtent=%ux%u descSize=%s -> using %ux%u "
    "(min=%ux%u max=%ux%u)",
    capabilities.currentExtent.width, capabilities.currentExtent.height,
    m_desc.size ? "set" : "nullopt", m_extent[0], m_extent[1],
    capabilities.minImageExtent.width,
    capabilities.minImageExtent.height,
    capabilities.maxImageExtent.width,
    capabilities.maxImageExtent.height);

  // Minimized / zero-sized surfaces cannot be presented.
  if(m_extent[0] == 0u || m_extent[1] == 0u) {
    m_needsRecreate = true;
    return;
  }

  VkSwapchainCreateInfoKHR swapchainCI{};

  { // Populate the create info.
    swapchainCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCI.pNext = nullptr;
    swapchainCI.flags = 0;
    swapchainCI.minImageCount      = minImageCount;
    swapchainCI.imageFormat        = convert(m_desc.format);
    swapchainCI.imageColorSpace    = convert(m_desc.colorSpace);
    swapchainCI.imageExtent.width  = m_extent[0];
    swapchainCI.imageExtent.height = m_extent[1];
    swapchainCI.imageArrayLayers   = 1u;
    swapchainCI.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainCI.queueFamilyIndexCount = 1u;
    swapchainCI.pQueueFamilyIndices   = &familyIndex;
    swapchainCI.preTransform =
      vd::hasFlag(
        capabilities.supportedTransforms,
        VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
        : capabilities.currentTransform;

    constexpr VkCompositeAlphaFlagBitsKHR alphaModes[] = {
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
    for(const auto alphaMode: alphaModes) {
      if(vd::hasFlag(capabilities.supportedCompositeAlpha, alphaMode)) {
        swapchainCI.compositeAlpha = alphaMode;
        break;
      }
    }
    if(swapchainCI.compositeAlpha == 0u)
      throw std::runtime_error(
        "Surface exposes no supported composite-alpha mode");
    swapchainCI.clipped        = VK_TRUE;
    swapchainCI.surface        = surface;
    swapchainCI.oldSwapchain   = m_handle;

    // FIFO is always valid. Prefer MAILBOX when unlocked and available.
    swapchainCI.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if(
      !m_desc.vsync &&
      std::ranges::find(availableModes, VK_PRESENT_MODE_MAILBOX_KHR) !=
        availableModes.end()) {
      swapchainCI.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    }
  }

  { // Validate the selected surface configuration in every build type.
    if(
      capabilities.currentExtent.width !=
      std::numeric_limits<uint32_t>::max()) {
      if(
        capabilities.currentExtent.width <
          capabilities.minImageExtent.width ||
        capabilities.currentExtent.width >
          capabilities.maxImageExtent.width ||
        capabilities.currentExtent.height <
          capabilities.minImageExtent.height ||
        capabilities.currentExtent.height >
          capabilities.maxImageExtent.height)
        throw std::runtime_error(
          "Surface current extent is outside its advertised limits");
    }

    if(
      !m_device.GetPhysicalDevice().IsPresentModeSupported(
        surface, swapchainCI.presentMode))
      throw std::runtime_error("Selected present mode is unsupported");
    if(
      !m_device.GetPhysicalDevice().IsPresentFormatSupported(
        surface,
        {swapchainCI.imageFormat, swapchainCI.imageColorSpace}))
      throw std::runtime_error("Selected present format is unsupported");
    if(
      swapchainCI.minImageCount < capabilities.minImageCount ||
      (capabilities.maxImageCount != 0u &&
       swapchainCI.minImageCount > capabilities.maxImageCount))
      throw std::runtime_error("Selected swapchain image count is unsupported");
  }

  // Finish all GPU work before retiring the old swapchain / semaphores.
  if(m_handle) { m_device.WaitIdle(); }

  { // Create new swapchain and throw away the old resources if needed.
    VkSwapchainKHR newHandle;
    VDVkTry(
      m_device.api().CreateSwapchainKHR(&swapchainCI, &newHandle));

    // destroy() clears m_handle; keep the new one.
    const auto oldHandle = m_handle;
    m_handle             = nullptr;
    destroy();
    if(oldHandle) { m_device.api().DestroySwapchainKHR(oldHandle); }
    m_handle = newHandle;
  }

  { // Create the new resources.
    VDVkTry(m_device.api().GetSwapchainImagesKHR(
      m_handle, &m_imageCount, nullptr));

    // Acquire semaphores: one per frame-in-flight.
    // Release/present semaphores: one per swapchain image.
    for(u32 idx = 0u; idx < m_desc.maxFramesInFlight; ++idx) {
      m_acquireFences.push_back(
        std::make_unique<Fence>(
          m_device, formatString("SwapchainAcquire #%u", idx),
          Fence::Type::Binary));
    }
    for(u32 idx = 0u; idx < m_imageCount; ++idx) {
      m_releaseFences.push_back(
        std::make_unique<Fence>(
          m_device, formatString("SwapchainRelease #%u", idx),
          Fence::Type::Binary));
    }
    m_waitFence = std::make_unique<Fence>(
      m_device, "SwapchainWait", Fence::Type::Fence);

    Arr<VkImage> vkImages;
    vkImages.resize(m_imageCount);
    VDVkTry(m_device.api().GetSwapchainImagesKHR(
      m_handle, &m_imageCount, std::data(vkImages)));

    m_images.reserve(m_imageCount);
    for(const auto& imgHandle: vkImages) {
      Image::Desc imgDesc{
        .name        = "Swapchain Image",
        .usage       = ResourceUsage::RenderTarget,
        .format      = m_desc.format,
        .dimension   = Dimension::e2D,
        .extent      = m_extent,
        .defaultView = ViewType::RTV,
        .handle      = imgHandle};

      auto image = std::make_unique<Image>(m_device, imgDesc);
      m_images.push_back(std::move(image));
    }
  }

  m_imageIndex    = 0u;
  m_frameIndex    = 0u;
  m_needsRecreate = false;
}

void Swapchain::destroy()
{
  m_images.clear();

  // Caller must have waited idle if the swapchain/semaphores may still
  // be referenced by in-flight presents.
  if(m_handle) {
    m_device.api().DestroySwapchainKHR(m_handle);
    m_handle = nullptr;
  }

  m_acquireFences.clear();
  m_releaseFences.clear();
  m_waitFence.reset();
}

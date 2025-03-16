#include "vuldir/api/Device.hpp"
#include "vuldir/api/Fence.hpp"
#include "vuldir/api/PhysicalDevice.hpp"
#include "vuldir/api/Swapchain.hpp"
#include "vuldir/api/vk/VkUti.hpp"

using namespace vd;

Swapchain::Swapchain(Device& device, const Desc& desc):
  m_device{device},
  m_desc{desc},
  m_images{},
  m_extent{},
  m_imageCount{},
  m_imageIndex{0u},
  m_frameIndex{0u},
  m_acquireFences{},
  m_releaseFences{},
  m_waitFence{},
  m_handle{}
{
  create();
}

Swapchain::~Swapchain() { destroy(); }

void Swapchain::Resize(Opt<UInt2> size)
{
  m_desc.size = size;
  create();
}

Image& Swapchain::AcquireNextImage(bool wait)
{
  auto waitFence = wait ? m_waitFence->GetFenceHandle() : nullptr;

  ///VDLogV(
  ///  "Acquiring next swapchain image (wait: %s)",
  ///  wait ? "true" : "false");
  ///VDLogV("- Signals %s", GetAcquireFence().GetName().c_str());
  ///if(waitFence) {
  ///  VDLogV("- Signals %s", m_waitFence->GetName().c_str());
  ///}

  VDVkTry(m_device.api().AcquireNextImageKHR(
    m_handle, MaxU64, GetAcquireFence().GetSemaphoreHandle(), waitFence,
    &m_imageIndex));

  //VDLogV("Acquired image index %u", m_imageIndex);

  if(wait) {
    m_waitFence->Wait(MaxU64);
    m_waitFence->Reset();
  }

  return *m_images[m_imageIndex];
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

  //VDLogV("Presenting swapchain image");
  //VDLogV("- Waits %s", GetReleaseFence().GetName().c_str());

  VDVkTry(m_device.api().QueuePresentKHR(
    m_device.GetQueueHandle(QueueType::Graphics), &info));
}

void Swapchain::create()
{
  const auto surface      = m_device.GetSurface();
  const auto capabilities = m_device.GetSurfaceCapabilities();
  const auto familyIndex = m_device.GetQueueFamily(QueueType::Graphics);
  const auto availableModes =
    m_device.GetPhysicalDevice().GetPresentModes(surface);
  const auto minImageCount = vd::clamp(
    m_desc.minImageCount, capabilities.minImageCount,
    capabilities.maxImageCount);

  m_extent[0] = capabilities.currentExtent.width;
  m_extent[1] = capabilities.currentExtent.height;

  if(m_desc.size) { m_extent = *m_desc.size; }

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
    swapchainCI.preTransform   = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchainCI.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainCI.clipped        = VK_TRUE;
    swapchainCI.surface        = surface;
    swapchainCI.oldSwapchain   = m_handle;

    if(m_device.GetPhysicalDevice().IsPresentModeSupported(
         m_device.GetSurface(), VK_PRESENT_MODE_MAILBOX_KHR)) {
      swapchainCI.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    } else if(m_device.GetPhysicalDevice().IsPresentModeSupported(
                m_device.GetSurface(), VK_PRESENT_MODE_FIFO_KHR)) {
      swapchainCI.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    } else {
      swapchainCI.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    }
  }

  { // Sanity checks
    if(
      capabilities.currentExtent.width !=
      std::numeric_limits<uint32_t>::max()) {
      VDAssertMsg(
        capabilities.currentExtent.width >=
          capabilities.minImageExtent.width,
        "Present width too small.");
      VDAssertMsg(
        capabilities.currentExtent.width <=
          capabilities.maxImageExtent.width,
        "Present width too big.");
      VDAssertMsg(
        capabilities.currentExtent.height >=
          capabilities.minImageExtent.height,
        "Present height too small.");
      VDAssertMsg(
        capabilities.currentExtent.height <=
          capabilities.maxImageExtent.height,
        "Present height too big.");
    }

    VDAssertMsg(
      m_device.GetPhysicalDevice().IsPresentModeSupported(
        surface, swapchainCI.presentMode),
      "Present mode not supported.");
    VDAssertMsg(
      m_device.GetPhysicalDevice().IsPresentFormatSupported(
        surface,
        {swapchainCI.imageFormat, swapchainCI.imageColorSpace}),
      "Present format not supported.");
    VDAssertMsg(
      capabilities.minImageCount == 0 ||
        swapchainCI.minImageCount >= capabilities.minImageCount,
      "Present image count not supported.");
    VDAssertMsg(
      capabilities.maxImageCount == 0 ||
        swapchainCI.minImageCount <= capabilities.maxImageCount,
      "Present image count not supported.");
  }

  // If there is an existing swapchain, wait for the gpu to be idle.
  if(m_handle) { m_device.WaitIdle(QueueType::Graphics); }

  { // Create new swapchain and throw away the old resources if needed.
    VkSwapchainKHR newHandle;
    VDVkTry(
      m_device.api().CreateSwapchainKHR(&swapchainCI, &newHandle));

    destroy();
    m_handle = newHandle;
  }

  { // Create the new resources.
    VDVkTry(m_device.api().GetSwapchainImagesKHR(
      m_handle, &m_imageCount, nullptr));

    for(u32 idx = 0u; idx < m_imageCount; ++idx) {
      m_acquireFences.push_back(std::make_unique<Fence>(
        m_device, formatString("SwapchainAcquire #%u", idx),
        Fence::Type::Binary));
      m_releaseFences.push_back(std::make_unique<Fence>(
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
}

void Swapchain::destroy()
{
  m_images.clear();

  if(m_handle) {
    m_device.api().DestroySwapchainKHR(m_handle);
    m_handle = nullptr;
  }

  m_acquireFences.clear();
  m_releaseFences.clear();
  m_waitFence.reset();
}

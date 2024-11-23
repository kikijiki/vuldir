#pragma once

#include "vuldir/api/Common.hpp"
#include "vuldir/api/Fence.hpp"
#include "vuldir/api/Image.hpp"

namespace vd {

class Device;
class Fence;

class Swapchain
{
public:
  enum class Mode {
    Windowed,
    BorderlessFullScreen,
    ExclusiveFullscreen,
  };

  struct Desc {
    WindowHandle window;
    Opt<UInt2>   size;

    Mode       mode              = Mode::Windowed;
    Format     format            = Format::B8G8R8A8_UNORM;
    ColorSpace colorSpace        = ColorSpace::SRGB_NONLINEAR;
    u32        minImageCount     = 2u;
    u32        maxFramesInFlight = 2u;

    bool vsync = false;
  };

public:
  Swapchain(Device& device, const Desc& desc);
  ~Swapchain();

public:
  UInt2  GetExtent() const { return m_extent; }
  Float2 GetExtentF() const
  {
    return {
      static_cast<f32>(m_extent[0]), static_cast<f32>(m_extent[1])};
  }

  Format GetFormat() const { return m_desc.format; }
  void   Resize(Opt<UInt2> size = {});

  Fence& GetAcquireFence() { return *m_acquireFences[m_frameIndex]; }
  Fence& GetReleaseFence() { return *m_releaseFences[m_frameIndex]; }

  u32 GetImageIndex() const { return m_imageIndex; }
  u32 GetFrameIndex() const { return m_frameIndex; }
  u32 GetImageCount() const { return m_imageCount; }

  const Image::View*
  GetView(u32 idx, ViewType type = ViewType::RTV) const
  {
    return m_images[idx]->GetView(type);
  }
  Image& AcquireNextImage(bool wait = false);
  u32    NextFrame();
  void   Present();

private:
  void create();
  void destroy();

private:
  Device& m_device;
  Desc    m_desc;

  Arr<UPtr<Image>> m_images;

  UInt2 m_extent;
  u32   m_imageCount;
  u32   m_imageIndex;
  u32   m_frameIndex;

  Arr<UPtr<Fence>> m_acquireFences;
  Arr<UPtr<Fence>> m_releaseFences;
  UPtr<Fence>      m_waitFence;

#ifdef VD_API_VK
public:
  VkSwapchainKHR GetHandle() const { return m_handle; }

private:
  VkSwapchainKHR m_handle;
#endif

#ifdef VD_API_DX
public:
  IDXGISwapChain4* GetHandle() const { return m_handle.Get(); }

private:
  ComPtr<IDXGISwapChain4> m_handle;

  UINT m_swapchainFlags;
  UINT m_presentFlags;
#endif
};

} // namespace vd

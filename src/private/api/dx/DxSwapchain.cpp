#include "vuldir/api/Device.hpp"
#include "vuldir/api/Swapchain.hpp"
#include "vuldir/api/dx/DxUti.hpp"

using namespace vd;

static bool hasTearingSupport()
{
  ComPtr<IDXGIFactory4> factory4;
  if(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4)))) {
    ComPtr<IDXGIFactory5> factory5;
    if(SUCCEEDED(factory4.As(&factory5))) {
      bool allowTearing = false;
      if(SUCCEEDED(factory5->CheckFeatureSupport(
           DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing,
           sizeof(allowTearing)))) {
        return allowTearing;
      }
    }
  }

  return false;
}

static UInt2 getSize(HWND hWnd, Opt<UInt2> requestedSize)
{
  if(requestedSize) { return *requestedSize; }

  RECT clientRect;
  GetClientRect(hWnd, &clientRect);
  return {
    static_cast<uint32_t>(clientRect.right - clientRect.left),
    static_cast<uint32_t>(clientRect.bottom - clientRect.top)};
}

Swapchain::Swapchain(Device& device, const Desc& desc):
  m_device{device},
  m_desc{desc},
  m_images{},
  m_extent{},
  m_imageCount{},
  m_imageIndex{},
  m_frameIndex{},
  m_acquireFences{},
  m_releaseFences{},
  m_handle{},
  m_swapchainFlags{},
  m_presentFlags{}
{
  create();
}

Swapchain::~Swapchain() { destroy(); }

void Swapchain::Resize(Opt<UInt2> size)
{
  m_desc.size = size;

  for(auto& fence: m_acquireFences) { fence->Wait(); }

  m_images.clear();
  m_acquireFences.clear();
  m_releaseFences.clear();

  auto newSize = getSize(m_desc.window.hWnd, size);
  m_handle->ResizeBuffers(
    m_imageCount, newSize[0], newSize[1], vd::convert(m_desc.format),
    hasTearingSupport() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);

  DXGI_SWAP_CHAIN_DESC1 desc;
  VDDxTry(m_handle->GetDesc1(&desc));
  m_extent[0] = desc.Width;
  m_extent[1] = desc.Height;

  for(auto idx = 0u; idx < m_imageCount; ++idx) {
    Image::Desc imgDesc{
      .name        = formatString("Swapchain Image #%u", idx),
      .format      = m_desc.format,
      .dimension   = Dimension::e2D,
      .extent      = m_extent,
      .defaultView = ViewType::RTV};

    m_handle->GetBuffer(idx, IID_PPV_ARGS(&imgDesc.handle));
    auto image = std::make_unique<Image>(m_device, imgDesc);
    m_images.push_back(std::move(image));
  }

  for(u32 idx = 0u; idx < m_imageCount; ++idx) {
    m_acquireFences.push_back(std::make_unique<Fence>(
      m_device, formatString("Swapchain Acquire Fence #%u", idx),
      Fence::Type::Timeline));
    m_releaseFences.push_back(std::make_unique<Fence>(
      m_device, formatString("Swapchain Release Fence #%u", idx),
      Fence::Type::Timeline));
  }
}

Image& Swapchain::AcquireNextImage(bool wait)
{
  if(wait) { GetAcquireFence().Wait(); }

  m_imageIndex = m_handle->GetCurrentBackBufferIndex();
  GetReleaseFence().Step();
  return *m_images[m_imageIndex];
}

u32 Swapchain::NextFrame()
{
  m_frameIndex = (m_frameIndex + 1u) % m_desc.maxFramesInFlight;
  return m_frameIndex;
}

void Swapchain::Present()
{
  m_handle->Present(m_desc.vsync ? 1 : 0, m_presentFlags);

  auto& cmd = m_device.GetQueueHandle(QueueType::Graphics);
  GetAcquireFence().Step();
  cmd.Signal(
    &GetAcquireFence().GetHandle(), GetAcquireFence().GetTarget());
}

void Swapchain::create()
{
  destroy();

  if(hasTearingSupport()) {
    m_swapchainFlags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    m_presentFlags   = DXGI_PRESENT_ALLOW_TEARING;
  }

  UINT createFactoryFlags = 0;
  if(m_device.IsDebugEnabled())
    createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;

  ComPtr<IDXGIFactory4> dxgiFactory4;
  VDDxTry(CreateDXGIFactory2(
    createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));

  DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};

  auto size                 = getSize(m_desc.window.hWnd, m_desc.size);
  swapChainDesc.Width       = size[0];
  swapChainDesc.Height      = size[1];
  swapChainDesc.Format      = convert(m_desc.format);
  swapChainDesc.Stereo      = FALSE;
  swapChainDesc.SampleDesc  = {1, 0};
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.BufferCount = m_desc.minImageCount;
  swapChainDesc.Scaling     = DXGI_SCALING_STRETCH;
  swapChainDesc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.AlphaMode   = DXGI_ALPHA_MODE_UNSPECIFIED;
  swapChainDesc.Flags       = m_swapchainFlags;

  ComPtr<IDXGISwapChain1> swapChain;
  VDDxTry(dxgiFactory4->CreateSwapChainForHwnd(
    &m_device.GetQueueHandle(QueueType::Graphics), m_desc.window.hWnd,
    &swapChainDesc, nullptr, nullptr, &swapChain));

  // TODO: allow Alt+Enter fullscreen toggle.
  VDDxTry(dxgiFactory4->MakeWindowAssociation(
    m_desc.window.hWnd, DXGI_MWA_NO_ALT_ENTER));
  VDDxTry(swapChain.As(&m_handle));

  VDDxTry(m_handle->GetDesc1(&swapChainDesc));
  m_imageCount = swapChainDesc.BufferCount;

  Resize(m_desc.size);
  //m_handle->Release(); // GetBuffer adds 1 the first time?
}

void Swapchain::destroy()
{
  m_acquireFences.clear();
  m_releaseFences.clear();

  m_images.clear();
  m_handle = nullptr;
}

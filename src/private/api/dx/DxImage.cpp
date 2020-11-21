#include "vuldir/api/Binder.hpp"
#include "vuldir/api/Device.hpp"
#include "vuldir/api/Image.hpp"
#include "vuldir/api/dx/DxUti.hpp"

using namespace vd;

static D3D12_RESOURCE_DESC makeResourceDesc(const Image::Desc& desc)
{
  D3D12_RESOURCE_DESC res{};

  res.Alignment          = 0u; // Default, 64KB?
  res.Format             = convert(desc.format);
  res.Width              = std::max(1u, desc.extent[0]);
  res.Height             = toU16(std::max(1u, desc.extent[1]));
  res.DepthOrArraySize   = toU16(std::max(1u, desc.extent[2]));
  res.MipLevels          = toU16(desc.mips);
  res.SampleDesc.Count   = toU16(desc.samples);
  res.SampleDesc.Quality = 0u; // TODO
  res.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  res.Flags              = D3D12_RESOURCE_FLAG_NONE;

  if(desc.usage.IsSet(ResourceUsage::RenderTarget))
    res.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  if(desc.usage.IsSet(ResourceUsage::DepthStencil))
    res.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  if(desc.usage.IsSet(ResourceUsage::UnorderedAccess))
    res.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

  switch(desc.dimension) {
    case Dimension::e1D:
      res.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
      break;
    case Dimension::e2D:
      res.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      break;
    case Dimension::e3D:
      res.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
      break;
    case Dimension::eCube:
      res.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      break;
  }

  return res;
}

static D3D12_SHADER_RESOURCE_VIEW_DESC
makeSrvDesc(const ViewRange& range, Format format, Dimension dimension)
{
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};

  srv.Format = convert(format);
  srv.Shader4ComponentMapping =
    D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

  switch(dimension) {
    case Dimension::e1D:
      if(range.layerCount > 1u) {
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
        srv.Texture1DArray.MostDetailedMip = range.mipOffset;
        srv.Texture1DArray.MipLevels       = range.mipCount;
        srv.Texture1DArray.FirstArraySlice = range.layerOffset;
        srv.Texture1DArray.ArraySize       = range.layerCount;
      } else {
        srv.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE1D;
        srv.Texture1D.MostDetailedMip = range.mipOffset;
        srv.Texture1D.MipLevels       = range.mipCount;
      }
      break;
    case Dimension::e2D:
      if(range.layerCount > 1u) {
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srv.Texture2DArray.MostDetailedMip = range.mipOffset;
        srv.Texture2DArray.MipLevels       = range.mipCount;
        srv.Texture2DArray.FirstArraySlice = range.layerOffset;
        srv.Texture2DArray.ArraySize       = range.layerCount;
      } else {
        srv.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MostDetailedMip = range.mipOffset;
        srv.Texture2D.MipLevels       = range.mipCount;
      }
      break;
    case Dimension::e3D:
      srv.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE3D;
      srv.Texture3D.MostDetailedMip = range.mipOffset;
      srv.Texture3D.MipLevels       = range.mipCount;
      break;
    case Dimension::eCube:
      if(range.layerCount > 1u) {
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
        srv.Texture2DArray.MostDetailedMip = range.mipOffset;
        srv.Texture2DArray.MipLevels       = range.mipCount;
        srv.Texture2DArray.FirstArraySlice = range.layerOffset;
        srv.Texture2DArray.ArraySize       = range.layerCount;
      } else {
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srv.TextureCube.MostDetailedMip = range.mipOffset;
        srv.TextureCube.MipLevels       = range.mipCount;
      }
      break;
    default:
      throw std::runtime_error("Invalid view dimension for SRV");
  }

  return srv;
}

static D3D12_UNORDERED_ACCESS_VIEW_DESC
makeUavDesc(const ViewRange& range, Format format, Dimension dimension)
{
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};

  uav.Format = convert(format);

  switch(dimension) {
    case Dimension::e1D:
      if(range.layerCount > 1u) {
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
        uav.Texture1DArray.MipSlice        = range.mipOffset;
        uav.Texture1DArray.FirstArraySlice = range.layerOffset;
        uav.Texture1DArray.ArraySize       = range.layerCount;
      } else {
        uav.ViewDimension      = D3D12_UAV_DIMENSION_TEXTURE1D;
        uav.Texture1D.MipSlice = range.mipOffset;
      }
      break;
    case Dimension::e2D:
      if(range.layerCount > 1u) {
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uav.Texture2DArray.MipSlice        = range.mipOffset;
        uav.Texture2DArray.FirstArraySlice = range.layerOffset;
        uav.Texture2DArray.ArraySize       = range.layerCount;
      } else {
        uav.ViewDimension      = D3D12_UAV_DIMENSION_TEXTURE2D;
        uav.Texture2D.MipSlice = range.mipOffset;
      }
      break;
    case Dimension::e3D:
      uav.ViewDimension      = D3D12_UAV_DIMENSION_TEXTURE3D;
      uav.Texture3D.MipSlice = range.mipOffset;
      break;
    default:
      throw std::runtime_error("Invalid view dimension for UAV");
  }

  return uav;
}

static D3D12_RENDER_TARGET_VIEW_DESC
makeRtvDesc(const ViewRange& range, Format format, Dimension dimension)
{
  D3D12_RENDER_TARGET_VIEW_DESC rtv{};

  rtv.Format = convert(format);

  switch(dimension) {
    case Dimension::e1D:
      if(range.layerCount > 1u) {
        rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
        rtv.Texture1DArray.MipSlice        = range.mipOffset;
        rtv.Texture1DArray.FirstArraySlice = range.layerOffset;
        rtv.Texture1DArray.ArraySize       = range.layerCount;
      } else {
        rtv.ViewDimension      = D3D12_RTV_DIMENSION_TEXTURE1D;
        rtv.Texture1D.MipSlice = range.mipOffset;
      }
      break;
    case Dimension::e2D:
      if(range.layerCount > 1u) {
        rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        rtv.Texture2DArray.MipSlice        = range.mipOffset;
        rtv.Texture2DArray.FirstArraySlice = range.layerOffset;
        rtv.Texture2DArray.ArraySize       = range.layerCount;
      } else {
        rtv.ViewDimension      = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtv.Texture2D.MipSlice = range.mipOffset;
      }
      break;
    case Dimension::e3D:
      rtv.ViewDimension      = D3D12_RTV_DIMENSION_TEXTURE3D;
      rtv.Texture3D.MipSlice = range.mipOffset;
      break;
    default:
      throw std::runtime_error("Invalid view dimension for RTV");
  }

  return rtv;
}

static D3D12_DEPTH_STENCIL_VIEW_DESC
makeDsvDesc(const ViewRange& range, Format format, Dimension dimension)
{
  D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};

  dsv.Format = convert(format);

  switch(dimension) {
    case Dimension::e1D:
      if(range.layerCount > 1u) {
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
        dsv.Texture1DArray.MipSlice        = range.mipOffset;
        dsv.Texture1DArray.FirstArraySlice = range.layerOffset;
        dsv.Texture1DArray.ArraySize       = range.layerCount;
      } else {
        dsv.ViewDimension      = D3D12_DSV_DIMENSION_TEXTURE1D;
        dsv.Texture1D.MipSlice = range.mipOffset;
      }
      break;
    case Dimension::e2D:
      if(range.layerCount > 1u) {
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsv.Texture2DArray.MipSlice        = range.mipOffset;
        dsv.Texture2DArray.FirstArraySlice = range.layerOffset;
        dsv.Texture2DArray.ArraySize       = range.layerCount;
      } else {
        dsv.ViewDimension      = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsv.Texture2D.MipSlice = range.mipOffset;
      }
      break;
    default:
      throw std::runtime_error("Invalid view dimension for DSV");
  }

  return dsv;
}
Image::Image(Device& device, const Desc& desc):
  m_device{device},
  m_desc{desc},
  m_state{ResourceState::Undefined},
  m_views{},
  m_allocation{},
  m_memoryDesc{},
  m_handle{}
{
  if(m_desc.mips == 0u) {
    auto largest = std::max(m_desc.extent[0], m_desc.extent[1]);
    m_desc.mips  = toU32(std::log2(largest));
  }

  if(m_desc.handle) {
    m_handle = m_desc.handle;
  } else {
    auto res = makeResourceDesc(m_desc);

    m_memoryDesc =
      m_device.api().GetResourceAllocationInfo(0u, 1u, &res);
    m_allocation = m_device.AllocateMemory(
      m_desc.memoryType, m_memoryDesc.SizeInBytes,
      m_memoryDesc.Alignment);
    if(!m_allocation.IsValid())
      throw std::runtime_error("Failed to allocate memory");

    D3D12_CLEAR_VALUE defaultClearValue{
      .Format = convert(m_desc.format),
      .Color  = {0.f, 0.f, 0.f, 0.f},
    };

    if(vd::getFormatAspect(m_desc.format) != ImageAspect::Color)
      defaultClearValue.DepthStencil = {1.0f, 0u};

    VDDxTry(m_device.api().CreatePlacedResource(
      &m_allocation.pool->GetHandle(), m_allocation.offset, &res,
      D3D12_RESOURCE_STATE_COMMON, &defaultClearValue,
      IID_PPV_ARGS(&m_handle)));
  }

  if(m_desc.defaultView) AddView(*m_desc.defaultView);
  if(!m_desc.name.empty())
    m_handle->SetName(widen(m_desc.name).c_str());
}

Image::~Image()
{
  for(auto& view: m_views) {
    m_device.GetDescriptorPool().Free(view->handle);
  }
  m_views.clear();

  // Non-owned handle
  // For DX12 we want to let it Release, contrary to Vulkan.
  //if(m_desc.handle) {
  //  m_handle.Detach();
  //  m_handle = nullptr;
  //}

  m_allocation.Free();
}

u32 Image::AddView(ViewType type, const ViewRange& range)
{
  auto view = std::make_unique<View>();

  view->resource = this;
  view->format   = m_desc.format;
  view->samples  = m_desc.samples;
  view->type     = type;
  view->range    = range;

  if(view->range.mipCount == MaxU32) {
    view->range.mipCount = m_desc.mips;
  }
  if(view->range.layerCount == MaxU32) {
    view->range.layerCount = m_desc.extent[2];
  }

  switch(type) {
    case ViewType::SRV: {
      auto srv =
        makeSrvDesc(view->range, m_desc.format, m_desc.dimension);
      view->handle = m_device.GetDescriptorPool().Allocate(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      m_device.api().CreateShaderResourceView(
        m_handle.Get(), &srv, view->handle.cpu);
    } break;
    case ViewType::UAV: {
      auto uav =
        makeUavDesc(view->range, m_desc.format, m_desc.dimension);
      view->handle = m_device.GetDescriptorPool().Allocate(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      m_device.api().CreateUnorderedAccessView(
        m_handle.Get(), nullptr, &uav, view->handle.cpu);
    } break;
    case ViewType::RTV: {
      auto rtv =
        makeRtvDesc(view->range, m_desc.format, m_desc.dimension);
      view->handle = m_device.GetDescriptorPool().Allocate(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
      m_device.api().CreateRenderTargetView(
        m_handle.Get(), &rtv, view->handle.cpu);
    } break;
    case ViewType::DSV: {
      auto dsv =
        makeDsvDesc(view->range, m_desc.format, m_desc.dimension);
      view->handle = m_device.GetDescriptorPool().Allocate(
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
      m_device.api().CreateDepthStencilView(
        m_handle.Get(), &dsv, view->handle.cpu);
    } break;
    default:
      throw std::runtime_error("Invalid image view type");
  }

  view->binding = m_device.GetBinder().Bind(*view);

  u32 idx = 0u;
  for(auto& v: m_views)
    if(v->type == type) ++idx;

  m_views.push_back(std::move(view));

  return idx;
}

const Image::View* Image::GetView(ViewType type, u32 index) const
{
  u32 typeIdx = 0u;
  for(auto& view: m_views) {
    if(view->type != type) continue;

    if(typeIdx == index) return view.get();
    else
      typeIdx++;
  }

  return nullptr;
}

const Image::View* Image::GetView() const
{
  //return m_views.empty() ? nullptr : m_views[0].get();
  return GetView(*m_desc.defaultView);
}

bool Image::Write(Span<u8 const> data)
{
  // TODO: Persistent mapping for stuff updated every frame.

  if(
    m_desc.memoryType != MemoryType::Upload &&
    m_desc.memoryType != MemoryType::Download) {
    VDLogE("Cannot map memory that is not type UPLOAD or DOWNLOAD");
    return false;
  }

  if(data.size_bytes() > m_allocation.size) {
    VDLogE("Not enough space to allocate memory.");
    return false;
  }

  D3D12_RANGE range{0, 0}; // No reads.
  void*       dst = nullptr;

  if(FAILED(m_handle->Map(0, &range, &dst))) {
    VDLogW("Failed to map image memory for Write");
    return false;
  }

  memcpy(dst, data.data(), data.size_bytes());
  m_handle->Unmap(0, &range);

  return true;
}

u32 Image::GetSubresourceIndex(u32 mip, u32 layer, u32 plane) const
{
  if(m_desc.dimension == Dimension::e3D)
    return mip + layer * m_desc.mips;
  else
    return mip + layer * m_desc.mips +
           plane * m_desc.mips * m_desc.extent[3];
}

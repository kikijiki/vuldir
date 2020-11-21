#include "vuldir/api/Binder.hpp"
#include "vuldir/api/Buffer.hpp"
#include "vuldir/api/Device.hpp"
#include "vuldir/api/dx/DxUti.hpp"

using namespace vd;

static D3D12_RESOURCE_DESC makeResourceDesc(const Buffer::Desc& desc)
{
  D3D12_RESOURCE_DESC res{};

  res.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
  res.Alignment          = 0u; // Default, 64KB?
  res.Format             = DXGI_FORMAT_UNKNOWN;
  res.Width              = desc.size;
  res.Height             = 1u;
  res.DepthOrArraySize   = 1u;
  res.MipLevels          = 1u;
  res.SampleDesc.Count   = 1u;
  res.SampleDesc.Quality = 0u;
  res.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  res.Flags              = D3D12_RESOURCE_FLAG_NONE;

  return res;
}

static D3D12_CONSTANT_BUFFER_VIEW_DESC
makeCbvDesc(const ViewRange& range)
{
  D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
  cbv.BufferLocation = range.offset;
  cbv.SizeInBytes    = toU32(range.size);
  return cbv;
}

static D3D12_SHADER_RESOURCE_VIEW_DESC
makeSrvDesc(const ViewRange& range)
{
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};

  srv.Format        = DXGI_FORMAT_R32_TYPELESS;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping =
    D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.Flags        = D3D12_BUFFER_SRV_FLAG_RAW;
  srv.Buffer.FirstElement = range.offset;
  srv.Buffer.NumElements  = static_cast<u32>(range.size / sizeof(u32));
  return srv;
}

static D3D12_UNORDERED_ACCESS_VIEW_DESC
makeUavDesc(const ViewRange& range)
{
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};

  uav.Format              = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension       = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.Flags        = D3D12_BUFFER_UAV_FLAG_RAW;
  uav.Buffer.FirstElement = range.offset;
  uav.Buffer.NumElements  = static_cast<u32>(range.size / sizeof(u32));
  return uav;
}

Buffer::Buffer(Device& device, const Desc& desc):
  m_device{device},
  m_desc{desc},
  m_state{ResourceState::Undefined},
  m_views{},
  m_allocation{},
  m_handle{}
{
  auto res = makeResourceDesc(m_desc);

  auto begState = D3D12_RESOURCE_STATE_COMMON;
  if(m_desc.memoryType == MemoryType::Upload)
    begState = D3D12_RESOURCE_STATE_GENERIC_READ;
  if(m_desc.memoryType == MemoryType::Download)
    begState = D3D12_RESOURCE_STATE_COPY_DEST;

  m_memoryDesc = m_device.api().GetResourceAllocationInfo(0u, 1u, &res);
  if(m_memoryDesc.SizeInBytes == UINT64_MAX)
    throw makeError<std::runtime_error>(
      "Failed to get memory requirements for buffer '%s'",
      m_desc.name.c_str());

  m_allocation = m_device.AllocateMemory(
    m_desc.memoryType, m_memoryDesc.SizeInBytes,
    m_memoryDesc.Alignment);
  if(!m_allocation.IsValid())
    throw makeError<std::runtime_error>(
      "Failed to allocate memory for buffer '%s'", m_desc.name.c_str());

  VDDxTry(m_device.api().CreatePlacedResource(
    &m_allocation.pool->GetHandle(), m_allocation.offset, &res,
    begState, nullptr, IID_PPV_ARGS(&m_handle)));

  if(!m_desc.name.empty())
    m_handle->SetName(widen(m_desc.name).c_str());

  if(desc.defaultView) { AddView(desc.defaultView.value()); }
}

Buffer::~Buffer()
{
  for(auto& view: m_views) {
    m_device.GetDescriptorPool().Free(view->handle);
  }
  m_views.clear();

  m_handle = nullptr;

  m_allocation.Free();
}

u32 Buffer::AddView(ViewType type, const ViewRange& range)
{
  auto view      = std::make_unique<View>();
  view->resource = this;
  view->type     = type;
  view->range    = range;

  if(view->range.size == MaxU64) { view->range.size = m_desc.size; }

  switch(type) {
    case ViewType::CBV: {
      auto desc    = makeCbvDesc(view->range);
      view->handle = m_device.GetDescriptorPool().Allocate(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      m_device.api().CreateConstantBufferView(&desc, view->handle.cpu);
    } break;
    case ViewType::SRV: {
      auto desc    = makeSrvDesc(view->range);
      view->handle = m_device.GetDescriptorPool().Allocate(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      m_device.api().CreateShaderResourceView(
        m_handle.Get(), &desc, view->handle.cpu);
    } break;
    case ViewType::UAV: {
      auto desc    = makeUavDesc(view->range);
      view->handle = m_device.GetDescriptorPool().Allocate(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      m_device.api().CreateUnorderedAccessView(
        m_handle.Get(), nullptr, &desc, view->handle.cpu);
    } break;
    default:
      throw makeError<std::runtime_error>(
        "Invalid view type for buffer '%s'", m_desc.name.c_str());
  }

  u32 idx = 0u;
  for(auto& v: m_views)
    if(v->type == type) ++idx;

  view->binding = m_device.GetBinder().Bind(*view);
  m_views.push_back(std::move(view));

  return idx;
}

const Buffer::View* Buffer::GetView(ViewType type, u32 index) const
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

bool Buffer::Write(Span<u8 const> data)
{
  // TODO: Persistent mapping for stuff updated every frame.

  if(
    m_desc.memoryType != MemoryType::Upload &&
    m_desc.memoryType != MemoryType::Download) {
    VDLogE(
      "Cannot map memory that is not type UPLOAD or DOWNLOAD for "
      "buffer '%s'",
      m_desc.name.c_str());
    return false;
  }

  if(data.size_bytes() > m_allocation.size) {
    VDLogE(
      "Not enough space to allocate memory for buffer '%s'.",
      m_desc.name.c_str());
    return false;
  }

  D3D12_RANGE range{0, 0}; // No reads.
  void*       dst = nullptr;

  const auto mapResult = m_handle->Map(0, &range, &dst);
  if(FAILED(mapResult)) {
    VDLogW(
      "Failed to map buffer '%s' memory for Write: %s",
      m_desc.name.c_str(), formatHRESULT(mapResult).c_str());
    return false;
  }

  memcpy(dst, data.data(), data.size_bytes());
  m_handle->Unmap(0, &range);

  return true;
}

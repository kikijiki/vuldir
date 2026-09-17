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
  res.Width = alignUp(desc.size, static_cast<u64>(256u));
  res.Height             = 1u;
  res.DepthOrArraySize   = 1u;
  res.MipLevels          = 1u;
  res.SampleDesc.Count   = 1u;
  res.SampleDesc.Quality = 0u;
  res.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  res.Flags = desc.usage.IsSet(ResourceUsage::UnorderedAccess)
                ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                : D3D12_RESOURCE_FLAG_NONE;

  return res;
}

static D3D12_CONSTANT_BUFFER_VIEW_DESC makeCbvDesc(
  ID3D12Resource& resource, const ViewRange& range)
{
  D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{};
  cbv.BufferLocation = resource.GetGPUVirtualAddress() + range.offset;
  cbv.SizeInBytes =
    toU32(alignUp(range.size, static_cast<u64>(256u)));
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
  srv.Buffer.FirstElement = range.offset / sizeof(u32);
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
  uav.Buffer.FirstElement = range.offset / sizeof(u32);
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
  if(m_desc.size == 0u)
    throw std::invalid_argument("Buffer size must be non-zero");
  if(m_desc.size > MaxU64 - 255u)
    throw std::overflow_error("Aligned DX12 buffer size overflows");

  auto res = makeResourceDesc(m_desc);

  auto begState = D3D12_RESOURCE_STATE_COMMON;
  if(m_desc.memoryType == MemoryType::Upload) {
    begState = D3D12_RESOURCE_STATE_GENERIC_READ;
    // Upload heaps must remain in GENERIC_READ. CopySrc is the narrow
    // public state used by staging buffers and prevents an illegal
    // transition away from that fixed native state.
    m_state = ResourceState::CopySrc;
  }
  if(m_desc.memoryType == MemoryType::Download) {
    begState = D3D12_RESOURCE_STATE_COPY_DEST;
    m_state  = ResourceState::CopyDst;
  }

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
  ++m_device.m_bufferCount;
}

Buffer::~Buffer()
{
  --m_device.m_bufferCount;
  for(auto& view: m_views) {
    if(
      view->type == ViewType::SRV || view->type == ViewType::UAV ||
      view->type == ViewType::CBV) {
      m_device.GetBinder().Unbind(view->binding);
    }
    m_device.GetDescriptorPool().Free(view->handle);
  }
  m_views.clear();

  m_handle = nullptr;

  m_allocation.Free();
}

u32 Buffer::AddView(ViewType type, const ViewRange& range)
{
  m_views.reserve(m_views.size() + 1u);
  auto view      = std::make_unique<View>();
  view->resource = this;
  view->type     = type;
  view->range    = range;

  if(view->range.offset > m_desc.size)
    throw makeError<std::invalid_argument>(
      "Buffer view offset is out of range for '%s'", m_desc.name.c_str());
  if(view->range.size == MaxU64)
    view->range.size = m_desc.size - view->range.offset;
  if(
    view->range.size == 0u ||
    view->range.size > m_desc.size - view->range.offset)
    throw makeError<std::invalid_argument>(
      "Buffer view size is out of range for '%s'", m_desc.name.c_str());

  switch(type) {
    case ViewType::CBV: {
      if(view->range.offset % 256u != 0u)
        throw std::runtime_error("Constant-buffer offset must be 256-byte aligned");
      if(view->range.size == 0u || view->range.size > 64u * 1024u)
        throw std::runtime_error("Constant-buffer size must be between 1 and 64 KiB");
      auto desc    = makeCbvDesc(*m_handle.Get(), view->range);
      view->handle = m_device.GetDescriptorPool().Allocate(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      m_device.api().CreateConstantBufferView(&desc, view->handle.cpu);
    } break;
    case ViewType::SRV: {
      if(view->range.offset % sizeof(u32) != 0u ||
         view->range.size % sizeof(u32) != 0u)
        throw std::runtime_error("Raw buffer views must be 4-byte aligned");
      auto desc    = makeSrvDesc(view->range);
      view->handle = m_device.GetDescriptorPool().Allocate(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      m_device.api().CreateShaderResourceView(
        m_handle.Get(), &desc, view->handle.cpu);
    } break;
    case ViewType::UAV: {
      if(!m_desc.usage.IsSet(ResourceUsage::UnorderedAccess))
        throw std::runtime_error("UAV buffer view requires UnorderedAccess usage");
      if(view->range.offset % sizeof(u32) != 0u ||
         view->range.size % sizeof(u32) != 0u)
        throw std::runtime_error("Raw buffer views must be 4-byte aligned");
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

  try {
    view->binding = m_device.GetBinder().Bind(*view);
  } catch(...) {
    m_device.GetDescriptorPool().Free(view->handle);
    throw;
  }
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

  if(data.size_bytes() > m_desc.size) {
    VDLogE(
      "Write exceeds the declared size of buffer '%s'",
      m_desc.name.c_str());
    return false;
  }
  if(data.empty()) return true;

  if(
    m_desc.memoryType != MemoryType::Upload &&
    m_desc.memoryType != MemoryType::Download) {
    VDLogE(
      "Cannot map memory that is not type UPLOAD or DOWNLOAD for "
      "buffer '%s'",
      m_desc.name.c_str());
    return false;
  }

  D3D12_RANGE readRange{0, 0};
  void*       dst = nullptr;

  const auto mapResult = m_handle->Map(0, &readRange, &dst);
  if(FAILED(mapResult)) {
    VDLogW(
      "Failed to map buffer '%s' memory for Write: %s",
      m_desc.name.c_str(), formatHRESULT(mapResult).c_str());
    return false;
  }

  memcpy(dst, data.data(), data.size_bytes());
  const D3D12_RANGE writtenRange{0, data.size_bytes()};
  m_handle->Unmap(0, &writtenRange);

  return true;
}

bool Buffer::Read(Span<u8> data)
{
  if(data.size_bytes() > m_desc.size) {
    VDLogE(
      "Read exceeds the declared size of buffer '%s'",
      m_desc.name.c_str());
    return false;
  }
  if(data.empty()) return true;
  if(
    m_desc.memoryType != MemoryType::Upload &&
    m_desc.memoryType != MemoryType::Download) {
    VDLogE(
      "Cannot map memory that is not type UPLOAD or DOWNLOAD for "
      "buffer '%s'",
      m_desc.name.c_str());
    return false;
  }

  const D3D12_RANGE readRange{0, data.size_bytes()};
  void*             source = nullptr;
  const auto mapResult = m_handle->Map(0u, &readRange, &source);
  if(FAILED(mapResult)) {
    VDLogW(
      "Failed to map buffer '%s' memory for Read: %s",
      m_desc.name.c_str(), formatHRESULT(mapResult).c_str());
    return false;
  }
  std::memcpy(data.data(), source, data.size_bytes());
  const D3D12_RANGE writtenRange{0u, 0u};
  m_handle->Unmap(0u, &writtenRange);
  return true;
}

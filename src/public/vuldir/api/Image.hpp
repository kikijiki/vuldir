#pragma once

#include "vuldir/api/Common.hpp"
#include "vuldir/api/MemoryPool.hpp"

#ifdef VD_API_DX
  #include "vuldir/api/dx/DxCPUDescriptorPool.hpp"
#endif

namespace vd {

class Device;

class Image
{
public:
  struct Desc {
    Str name;

    Flags<ResourceUsage> usage;
    Format               format;
    Dimension            dimension;
    UInt3                extent;

    Opt<ViewType> defaultView;

    u32 samples = 1u;
    u32 mips    = 1u;

    MemoryType memoryType = MemoryType::Main;

#ifdef VD_API_VK
    VkImage handle;
#elif VD_API_DX
    ComPtr<ID3D12Resource> handle;
#endif
  };

  struct View {
    Image*            resource = {};
    Format            format   = Format::UNDEFINED;
    u32               samples  = 1u;
    ViewType          type     = ViewType::SRV;
    ViewRange         range    = {};
    DescriptorBinding binding  = {};

#ifdef VD_API_VK
    VkImageView handle = {};
#elif VD_API_DX
    CPUDescriptorPool::Handle handle = {};
#endif

    const char* GetResourceName() const
    {
      if(!resource) return "NULL";

      const auto& name = resource->GetDesc().name;
      return name.empty() ? "NONAME" : name.c_str();
    }
  };

public:
  VD_NONMOVABLE(Image);

  Image(Device& device, const Desc& desc);
  ~Image();

public:
  const Desc& GetDesc() const { return m_desc; }

  Flags<ResourceUsage> GetUsage() const { return m_desc.usage; }
  Format               GetFormat() const { return m_desc.format; }
  MemoryType    GetMemoryType() const { return m_desc.memoryType; }
  ResourceState GetState() const { return m_state; }
  void          SetState(ResourceState state) { m_state = state; }

  u32         AddView(ViewType type, const ViewRange& range = {});
  const View* GetView(ViewType type, u32 index = 0u) const;
  const View* GetView() const;

  bool Write(Span<u8 const> data);

#ifdef VD_API_VK
  VkImage GetHandle() const { return m_handle; }
#elif VD_API_DX
  ID3D12Resource* GetHandle() const { return m_handle.Get(); }
  u32             GetSubresourceIndex(
                u32 mip = 0u, u32 layer = 0u, u32 plane = 0u) const;
#endif

private:
  Device&         m_device;
  Desc            m_desc;
  ResourceState   m_state;
  Arr<UPtr<View>> m_views;

  MemoryPool::Allocation m_allocation;

#ifdef VD_API_VK
  VkMemoryRequirements m_memoryDesc;
  VkImage              m_handle;
#elif VD_API_DX
  D3D12_RESOURCE_ALLOCATION_INFO m_memoryDesc;
  ComPtr<ID3D12Resource>         m_handle;
#endif
};

} // namespace vd

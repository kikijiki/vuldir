#pragma once

#include "vuldir/api/Common.hpp"
#include "vuldir/api/MemoryPool.hpp"

#ifdef VD_API_DX
  #include "vuldir/api/dx/DxCPUDescriptorPool.hpp"
#endif

namespace vd {

class Device;

class Buffer
{
public:
  struct Desc {
    Str                  name;
    Flags<ResourceUsage> usage;
    u64                  size;
    Opt<ViewType>        defaultView;
    MemoryType           memoryType = MemoryType::Main;
  };

  struct View {
    Buffer*           resource = {};
    ViewType          type     = ViewType::SRV;
    ViewRange         range    = {};
    DescriptorBinding binding  = {};

#ifdef VD_API_VK
    VkBufferView handle = nullptr;
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
  VD_NONMOVABLE(Buffer);

  Buffer(Device& device, const Desc& desc);
  ~Buffer();

public:
  const Desc& GetDesc() const { return m_desc; }

  u32         AddView(ViewType type, const ViewRange& range = {});
  const View* GetView(ViewType type, u32 index = 0u) const;

  Flags<ResourceUsage> GetUsage() const { return m_desc.usage; }
  u64                  GetSize() const { return m_desc.size; }
  ResourceState        GetState() const { return m_state; }
  void       SetState(ResourceState state) { m_state = state; }
  MemoryType GetMemoryType() const { return m_desc.memoryType; }

  bool Write(Span<u8 const> data);
  template<typename T>
  bool Write(const T& data)
  {
    return Write(getBytes(data));
  }
#ifdef VD_API_VK
  VkBuffer GetHandle() const { return m_handle; }
#elif VD_API_DX
  ID3D12Resource* GetHandle() const { return m_handle.Get(); }
#endif

private:
  Device&         m_device;
  Desc            m_desc;
  ResourceState   m_state;
  Arr<UPtr<View>> m_views;

  MemoryPool::Allocation m_allocation;

#ifdef VD_API_VK
  VkMemoryRequirements m_memoryDesc;
  VkBuffer             m_handle;
#elif VD_API_DX
  D3D12_RESOURCE_ALLOCATION_INFO m_memoryDesc;
  ComPtr<ID3D12Resource>         m_handle;
#endif
};

} // namespace vd

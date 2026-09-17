#pragma once

#include "vuldir/api/Buffer.hpp"
#include "vuldir/api/Common.hpp"
#include "vuldir/api/Image.hpp"
#include "vuldir/api/Sampler.hpp"

namespace vd {

class Device;
class CommandBuffer;

class Binder
{
public:
  using FrameContextId = u64;

  struct Stats {
    u32 allocated = 0u;
    u32 capacity  = 0u;
  };

  struct Desc {
    u32 maxDescriptorCount = 1000000u;
    u32 maxSamplerCount    = 2048u;
  };

public:
  VD_NONMOVABLE(Binder);

  Binder(Device& device, const Desc& desc);
  ~Binder();

  void Bind(CommandBuffer& cmd, BindPoint bindPoint);

  DescriptorBinding Bind(const Sampler::View& view);
  DescriptorBinding Bind(const Buffer::View& view);
  DescriptorBinding Bind(const Image::View& view);

  void Unbind(const DescriptorBinding& binding);

  // Deferred bindless free-list reclaim. A descriptor is released only
  // after every live frame slot that could reference it has retired.
  FrameContextId RegisterFrameContext(u32 slotCount);
  void           UnregisterFrameContext(FrameContextId context);
  void           BeginFrame(FrameContextId context, u32 slot);
  void           RetireFrame(FrameContextId context, u32 slot);
  void           FlushDeferred(FrameContextId context);

  Stats GetStats();

private:
  void createHeaps();
  void createLayout();
  void freeBinding(const DescriptorBinding& binding);
  void retireSlot(FrameContextId context, u32 slot);

private:
  struct Heap;

  Device&         m_device;
  Desc            m_desc;
  std::mutex      m_mutex;
  Arr<UPtr<Heap>> m_heaps;

  struct PendingUnbind {
    DescriptorBinding binding;
    u32               remainingSlots = 0u;
  };
  struct FrameSlot {
    bool                     active = false;
    Arr<SPtr<PendingUnbind>> pending;
  };

  FrameContextId                      m_nextFrameContext = 1u;
  Map<FrameContextId, Arr<FrameSlot>> m_frameContexts;

#ifdef VD_API_VK
public:
  VkPipelineLayout GetPipelineLayout() const
  {
    return m_pipelineLayout;
  }

private:
  Heap& getHeap(DescriptorType type)
  {
    return *m_heaps[enumValue(type)];
  }

  Arr<UPtr<Sampler>>    m_staticSamplers;
  VkDescriptorPool      m_descriptorPool;
  VkDescriptorSetLayout m_descriptorSetLayout;
  VkDescriptorSet       m_descriptorSet;
  VkPipelineLayout      m_pipelineLayout;
#endif

#ifdef VD_API_DX
public:
  struct DescriptorInfo {
    struct Table {
      u32                         index;
      D3D12_GPU_DESCRIPTOR_HANDLE handle;
    };

    u32 pushConstantsIdx;

    SArr<ID3D12DescriptorHeap*, 2u> heaps;
    SArr<Table, 2u>                 tables;
  };

  ID3D12RootSignature* GetRootSignature() const
  {
    return m_rootSignature.Get();
  }

  const DescriptorInfo& GetDescriptorInfo() const
  {
    return m_descriptorInfo;
  }

private:
  Heap& getHeap(DescriptorType type)
  {
    return *m_heaps[type == DescriptorType::Sampler ? 0u : 1u];
  }

  ComPtr<ID3D12RootSignature> m_rootSignature;
  DescriptorInfo              m_descriptorInfo;
#endif
};

} // namespace vd

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

private:
  void createHeaps();
  void createLayout();

private:
  struct Heap;

  Device&         m_device;
  Desc            m_desc;
  Arr<UPtr<Heap>> m_heaps;

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

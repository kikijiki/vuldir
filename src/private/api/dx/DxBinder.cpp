#include "vuldir/api/Binder.hpp"
#include "vuldir/api/CommandBuffer.hpp"
#include "vuldir/api/Device.hpp"
#include "vuldir/api/dx/DxUti.hpp"

using namespace vd;

struct StaticSampler {
  u32                        index;
  D3D12_FILTER               filter;
  D3D12_TEXTURE_ADDRESS_MODE mode;
};

struct BindlessSlot {
  u32                         space;
  D3D12_DESCRIPTOR_RANGE_TYPE type;
};

static constexpr u64 PushConstantsSize = 16u;

static constexpr StaticSampler StaticSamplers[] = {
  {0u, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP},
  {1u, D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR,
   D3D12_TEXTURE_ADDRESS_MODE_WRAP},
  {2u, D3D12_FILTER_MIN_MAG_MIP_POINT,
   D3D12_TEXTURE_ADDRESS_MODE_MIRROR},
  {3u, D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR,
   D3D12_TEXTURE_ADDRESS_MODE_MIRROR},
  {4u, D3D12_FILTER_MIN_MAG_MIP_POINT,
   D3D12_TEXTURE_ADDRESS_MODE_CLAMP},
  {5u, D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR,
   D3D12_TEXTURE_ADDRESS_MODE_CLAMP},
};

static constexpr BindlessSlot BindlessSlots[] = {
  {1u, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER}, // samplers[]
  {1u, D3D12_DESCRIPTOR_RANGE_TYPE_SRV},     // srvBuf[]
  {1u, D3D12_DESCRIPTOR_RANGE_TYPE_UAV},     // uavBuf[]
  {2u, D3D12_DESCRIPTOR_RANGE_TYPE_SRV},     // srvTex1D[]
  {3u, D3D12_DESCRIPTOR_RANGE_TYPE_SRV},     // srvTex2D[]
  {4u, D3D12_DESCRIPTOR_RANGE_TYPE_SRV},     // srvTex3D[]
  {5u, D3D12_DESCRIPTOR_RANGE_TYPE_SRV},     // srvTexCB[]
  {6u, D3D12_DESCRIPTOR_RANGE_TYPE_SRV},     // srvTex1DAR[]
  {7u, D3D12_DESCRIPTOR_RANGE_TYPE_SRV},     // srvTex2DAR[]
  {8u, D3D12_DESCRIPTOR_RANGE_TYPE_SRV},     // srvTexCBAR[]
  {2u, D3D12_DESCRIPTOR_RANGE_TYPE_UAV},     // uavTex1D[]
  {3u, D3D12_DESCRIPTOR_RANGE_TYPE_UAV},     // uavTex2D[]
  {4u, D3D12_DESCRIPTOR_RANGE_TYPE_UAV},     // uavTex3D[]
  {5u, D3D12_DESCRIPTOR_RANGE_TYPE_UAV},     // uavTex1DAR[]
  {6u, D3D12_DESCRIPTOR_RANGE_TYPE_UAV},     // uavTex2DAR[]
};

struct Binder::Heap {
  Device&    device;
  std::mutex mutex;

  D3D12_DESCRIPTOR_HEAP_TYPE   type;
  u32                          size;
  ComPtr<ID3D12DescriptorHeap> handle;
  D3D12_CPU_DESCRIPTOR_HANDLE  cpuStart;
  D3D12_GPU_DESCRIPTOR_HANDLE  gpuStart;
  u64                          increment;

  Arr<u32> freeList;

  VD_NONMOVABLE(Heap);

  Heap(Device& device_, D3D12_DESCRIPTOR_HEAP_TYPE type_, u32 size_):
    device{device_}, type{type_}, size{size_}
  {
    D3D12_DESCRIPTOR_HEAP_DESC desc{};

    desc.Type           = type;
    desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    desc.NumDescriptors = size;
    desc.NodeMask       = 0u;

    VDDxTry(
      device.api().CreateDescriptorHeap(&desc, IID_PPV_ARGS(&handle)));

    cpuStart  = handle->GetCPUDescriptorHandleForHeapStart();
    gpuStart  = handle->GetGPUDescriptorHandleForHeapStart();
    increment = device.api().GetDescriptorHandleIncrementSize(type);

    for(u32 idx = 1u; idx <= size; ++idx)
      freeList.push_back(size - idx);
  }

  ~Heap()
  {
    std::scoped_lock lock(mutex);
    handle = nullptr;
    freeList.clear();
  }

  u32 allocate(D3D12_CPU_DESCRIPTOR_HANDLE src)
  {
    u32 index = 0u;

    {
      std::scoped_lock lock(mutex);
      index = freeList.back();
      freeList.pop_back();
    }

    const D3D12_CPU_DESCRIPTOR_HANDLE dst = {
      cpuStart.ptr + increment * index};
    device.api().CopyDescriptorsSimple(1u, dst, src, type);

    return index;
  }

  void free(u32 idx)
  {
    std::scoped_lock lock(mutex);
    freeList.push_back(idx);
  }
};

Binder::Binder(Device& device, const Desc& desc):
  m_device{device},
  m_desc{desc},
  m_heaps{},
  m_rootSignature{},
  m_descriptorInfo{}
{
  { // Create heaps
    m_heaps.push_back(std::make_unique<Heap>(
      m_device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
      m_desc.maxSamplerCount));
    m_heaps.push_back(std::make_unique<Heap>(
      m_device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
      m_desc.maxDescriptorCount));
  }

  m_descriptorInfo.pushConstantsIdx = 0u;

  m_descriptorInfo.heaps[0]  = m_heaps[0]->handle.Get();
  m_descriptorInfo.heaps[1]  = m_heaps[1]->handle.Get();
  m_descriptorInfo.tables[0] = {1u, m_heaps[0]->gpuStart};
  m_descriptorInfo.tables[1] = {2u, m_heaps[1]->gpuStart};

  { // Create root signature
    Arr<D3D12_ROOT_PARAMETER1>     rootParams;
    Arr<D3D12_STATIC_SAMPLER_DESC> staticSamplers;
    Arr<D3D12_DESCRIPTOR_RANGE1>   bindlessSamplers;
    Arr<D3D12_DESCRIPTOR_RANGE1>   bindlessResources;

    //////////////////////////////////////////////////////////////////////////
    { // PUSH CONSTANTS

      auto& params = rootParams.emplace_back();

      params.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
      params.Constants.Num32BitValues = PushConstantsSize / sizeof(u32);
      params.Constants.ShaderRegister = 0u;
      params.Constants.RegisterSpace  = 0u;
    }

    //////////////////////////////////////////////////////////////////////////
    { // STATIC SAMPLERS
      for(auto& sampler: StaticSamplers) {
        D3D12_STATIC_SAMPLER_DESC smpDesc{};

        smpDesc.Filter         = sampler.filter;
        smpDesc.AddressU       = sampler.mode;
        smpDesc.AddressV       = sampler.mode;
        smpDesc.AddressW       = sampler.mode;
        smpDesc.ShaderRegister = sampler.index;
        smpDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        smpDesc.BorderColor =
          D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        smpDesc.MaxLOD           = 9999.f;
        smpDesc.RegisterSpace    = 0u;
        smpDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        staticSamplers.push_back(smpDesc);
      }
    }

    //////////////////////////////////////////////////////////////////////////
    { // BINDLESS RESOURCES
      for(auto& slot: BindlessSlots) {
        D3D12_DESCRIPTOR_RANGE1 rng{};

        rng.RangeType                         = slot.type;
        rng.NumDescriptors                    = ~0u;
        rng.BaseShaderRegister                = 0u;
        rng.RegisterSpace                     = slot.space;
        rng.OffsetInDescriptorsFromTableStart = 0u;

        if(slot.type == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER) {
          rng.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
          bindlessSamplers.push_back(rng);
        } else {
          rng.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
                      D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
          bindlessResources.push_back(rng);
        }
      }

      auto& smpParams = rootParams.emplace_back();
      smpParams.ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      smpParams.DescriptorTable.NumDescriptorRanges =
        vd::size32(bindlessSamplers);
      smpParams.DescriptorTable.pDescriptorRanges =
        std::data(bindlessSamplers);

      auto& resParams = rootParams.emplace_back();
      resParams.ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      resParams.DescriptorTable.NumDescriptorRanges =
        vd::size32(bindlessResources);
      resParams.DescriptorTable.pDescriptorRanges =
        std::data(bindlessResources);
    }

    /////////////////////////////////////////////////////////////////////
    // ROOT SIGNATURE

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC signatureDesc{};

    signatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    signatureDesc.Desc_1_1.NumParameters = vd::size32(rootParams);
    signatureDesc.Desc_1_1.pParameters   = std::data(rootParams);
    signatureDesc.Desc_1_1.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    signatureDesc.Desc_1_1.NumStaticSamplers =
      vd::size32(staticSamplers);
    signatureDesc.Desc_1_1.pStaticSamplers = std::data(staticSamplers);

    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;

    VDDxTryBlob(
      errorBlob, D3D12SerializeVersionedRootSignature(
                   &signatureDesc, &signatureBlob, &errorBlob));

    VDDxTry(m_device.api().CreateRootSignature(
      0u, signatureBlob->GetBufferPointer(),
      signatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
  }
}

Binder::~Binder()
{
  m_rootSignature = nullptr;
  m_heaps.clear();
}

DescriptorBinding Binder::Bind(const Sampler::View& view)
{
  DescriptorBinding binding{};
  binding.type = DescriptorType::Sampler;
  binding.index =
    getHeap(DescriptorType::Sampler).allocate(view.handle.cpu);

  VDLogV("Bind sampler #%u", binding.index);
  return binding;
}

DescriptorBinding Binder::Bind(const Buffer::View& view)
{
  DescriptorBinding binding{};
  binding.type = DescriptorType::StorageBuffer;
  binding.index =
    getHeap(DescriptorType::StorageBuffer).allocate(view.handle.cpu);

  VDLogV(
    "Bind buffer #%u to %s", binding.index, view.GetResourceName());
  return binding;
}

DescriptorBinding Binder::Bind(const Image::View& view)
{
  DescriptorBinding binding{};

  switch(view.type) {
    case ViewType::SRV:
      binding.type = DescriptorType::SampledImage;
      binding.index =
        getHeap(DescriptorType::SampledImage).allocate(view.handle.cpu);
      VDLogV(
        "Bind image (SRV)  #%u to %s", binding.index,
        view.GetResourceName());
      break;
    case ViewType::UAV:
      binding.type = DescriptorType::StorageImage;
      binding.index =
        getHeap(DescriptorType::StorageImage).allocate(view.handle.cpu);
      VDLogV(
        "Bind image (UAV) #%u to %s", binding.index,
        view.GetResourceName());
      break;
    default:
      // throw std::runtime_error("Invalid image view type");
      break;
  }

  return binding;
}

void Binder::Unbind(const DescriptorBinding& binding)
{
  getHeap(binding.type).free(binding.index);
}

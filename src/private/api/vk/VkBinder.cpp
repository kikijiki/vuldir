#include "vuldir/api/Binder.hpp"
#include "vuldir/api/CommandBuffer.hpp"
#include "vuldir/api/Device.hpp"
#include "vuldir/api/PhysicalDevice.hpp"
#include "vuldir/api/vk/VkUti.hpp"

using namespace vd;

struct StaticSampler {
  u32                binding;
  SamplerFilter      filter;
  SamplerAddressMode mode;
};

struct BindlessSlot {
  u32              binding;
  VkDescriptorType type;
  const char*      name;
};

static constexpr u64 PushConstantsSize = 16u;

static constexpr StaticSampler StaticSamplers[] = {
  {0u, SamplerFilter::Nearest, SamplerAddressMode::Repeat},
  {1u, SamplerFilter::Linear, SamplerAddressMode::Repeat},
  {2u, SamplerFilter::Nearest, SamplerAddressMode::Mirror},
  {3u, SamplerFilter::Linear, SamplerAddressMode::Mirror},
  {4u, SamplerFilter::Nearest, SamplerAddressMode::Clamp},
  {5u, SamplerFilter::Linear, SamplerAddressMode::Clamp},
};

static constexpr BindlessSlot BindlessSlots[] = {
  {6u, VK_DESCRIPTOR_TYPE_SAMPLER, "smp"},
  {7u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, "buf"},
  {8u, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, "srvTex"},
  {9u, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, "uavTex"},
};

struct Binder::Heap {
  Device&    device;
  std::mutex mutex;

  VkDescriptorSet  set;
  VkDescriptorType type;
  u32              binding;
  u32              size;
  const char*      name;

  Arr<u32> freeList;

  VD_NONMOVABLE(Heap);

  Heap(
    Device& device_, VkDescriptorSet set_, VkDescriptorType type_,
    u32 size, u32 binding_, const char* name_):
    device{device_},
    set{set_},
    type{type_},
    binding{binding_},
    name{name_}
  {
    freeList.reserve(size);
    for(u32 idx = 0u; idx < size; ++idx)
      freeList.push_back(size - idx - 1u);
  }

  ~Heap()
  {
    std::scoped_lock lock(mutex);
    freeList.clear();
  }

  u32 allocate()
  {
    std::scoped_lock lock(mutex);

    const auto index = freeList.back();
    freeList.pop_back();

    return index;
  }

  void free(u32 idx)
  {
    std::scoped_lock lock(mutex);
    freeList.push_back(idx);
  }
};

Binder::Binder(Device& device_, const Desc& desc_):
  m_device{device_},
  m_desc{desc_},
  m_heaps{},
  m_staticSamplers{},
  m_descriptorSetLayout{},
  m_descriptorSet{},
  m_pipelineLayout{}
{
  const auto& props = m_device.GetPhysicalDevice().GetProperties();

  Arr<VkDescriptorSetLayoutBinding> bindings;
  Arr<VkSampler>                    staticSamplers;
  Map<VkDescriptorType, u32>        heapSizes = {
    {VK_DESCRIPTOR_TYPE_SAMPLER,
            std::min(
       props->limits.maxDescriptorSetSamplers, m_desc.maxSamplerCount)},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            std::min(
       props->limits.maxDescriptorSetStorageBuffers,
       m_desc.maxDescriptorCount)},
    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            std::min(
       props->limits.maxDescriptorSetSampledImages,
       m_desc.maxDescriptorCount)},
    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            std::min(
       props->limits.maxDescriptorSetStorageImages,
       m_desc.maxDescriptorCount)},
  };

  { // Descriptor pool
    VkDescriptorPoolSize poolSizes[] = {
      {VK_DESCRIPTOR_TYPE_SAMPLER,
       heapSizes[VK_DESCRIPTOR_TYPE_SAMPLER]},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
       heapSizes[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER]},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
       heapSizes[VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE]},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
       heapSizes[VK_DESCRIPTOR_TYPE_STORAGE_IMAGE]},
    };

    VkDescriptorPoolCreateInfo poolCI{};
    poolCI.sType   = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCI.flags   = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolCI.maxSets = 1u;
    poolCI.poolSizeCount = vd::size32(poolSizes);
    poolCI.pPoolSizes    = poolSizes;

    VDVkTry(
      m_device.api().CreateDescriptorPool(&poolCI, &m_descriptorPool));
  }

  // https://gist.github.com/NotAPenguin0/284461ecc81267fa41a7fbc472cd3afe
  // http://chunkstories.xyz/blog/a-note-on-descriptor-indexing/
  Arr<VkDescriptorBindingFlags> bindingFlags;
  //Arr<u32>                      descriptorCounts;

  { // Bindings: Static samplers (binding 0-5)
    for(const auto& sampler: StaticSamplers) {
      Sampler::Desc desc{};
      desc.minFilter = desc.magFilter = desc.mipFilter = sampler.filter;
      desc.u = desc.v = desc.w = sampler.mode;

      m_staticSamplers.push_back(
        std::make_unique<Sampler>(m_device, desc));

      auto& bnd           = bindings.emplace_back();
      bnd.binding         = sampler.binding;
      bnd.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER;
      bnd.stageFlags      = VK_SHADER_STAGE_ALL;
      bnd.descriptorCount = 1;
      bnd.pImmutableSamplers =
        &m_staticSamplers.back()->GetView().handle;

      bindingFlags.push_back(0);
      //descriptorCounts.push_back(1);
    }
  }

  // Bindless slots
  for(auto& slot: BindlessSlots) {
    auto& bnd = bindings.emplace_back();

    bnd.descriptorType     = slot.type;
    bnd.binding            = slot.binding;
    bnd.descriptorCount    = heapSizes[slot.type] - 1; // TODO Wtf?
    bnd.stageFlags         = VK_SHADER_STAGE_ALL;
    bnd.pImmutableSamplers = nullptr;

    bindingFlags.push_back(
      VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
      // | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT
    );
    //descriptorCounts.push_back(bnd.descriptorCount);
  }

  { // Descriptor set
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCI{
      .sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
      .pNext         = nullptr,
      .bindingCount  = vd::size32(bindingFlags),
      .pBindingFlags = std::data(bindingFlags),
    };

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.pNext = &bindingFlagsCI;
    ci.flags =
      VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    ci.bindingCount = vd::size32(bindings);
    ci.pBindings    = std::data(bindings);

    VDVkTry(m_device.api().CreateDescriptorSetLayout(
      &ci, &m_descriptorSetLayout));

    //VkDescriptorSetVariableDescriptorCountAllocateInfo setCounts = {};
    //setCounts.sType =
    //  VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    //setCounts.descriptorSetCount = vd::size32(descriptorCounts);
    //setCounts.pDescriptorCounts  = std::data(descriptorCounts);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    //allocInfo.pNext = &setCounts;
    allocInfo.descriptorPool     = m_descriptorPool;
    allocInfo.descriptorSetCount = 1u;
    allocInfo.pSetLayouts        = &m_descriptorSetLayout;

    VDVkTry(m_device.api().AllocateDescriptorSets(
      &allocInfo, &m_descriptorSet));
  }

  // Heaps
  for(auto& slot: BindlessSlots)
    m_heaps.push_back(std::make_unique<Heap>(
      m_device, m_descriptorSet, slot.type, heapSizes[slot.type],
      slot.binding, slot.name));

  { // Pipeline layout
    VkPipelineLayoutCreateInfo ci{};

    VkPushConstantRange pushConstants{};
    pushConstants.offset     = 0u;
    pushConstants.size       = PushConstantsSize;
    pushConstants.stageFlags = VK_SHADER_STAGE_ALL;

    ci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.setLayoutCount = 1u;
    ci.pSetLayouts    = &m_descriptorSetLayout;
    ci.pushConstantRangeCount = 1u;
    ci.pPushConstantRanges    = &pushConstants;

    VDVkTry(
      m_device.api().CreatePipelineLayout(&ci, &m_pipelineLayout));
  }
}

Binder::~Binder()
{
  m_staticSamplers.clear();
  m_heaps.clear();

  if(m_pipelineLayout)
    m_device.api().DestroyPipelineLayout(m_pipelineLayout);
  m_pipelineLayout = nullptr;

  if(m_descriptorSetLayout)
    m_device.api().DestroyDescriptorSetLayout(m_descriptorSetLayout);
  m_descriptorSetLayout = nullptr;

  if(m_descriptorPool)
    m_device.api().DestroyDescriptorPool(m_descriptorPool);
  m_descriptorPool = nullptr;
}

void Binder::Bind(CommandBuffer& cmd, BindPoint bindPoint)
{
  m_device.api().CmdBindDescriptorSets(
    cmd.GetHandle(), convert(bindPoint), m_pipelineLayout, 0u, 1u,
    &m_descriptorSet, 0u, nullptr);
}

DescriptorBinding Binder::Bind(const Sampler::View& view)
{
  auto& heap = getHeap(DescriptorType::Sampler);

  DescriptorBinding binding{};
  binding.type  = DescriptorType::Sampler;
  binding.index = heap.allocate();

  VkDescriptorImageInfo info{};
  info.sampler = view.handle;

  VkWriteDescriptorSet write{};
  write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER;
  write.dstSet          = heap.set;
  write.dstBinding      = heap.binding;
  write.dstArrayElement = binding.index;
  write.descriptorCount = 1;
  write.pImageInfo      = &info;

  VDLogI(
    "Bind sampler #%u %s[%u]", heap.binding, heap.name, binding.index);
  m_device.api().UpdateDescriptorSets(1u, &write, 0u, nullptr);

  return binding;
}

DescriptorBinding Binder::Bind(const Buffer::View& view)
{
  auto& heap = getHeap(DescriptorType::StorageBuffer);

  DescriptorBinding binding{};
  binding.type  = DescriptorType::StorageBuffer;
  binding.index = heap.allocate();

  VkDescriptorBufferInfo info{};
  info.buffer = view.resource->GetHandle();
  info.offset = view.range.offset;
  info.range  = view.range.size;

  VkWriteDescriptorSet write{};
  write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  write.dstSet          = heap.set;
  write.dstBinding      = heap.binding;
  write.dstArrayElement = binding.index;
  write.descriptorCount = 1;
  write.pBufferInfo     = &info;

  VDLogI(
    "Bind buffer #%u %s[%u] to: %s", heap.binding, heap.name,
    binding.index, view.GetResourceName());
  m_device.api().UpdateDescriptorSets(1u, &write, 0u, nullptr);

  return binding;
}

DescriptorBinding Binder::Bind(const Image::View& view)
{
  DescriptorBinding binding{};
  Heap*             heap = nullptr;

  switch(view.type) {
    case ViewType::SRV:
      heap         = &getHeap(DescriptorType::SampledImage);
      binding.type = DescriptorType::SampledImage;
      break;
    case ViewType::UAV:
      heap         = &getHeap(DescriptorType::StorageImage);
      binding.type = DescriptorType::StorageImage;
      break;
    default:
      return {};
  }

  binding.index = heap->allocate();

  VkDescriptorImageInfo info{};
  info.imageView = view.handle;

  switch(view.type) {
    case ViewType::SRV:
      info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      break;
    case ViewType::UAV:
      info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
      break;
    case ViewType::DSV:
      info.imageLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
      break;
    case ViewType::RTV:
      info.imageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      break;
    default:
      info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
      break;
  }

  VkWriteDescriptorSet write{};
  write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.descriptorType  = heap->type;
  write.dstSet          = heap->set;
  write.dstBinding      = heap->binding;
  write.dstArrayElement = binding.index;
  write.descriptorCount = 1;
  write.pImageInfo      = &info;

  VDLogI(
    "Bind image #%u %s[%u] to: %s", heap->binding, heap->name,
    binding.index, view.GetResourceName());
  m_device.api().UpdateDescriptorSets(1u, &write, 0u, nullptr);

  return binding;
}

void Binder::Unbind(const DescriptorBinding& binding)
{
  getHeap(binding.type).free(binding.index);
}

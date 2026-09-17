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
  bool               anisotropy;
};

struct BindlessSlot {
  u32              binding;
  VkDescriptorType type;
  const char*      name;
};

static constexpr u64 PushConstantsSize = 16u;

static constexpr StaticSampler StaticSamplers[] = {
  {0u, SamplerFilter::Nearest, SamplerAddressMode::Repeat, false},
  {1u, SamplerFilter::Linear, SamplerAddressMode::Repeat, true},
  {2u, SamplerFilter::Nearest, SamplerAddressMode::Mirror, false},
  {3u, SamplerFilter::Linear, SamplerAddressMode::Mirror, true},
  {4u, SamplerFilter::Nearest, SamplerAddressMode::Clamp, false},
  {5u, SamplerFilter::Linear, SamplerAddressMode::Clamp, true},
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
    u32 size_, u32 binding_, const char* name_):
    device{device_},
    set{set_},
    type{type_},
    binding{binding_},
    size{size_},
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

    if(freeList.empty()) {
      throw std::runtime_error(
        formatString("Descriptor heap '%s' exhausted", name));
    }

    const auto index = freeList.back();
    freeList.pop_back();

    return index;
  }

  void free(u32 idx)
  {
    std::scoped_lock lock(mutex);
    freeList.push_back(idx);
  }

  Binder::Stats stats()
  {
    std::scoped_lock lock(mutex);
    return {
      .allocated = size - static_cast<u32>(freeList.size()),
      .capacity  = size};
  }
};

Binder::Binder(Device& device_, const Desc& desc_):
  m_device{device_},
  m_desc{desc_},
  m_mutex{},
  m_heaps{},
  m_staticSamplers{},
  m_descriptorPool{},
  m_descriptorSetLayout{},
  m_descriptorSet{},
  m_pipelineLayout{}
{
  const auto& props = m_device.GetPhysicalDevice().GetProperties();

  Arr<VkDescriptorSetLayoutBinding> bindings;
  Arr<VkSampler>                    staticSamplers;
  const auto& props12 = props.properties12;

  SArr<u32, vd::size32(BindlessSlots)> heapCapacities = {
    std::min({
      props12.maxDescriptorSetUpdateAfterBindSamplers,
      props12.maxPerStageDescriptorUpdateAfterBindSamplers,
      m_desc.maxSamplerCount}),
    std::min({
      props12.maxDescriptorSetUpdateAfterBindStorageBuffers,
      props12.maxPerStageDescriptorUpdateAfterBindStorageBuffers,
      m_desc.maxDescriptorCount}),
    std::min({
      props12.maxDescriptorSetUpdateAfterBindSampledImages,
      props12.maxPerStageDescriptorUpdateAfterBindSampledImages,
      m_desc.maxDescriptorCount}),
    std::min({
      props12.maxDescriptorSetUpdateAfterBindStorageImages,
      props12.maxPerStageDescriptorUpdateAfterBindStorageImages,
      m_desc.maxDescriptorCount})};

  const u32 resourceLimit = props12.maxPerStageUpdateAfterBindResources;
  if(
    resourceLimit < heapCapacities.size() ||
    std::ranges::any_of(
      heapCapacities, [](u32 capacity) { return capacity == 0u; })) {
    throw std::runtime_error(
      "Insufficient Vulkan update-after-bind descriptor capacity");
  }

  SArr<u32, vd::size32(BindlessSlots)> allocatedCapacities{};
  u32 remaining = resourceLimit;
  for(u32 idx = 0u; idx < allocatedCapacities.size(); ++idx) {
    const u32 slotsLeft = size32(allocatedCapacities) - idx;
    const u32 fairShare = remaining / slotsLeft;
    allocatedCapacities[idx] =
      std::max(1u, std::min(heapCapacities[idx], fairShare));
    remaining -= allocatedCapacities[idx];
  }
  for(u32 idx = 0u; idx < allocatedCapacities.size(); ++idx) {
    const u32 extra = std::min(
      remaining, heapCapacities[idx] - allocatedCapacities[idx]);
    allocatedCapacities[idx] += extra;
    remaining -= extra;
  }

  Map<VkDescriptorType, u32> heapSizes;
  for(u32 idx = 0u; idx < vd::size32(BindlessSlots); ++idx)
    heapSizes[BindlessSlots[idx].type] = allocatedCapacities[idx];

  { // Descriptor pool
    // Static samplers also consume SAMPLER descriptors from the pool.
    VkDescriptorPoolSize poolSizes[] = {
      {VK_DESCRIPTOR_TYPE_SAMPLER,
       heapSizes[VK_DESCRIPTOR_TYPE_SAMPLER] +
         vd::size32(StaticSamplers)},
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

  { // Bindings: Static samplers (binding 0-5)
    const bool anisotropySupported =
      props.properties.properties.limits.maxSamplerAnisotropy >= 1.f &&
      m_device.GetPhysicalDevice().GetFeatures()->samplerAnisotropy;
    const f32 anisotropyMax = anisotropySupported
                                ? std::min(
                                    16.f,
                                    props.properties.properties.limits
                                      .maxSamplerAnisotropy)
                                : 0.f;

    for(const auto& sampler: StaticSamplers) {
      Sampler::Desc desc{};
      desc.minFilter = desc.magFilter = desc.mipFilter = sampler.filter;
      desc.u = desc.v = desc.w = sampler.mode;
      if(sampler.anisotropy && anisotropySupported) {
        desc.anisotropyEnable = true;
        desc.anisotropyMax    = anisotropyMax;
      }

      m_staticSamplers.push_back(
        UPtr<Sampler>{new Sampler(m_device, desc, false)});

      auto& bnd           = bindings.emplace_back();
      bnd.binding         = sampler.binding;
      bnd.descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER;
      bnd.stageFlags      = VK_SHADER_STAGE_ALL;
      bnd.descriptorCount = 1;
      bnd.pImmutableSamplers =
        &m_staticSamplers.back()->GetView().handle;

      bindingFlags.push_back(0);
    }
  }

  // Bindless slots
  for(auto& slot: BindlessSlots) {
    auto& bnd = bindings.emplace_back();

    bnd.descriptorType     = slot.type;
    bnd.binding            = slot.binding;
    bnd.descriptorCount    = heapSizes[slot.type];
    bnd.stageFlags         = VK_SHADER_STAGE_ALL;
    bnd.pImmutableSamplers = nullptr;

    bindingFlags.push_back(
      VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
      VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
    );
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

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_descriptorPool;
    allocInfo.descriptorSetCount = 1u;
    allocInfo.pSetLayouts        = &m_descriptorSetLayout;

    VDVkTry(m_device.api().AllocateDescriptorSets(
      &allocInfo, &m_descriptorSet));
  }

  // Heaps
  for(auto& slot: BindlessSlots)
    m_heaps.push_back(
      std::make_unique<Heap>(
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
  std::scoped_lock lock(m_mutex);
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

  VDLogV(
    "Bind sampler #%u %s[%u]", heap.binding, heap.name, binding.index);
  m_device.api().UpdateDescriptorSets(1u, &write, 0u, nullptr);

  return binding;
}

DescriptorBinding Binder::Bind(const Buffer::View& view)
{
  std::scoped_lock lock(m_mutex);
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

  VDLogV(
    "Bind buffer #%u %s[%u] to: %s", heap.binding, heap.name,
    binding.index, view.GetResourceName());
  m_device.api().UpdateDescriptorSets(1u, &write, 0u, nullptr);

  return binding;
}

DescriptorBinding Binder::Bind(const Image::View& view)
{
  std::scoped_lock lock(m_mutex);
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

  VDLogV(
    "Bind image #%u %s[%u] to: %s", heap->binding, heap->name,
    binding.index, view.GetResourceName());
  m_device.api().UpdateDescriptorSets(1u, &write, 0u, nullptr);

  return binding;
}

void Binder::Unbind(const DescriptorBinding& binding)
{
  std::scoped_lock lock(m_mutex);
  if(!binding.IsValid()) return;
  u32 activeSlots = 0u;
  for(const auto& [context, slots]: m_frameContexts) {
    VD_UNUSED(context);
    for(const auto& slot: slots)
      if(slot.active) ++activeSlots;
  }
  if(activeSlots == 0u) {
    freeBinding(binding);
    return;
  }

  auto pending            = std::make_shared<PendingUnbind>();
  pending->binding        = binding;
  pending->remainingSlots = activeSlots;
  for(auto& [context, slots]: m_frameContexts) {
    VD_UNUSED(context);
    for(auto& slot: slots)
      if(slot.active) slot.pending.push_back(pending);
  }
}

Binder::FrameContextId Binder::RegisterFrameContext(u32 slotCount)
{
  std::scoped_lock lock(m_mutex);
  const FrameContextId id = m_nextFrameContext++;
  m_frameContexts[id].resize(std::max(1u, slotCount));
  return id;
}

void Binder::UnregisterFrameContext(FrameContextId context)
{
  std::scoped_lock lock(m_mutex);
  auto it = m_frameContexts.find(context);
  if(it == m_frameContexts.end()) return;
  for(u32 slot = 0u; slot < it->second.size(); ++slot)
    retireSlot(context, slot);
  m_frameContexts.erase(context);
}

void Binder::BeginFrame(FrameContextId context, u32 slot)
{
  std::scoped_lock lock(m_mutex);
  auto it = m_frameContexts.find(context);
  if(it == m_frameContexts.end())
    throw std::runtime_error("Binder: unknown frame context");
  auto& frameSlot = it->second[slot % it->second.size()];
  if(frameSlot.active)
    throw std::runtime_error("Binder: frame slot is already active");
  frameSlot.active = true;
}

void Binder::RetireFrame(FrameContextId context, u32 slot)
{
  std::scoped_lock lock(m_mutex);
  retireSlot(context, slot);
}

void Binder::FlushDeferred(FrameContextId context)
{
  std::scoped_lock lock(m_mutex);
  auto it = m_frameContexts.find(context);
  if(it == m_frameContexts.end()) return;
  for(u32 slot = 0u; slot < it->second.size(); ++slot)
    retireSlot(context, slot);
}

void Binder::retireSlot(FrameContextId context, u32 slot)
{
  auto it = m_frameContexts.find(context);
  if(it == m_frameContexts.end()) return;
  auto& frameSlot = it->second[slot % it->second.size()];
  if(!frameSlot.active) return;

  frameSlot.active = false;
  for(auto& pending: frameSlot.pending) {
    if(--pending->remainingSlots == 0u) freeBinding(pending->binding);
  }
  frameSlot.pending.clear();
}

Binder::Stats Binder::GetStats()
{
  std::scoped_lock lock(m_mutex);
  Stats result{};
  for(auto& heap: m_heaps) {
    const auto heapStats = heap->stats();
    result.allocated += heapStats.allocated;
    result.capacity += heapStats.capacity;
  }
  return result;
}

void Binder::freeBinding(const DescriptorBinding& binding)
{
  getHeap(binding.type).free(binding.index);
}

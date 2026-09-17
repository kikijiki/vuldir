#include "vuldir/api/Device.hpp"
#include "vuldir/api/MemoryPool.hpp"
#include "vuldir/api/PhysicalDevice.hpp"
#include "vuldir/api/vk/VkDispatcher.hpp"
#include "vuldir/api/vk/VkUti.hpp"

using namespace vd;

MemoryPool::MemoryPool(
  vd::Device& device, MemoryType type, u64 capacity, u32 idx,
  u32 memoryTypeBits):
  m_device{device},
  m_type{type},
  m_name{},
  m_debugVerbose{false},
  m_typeIdx{0xffff},
  m_props{},
  m_handle{},
  m_freeBlocks{},
  m_mutex{},
  m_capacity{capacity},
  m_usedSize{capacity},
  m_freeSize{0u},
  m_maxAllocSize{0u}
{
  if(capacity == 0u)
    throw std::invalid_argument("Memory pool capacity must be non-zero");
  auto& physicalDevice = m_device.GetPhysicalDevice();
  auto& memProps       = physicalDevice.GetMemoryProperties();
  u32   requiredProps  = 0u;
  u32   fallbackProps  = 0u;

  switch(m_type) {
    case MemoryType::Main:
      m_name        = formatString("Main %u", idx);
      requiredProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      break;
    case MemoryType::Upload:
      m_name        = formatString("Upload %u", idx);
      requiredProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      fallbackProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
      break;
    case MemoryType::Download:
      m_name        = formatString("Download %u", idx);
      requiredProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                      VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
      fallbackProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      break;
    default:
      throw std::runtime_error("Unknown memory type");
  }

  for(u32 memIdx = 0u; memIdx < memProps->memoryTypeCount; ++memIdx) {
    if((memoryTypeBits & (1u << memIdx)) == 0u) continue;

    auto props   = memProps->memoryTypes[memIdx].propertyFlags;
    auto heapIdx = memProps->memoryTypes[memIdx].heapIndex;
    auto heap    = memProps->memoryHeaps[heapIdx];

    bool hasProps = vd::hasFlag(props, requiredProps);
    bool hasSize  = heap.size >= capacity;

    if(hasProps && hasSize) {
      m_typeIdx = memIdx;
      m_props   = props;
      break;
    }
  }

  // Main may use any allowed type. Upload and Download must remain
  // host-visible; Download may only relax the HOST_CACHED preference.
  if(m_typeIdx == 0xffff) {
    for(u32 memIdx = 0u; memIdx < memProps->memoryTypeCount; ++memIdx) {
      if((memoryTypeBits & (1u << memIdx)) == 0u) continue;
      auto props   = memProps->memoryTypes[memIdx].propertyFlags;
      auto heapIdx = memProps->memoryTypes[memIdx].heapIndex;
      auto heap    = memProps->memoryHeaps[heapIdx];
      if(heap.size < capacity) continue;
      if(!vd::hasFlag(props, fallbackProps)) continue;
      m_typeIdx = memIdx;
      m_props   = props;
      break;
    }
  }

  if(m_typeIdx == 0xffff)
    throw std::runtime_error("No suitable memory type found");

  VkMemoryAllocateInfo info{
    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize  = m_capacity,
    .memoryTypeIndex = m_typeIdx};

  VDVkTry(device.api().AllocateMemory(&info, &m_handle));
  pushFreeBlock({0u, m_capacity});
}

MemoryPool::~MemoryPool()
{
  if(m_usedSize > 0u)
    VDLogE(
      "[MemoryPool %s]: %llu bytes leaked.", m_name.c_str(),
      m_usedSize);

  if(m_handle) {
    m_device.api().FreeMemory(m_handle);
    m_handle = nullptr;
  }

  m_freeBlocks.clear();
  m_usedSize     = 0u;
  m_freeSize     = 0u;
  m_maxAllocSize = 0u;
}

VkResult MemoryPool::Bind(
  VkBuffer buffer, const Allocation& allocation)
{
  if(allocation.pool != this)
    throw std::invalid_argument(
      "Cannot bind an allocation from a different memory pool");

  std::scoped_lock lock(m_mutex);
  return m_device.api().BindBufferMemory(
    buffer, m_handle, allocation.offset);
}

VkResult MemoryPool::Bind(
  VkImage image, const Allocation& allocation)
{
  if(allocation.pool != this)
    throw std::invalid_argument(
      "Cannot bind an allocation from a different memory pool");

  std::scoped_lock lock(m_mutex);
  return m_device.api().BindImageMemory(
    image, m_handle, allocation.offset);
}

bool MemoryPool::Write(Allocation& alloc, Span<u8 const> data)
{
  std::scoped_lock lock(m_mutex);

  const auto dataSize = data.size_bytes();

  if(alloc.pool != this)
    throw std::invalid_argument(
      "Cannot write an allocation from a different memory pool");
  if(
    alloc.offset > m_capacity || alloc.size > m_capacity - alloc.offset)
    throw std::invalid_argument("Memory allocation range is invalid");

  if(!vd::hasFlag(m_props, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
    VDLogE(
      "[MemoryPool %s]: Cannot map memory that is not HOST VISIBLE.",
      m_name.c_str());
    return false;
  }

  if(dataSize > alloc.size) {
    VDLogE(
      "[MemoryPool %s]: Not enough space to allocate memory.",
      m_name.c_str());
    return false;
  }

  if(dataSize == 0u) return true;

  const bool coherent =
    vd::hasFlag(m_props, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  const u64 atomSize = m_device.GetPhysicalDevice()
                         .GetProperties()
                         ->limits.nonCoherentAtomSize;
  const u64 mapOffset = coherent ? alloc.offset
                                 : alignDown(alloc.offset, atomSize);
  const u64 dataEnd = alloc.offset + dataSize;
  const u64 endPadding =
    (atomSize - (dataEnd % atomSize)) % atomSize;
  const u64 mapEnd = coherent
                       ? dataEnd
                       : dataEnd +
                           std::min(endPadding, m_capacity - dataEnd);
  const u64 mapSize = mapEnd - mapOffset;

  void*      pDst{};
  const auto mapResult = m_device.api().MapMemory(
    m_handle, mapOffset, mapSize, 0, &pDst);
  if(mapResult != VK_SUCCESS) {
    VDLogE(
      "[MemoryPool %s] Could not map device memory: %s", m_name.c_str(),
      toString(mapResult));
    return false;
  }

  auto* writePtr = static_cast<u8*>(pDst) + (alloc.offset - mapOffset);
  std::memcpy(writePtr, data.data(), dataSize);

  if(!coherent) {
    VkMappedMemoryRange range;

    range.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.pNext  = nullptr;
    range.memory = m_handle;
    range.offset = mapOffset;
    range.size   = mapSize;

    auto flushResult =
      m_device.api().FlushMappedMemoryRanges(1u, &range);
    if(flushResult != VK_SUCCESS) {
      VDLogE(
        "[MemoryPool %s] Could not flush mapped device memory: %s",
        m_name.c_str(), toString(flushResult));
      m_device.api().UnmapMemory(m_handle);
      return false;
    }
  }

  m_device.api().UnmapMemory(m_handle);

  return true;
}

bool MemoryPool::Read(const Allocation& alloc, Span<u8> data)
{
  std::scoped_lock lock(m_mutex);

  const auto dataSize = data.size_bytes();
  if(alloc.pool != this)
    throw std::invalid_argument(
      "Cannot read an allocation from a different memory pool");
  if(
    alloc.offset > m_capacity || alloc.size > m_capacity - alloc.offset)
    throw std::invalid_argument("Memory allocation range is invalid");
  if(!vd::hasFlag(m_props, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
    VDLogE(
      "[MemoryPool %s]: Cannot map memory that is not HOST VISIBLE.",
      m_name.c_str());
    return false;
  }
  if(dataSize > alloc.size) {
    VDLogE(
      "[MemoryPool %s]: Read destination exceeds the allocation.",
      m_name.c_str());
    return false;
  }
  if(dataSize == 0u) return true;

  const bool coherent =
    vd::hasFlag(m_props, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  const u64 atomSize = m_device.GetPhysicalDevice()
                         .GetProperties()
                         ->limits.nonCoherentAtomSize;
  const u64 mapOffset = coherent ? alloc.offset
                                 : alignDown(alloc.offset, atomSize);
  const u64 dataEnd = alloc.offset + dataSize;
  const u64 endPadding =
    (atomSize - (dataEnd % atomSize)) % atomSize;
  const u64 mapEnd = coherent
                       ? dataEnd
                       : dataEnd +
                           std::min(endPadding, m_capacity - dataEnd);
  const u64 mapSize = mapEnd - mapOffset;

  void* mapped = nullptr;
  const auto mapResult = m_device.api().MapMemory(
    m_handle, mapOffset, mapSize, 0u, &mapped);
  if(mapResult != VK_SUCCESS) {
    VDLogE(
      "[MemoryPool %s] Could not map device memory: %s", m_name.c_str(),
      toString(mapResult));
    return false;
  }

  if(!coherent) {
    const VkMappedMemoryRange range{
      .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
      .memory = m_handle,
      .offset = mapOffset,
      .size   = mapSize};
    const auto invalidateResult =
      m_device.api().InvalidateMappedMemoryRanges(1u, &range);
    if(invalidateResult != VK_SUCCESS) {
      VDLogE(
        "[MemoryPool %s] Could not invalidate mapped device memory: %s",
        m_name.c_str(), toString(invalidateResult));
      m_device.api().UnmapMemory(m_handle);
      return false;
    }
  }

  const auto* readPtr =
    static_cast<const u8*>(mapped) + (alloc.offset - mapOffset);
  std::memcpy(data.data(), readPtr, dataSize);
  m_device.api().UnmapMemory(m_handle);
  return true;
}

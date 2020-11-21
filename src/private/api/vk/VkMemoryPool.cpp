#include "vuldir/api/Device.hpp"
#include "vuldir/api/MemoryPool.hpp"
#include "vuldir/api/PhysicalDevice.hpp"
#include "vuldir/api/vk/VkDispatcher.hpp"
#include "vuldir/api/vk/VkUti.hpp"

using namespace vd;

MemoryPool::MemoryPool(
  vd::Device& device, MemoryType type, u64 capacity, u32 idx):
  m_device{device},
  m_type{type},
  m_name{},
  m_debugVerbose{false},
  m_typeIdx{0xffff},
  m_props{},
  m_handle{},
  m_freeBlocks{},
  m_capacity{capacity},
  m_usedSize{capacity},
  m_freeSize{0u},
  m_maxAllocSize{0u}
{
  auto& physicalDevice = m_device.GetPhysicalDevice();
  auto& memProps       = physicalDevice.GetMemoryProperties();
  u32   requiredProps  = 0u;

  switch(m_type) {
    case MemoryType::Main:
      m_name        = formatString("Main %u", idx);
      requiredProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      break;
    case MemoryType::Upload:
      m_name        = formatString("Upload %u", idx);
      requiredProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      break;
    case MemoryType::Download:
      m_name        = formatString("Download %u", idx);
      requiredProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                      VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
      break;
    default:
      throw std::runtime_error("Unknown memory type");
  }

  for(u32 memIdx = 0u; memIdx < memProps->memoryTypeCount; ++memIdx) {
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

bool MemoryPool::Write(Allocation& alloc, Span<u8 const> data)
{
  const auto dataSize = data.size_bytes();

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

  void*      pDst{};
  const auto mapResult = m_device.api().MapMemory(
    m_handle, alloc.offset, dataSize, 0, &pDst);
  if(mapResult != VK_SUCCESS) {
    VDLogE(
      "[MemoryPool %s] Could not map device memory: %s", m_name.c_str(),
      toString(mapResult));
    return false;
  }

  std::memcpy(pDst, data.data(), dataSize);
  m_device.api().UnmapMemory(m_handle);

  if(!vd::hasFlag(m_props, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
    VkMappedMemoryRange range;

    range.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.pNext  = nullptr;
    range.memory = m_handle;
    range.offset = alloc.offset;
    range.size   = dataSize;

    auto flushResult =
      m_device.api().FlushMappedMemoryRanges(1u, &range);
    if(flushResult != VK_SUCCESS) {
      VDLogE(
        "[MemoryPool %s] Could not flush mapped device memory: %s",
        m_name.c_str(), toString(flushResult));
      return false;
    }
  }

  return true;
}

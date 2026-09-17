#include "api/vk/VkHostAllocator.hpp"

using namespace vd;

HostAllocator::HostAllocator():
  m_callbacks(), m_usage(0u), m_valid{true}
{
  m_callbacks.pUserData       = this;
  m_callbacks.pfnAllocation   = &HostAllocator::vkAllocation;
  m_callbacks.pfnReallocation = &HostAllocator::vkReallocation;
  m_callbacks.pfnFree         = &HostAllocator::vkFree;
  m_callbacks.pfnInternalAllocation =
    &HostAllocator::vkInternalAllocationNotification;
  m_callbacks.pfnInternalFree =
    &HostAllocator::vkInternalFreeNotification;
}

HostAllocator::~HostAllocator()
{
  if(!m_valid) return;

  const auto usage = m_usage.load(std::memory_order_seq_cst);
  if(usage > 0u) VDLogE("HostAllocator: %llu bytes leaked.", usage);

  memset(&m_callbacks, 0, sizeof(VkAllocationCallbacks));
  m_usage.store(0u);
  m_valid = false;
}

u64 HostAllocator::GetUsage() const
{
  return m_usage.load(std::memory_order_relaxed);
}

HostAllocator::AllocationResult HostAllocator::internalAllocate(
  const size_t size, const size_t alignment)
{
  if(size == 0u) return {nullptr, 0u};

  if(
    alignment == 0u || (alignment & (alignment - 1u)) != 0u ||
    alignment > MaxU64 - sizeof(AllocationHeader) ||
    size > MaxU64 - sizeof(AllocationHeader) - alignment)
    return {nullptr, 0u};
  const auto allocationSize = size + alignment + sizeof(AllocationHeader);
  const auto pAllocation = ::malloc(allocationSize);

  if(!pAllocation) return {nullptr, 0u};

  char* pAllocationChar = reinterpret_cast<char*>(pAllocation);
  char* pAlignedMemory  = pAllocationChar + sizeof(AllocationHeader);

  // Adjust the space value accordingly
  std::size_t space =
    allocationSize - (pAlignedMemory - pAllocationChar);

  // Align the memory
  void* pAlignedAddress = pAlignedMemory;
  if(!std::align(alignment, size, pAlignedAddress, space)) {
    ::free(pAllocation);
    return {nullptr, 0u}; // Alignment failed, return null
  }

  pAlignedMemory = reinterpret_cast<char*>(pAlignedAddress);

  // Calculate header position
  auto* pHeader = reinterpret_cast<AllocationHeader*>(
    pAlignedMemory - sizeof(AllocationHeader));
  pHeader->pAllocation   = pAllocation;
  pHeader->allocationSize = allocationSize;
  pHeader->payloadSize    = size;
  pHeader->alignment      = alignment;

  m_usage.fetch_add(pHeader->allocationSize, std::memory_order_relaxed);

  return {reinterpret_cast<void*>(pAlignedMemory), allocationSize};
}

HostAllocator::AllocationResult HostAllocator::internalReallocate(
  const void* pOriginal, const size_t size, const size_t alignment)
{
  if(!pOriginal) return internalAllocate(size, alignment);

  if(size == 0u) {
    internalFree(pOriginal);
    return {nullptr, 0u};
  }

  const auto newAllocation = internalAllocate(size, alignment);
  if(!newAllocation.pMemory) return {nullptr, 0u};

  memcpy(
    newAllocation.pMemory, pOriginal,
    std::min(size, getAllocationSize(pOriginal)));
  internalFree(pOriginal);

  return newAllocation;
}

void HostAllocator::internalFree(const void* pMemory)
{
  if(!pMemory) return;
  const auto* pHeader = getAllocationHeader(pMemory);
  m_usage.fetch_sub(pHeader->allocationSize, std::memory_order_relaxed);
  ::free(pHeader->pAllocation);
}

HostAllocator::AllocationHeader*
HostAllocator::getAllocationHeader(const void* pMemory)
{
  const char* pHeaderAddress =
    reinterpret_cast<const char*>(pMemory) - sizeof(AllocationHeader);
  return reinterpret_cast<AllocationHeader*>(
    const_cast<char*>(pHeaderAddress));
}

size_t HostAllocator::getAllocationSize(const void* pMemory)
{
  if(!pMemory) return 0u;
  return getAllocationHeader(pMemory)->payloadSize;
}

size_t HostAllocator::getAllocationAlignment(const void* pMemory)
{
  if(!pMemory) return 0u;
  return getAllocationHeader(pMemory)->alignment;
}

void* VKAPI_CALL HostAllocator::vkAllocation(
  void* pUserData, size_t size, size_t alignment,
  [[maybe_unused]] VkSystemAllocationScope allocationScope)
{
  auto&      self       = *reinterpret_cast<HostAllocator*>(pUserData);
  const auto allocation = self.internalAllocate(size, alignment);
  return allocation.pMemory;
}

void* VKAPI_CALL HostAllocator::vkReallocation(
  void* pUserData, void* pOriginal, size_t size, size_t alignment,
  [[maybe_unused]] VkSystemAllocationScope allocationScope)
{
  auto&      self = *reinterpret_cast<HostAllocator*>(pUserData);
  const auto allocation =
    self.internalReallocate(pOriginal, size, alignment);
  return allocation.pMemory;
}

void VKAPI_CALL HostAllocator::vkFree(void* pUserData, void* pMemory)
{
  auto& self = *reinterpret_cast<HostAllocator*>(pUserData);

  [[maybe_unused]] auto size = getAllocationSize(pMemory);

  self.internalFree(pMemory);
}

void VKAPI_CALL HostAllocator::vkInternalAllocationNotification(
  void* pUserData, size_t size,
  [[maybe_unused]] VkInternalAllocationType allocationType,
  [[maybe_unused]] VkSystemAllocationScope  allocationScope)
{
  auto& self = *reinterpret_cast<HostAllocator*>(pUserData);
  self.m_usage.fetch_add(size, std::memory_order_relaxed);
}

void VKAPI_CALL HostAllocator::vkInternalFreeNotification(
  void* pUserData, size_t size,
  [[maybe_unused]] VkInternalAllocationType allocationType,
  [[maybe_unused]] VkSystemAllocationScope  allocationScope)
{
  auto& self = *reinterpret_cast<HostAllocator*>(pUserData);
  self.m_usage.fetch_sub(size, std::memory_order_relaxed);
}

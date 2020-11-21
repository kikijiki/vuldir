#pragma once

#include "vuldir/api/Common.hpp"

namespace vd {

class HostAllocator
{
public:
  VD_NONMOVABLE(HostAllocator);

  HostAllocator();
  ~HostAllocator();

public:
  u64 GetUsage() const;

public:
  const VkAllocationCallbacks* GetAllocationCallbacks() const
  {
    return &m_callbacks;
  }

private:
  struct AllocationHeader {
    void*  pAllocation;
    size_t size;
    size_t alignment;
  };

  struct AllocationResult {
    void*  pMemory;
    size_t size;
  };

  AllocationResult
  internalAllocate(const size_t size, const size_t alignment);
  AllocationResult internalReallocate(
    const void* pOriginal, const size_t size, const size_t alignment);
  void                     internalFree(const void* pMemory);
  static AllocationHeader* getAllocationHeader(const void* pMemory);
  static size_t            getAllocationSize(const void* pMemory);
  static size_t            getAllocationAlignment(const void* pMemory);

private:
  static void* VKAPI_CALL vkAllocation(
    void* pUserData, size_t size, size_t alignment,
    VkSystemAllocationScope allocationScope);

  static void* VKAPI_CALL vkReallocation(
    void* pUserData, void* pOriginal, size_t size, size_t alignment,
    VkSystemAllocationScope allocationScope);

  static void VKAPI_CALL vkFree(void* pUserData, void* pMemory);

  static void VKAPI_CALL vkInternalAllocationNotification(
    void* pUserData, size_t size,
    VkInternalAllocationType allocationType,
    VkSystemAllocationScope  allocationScope);

  static void VKAPI_CALL vkInternalFreeNotification(
    void* pUserData, size_t size,
    VkInternalAllocationType allocationType,
    VkSystemAllocationScope  allocationScope);

private:
  VkAllocationCallbacks m_callbacks;
  std::atomic<u64>      m_usage;
  bool                  m_valid;
};
}; // namespace vd

#pragma once

#include "vuldir/api/Common.hpp"

namespace vd {
class Device;

class MemoryPool
{
public:
  struct Allocation {
    MemoryPool* pool;

    u64 offset;
    u64 size;
    u64 blockOffset;
    u64 blockSize;

    bool IsValid() const { return pool != nullptr; }
    void Free()
    {
      if(IsValid()) {
        pool->Free(*this);
        Invalidate();
      }
    }
    void Invalidate() { pool = nullptr; }
  };

public:
  VD_NONMOVABLE(MemoryPool);

  MemoryPool(
    vd::Device& device, MemoryType type, u64 capacity, u32 idx);
  ~MemoryPool();

public:
  Allocation Allocate(u64 size, u64 alignment = 1u);
  void       Free(const Allocation& allocation);

  bool Write(Allocation& alloc, Span<u8 const> data);

  MemoryType GetType() const { return m_type; }

  u64 GetCapacity() const { return m_capacity; }
  u64 GetFreeSize() const { return m_freeSize; }
  u64 GetUsedSize() const { return m_usedSize; }
  u64 GetMaxAllocSize() const { return m_maxAllocSize; }

  bool IsValid() const { return m_handle; }
  bool IsEmpty() const { return m_usedSize == 0u; }

#ifdef VD_API_VK
  VkDeviceMemory GetHandle() { return m_handle; }
#elif VD_API_DX
  ID3D12Heap& GetHandle() { return *m_handle.Get(); }
#endif

private:
  struct Block {
    u64 offset;
    u64 size;
  };

private:
  void pushFreeBlock(const Block& newBlock);
  void deleteFreeBlock(const Arr<Block>::iterator& blockIt);
  void sortFreeBlocks();

private:
  void logDetailedUsage();

private:
  vd::Device& m_device;
  MemoryType  m_type;

  Str  m_name;
  bool m_debugVerbose;

#ifdef VD_API_VK
  u32                   m_typeIdx;
  VkMemoryPropertyFlags m_props;
  VkDeviceMemory        m_handle;
#elif VD_API_DX
  D3D12_HEAP_DESC    m_desc;
  ComPtr<ID3D12Heap> m_handle;
#endif

  Arr<Block> m_freeBlocks;

  u64 m_capacity;
  u64 m_usedSize;
  u64 m_freeSize;
  u64 m_maxAllocSize;
};
} // namespace vd

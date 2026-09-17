#pragma once

#include "vuldir/api/Common.hpp"

namespace vd {
class Device;

class MemoryPool
{
public:
  struct Allocation {
    Allocation() = default;
    ~Allocation() { Free(); }

    Allocation(const Allocation&)            = delete;
    Allocation& operator=(const Allocation&) = delete;

    Allocation(Allocation&& other) noexcept:
      pool{std::exchange(other.pool, nullptr)},
      offset{other.offset},
      size{other.size},
      blockOffset{other.blockOffset},
      blockSize{other.blockSize}
    {}

    Allocation& operator=(Allocation&& other) noexcept
    {
      if(this == &other) return *this;
      Free();
      pool        = std::exchange(other.pool, nullptr);
      offset      = other.offset;
      size        = other.size;
      blockOffset = other.blockOffset;
      blockSize   = other.blockSize;
      return *this;
    }

    MemoryPool* pool = nullptr;

    u64 offset      = 0u;
    u64 size        = 0u;
    u64 blockOffset = 0u;
    u64 blockSize   = 0u;

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
    vd::Device& device, MemoryType type, u64 capacity, u32 idx
#ifdef VD_API_VK
    ,
    u32 memoryTypeBits = ~0u
#endif
  );
  ~MemoryPool();

public:
  Allocation Allocate(u64 size, u64 alignment = 1u);

  bool Write(Allocation& alloc, Span<u8 const> data);
#ifdef VD_API_VK
  bool Read(const Allocation& alloc, Span<u8> data);
#endif

  MemoryType GetType() const { return m_type; }
#ifdef VD_API_VK
  u32  GetMemoryTypeIndex() const { return m_typeIdx; }
  bool SupportsMemoryTypeBits(u32 bits) const
  {
    return (bits & (1u << m_typeIdx)) != 0u;
  }
  VkResult Bind(VkBuffer buffer, const Allocation& allocation);
  VkResult Bind(VkImage image, const Allocation& allocation);
#endif

  u64 GetCapacity() const { return m_capacity; }
  u64 GetFreeSize() const
  {
    std::scoped_lock lock(m_mutex);
    return m_freeSize;
  }
  u64 GetUsedSize() const
  {
    std::scoped_lock lock(m_mutex);
    return m_usedSize;
  }
  u64 GetMaxAllocSize() const
  {
    std::scoped_lock lock(m_mutex);
    return m_maxAllocSize;
  }

  bool IsValid() const { return m_handle; }
  bool IsEmpty() const
  {
    std::scoped_lock lock(m_mutex);
    return m_usedSize == 0u;
  }

#ifdef VD_API_VK
  VkDeviceMemory GetHandle() { return m_handle; }
#elif VD_API_DX
  ID3D12Heap& GetHandle() { return *m_handle.Get(); }
#endif

private:
  void Free(const Allocation& allocation);

  struct Block {
    u64 offset;
    u64 size;
  };

private:
  void pushFreeBlock(const Block& newBlock);
  void deleteFreeBlock(const Arr<Block>::iterator& blockIt);
  void updateMaxAllocSize();

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
  mutable std::mutex m_mutex;

  u64 m_capacity;
  u64 m_usedSize;
  u64 m_freeSize;
  u64 m_maxAllocSize;
};
} // namespace vd

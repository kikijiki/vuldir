#include "vuldir/api/Device.hpp"

using namespace vd;

MemoryPool::Allocation
Device::AllocateMemory(MemoryType type, u64 size, u64 alignment)
{
  std::scoped_lock lock(m_memoryMutex);

  // Try existing pools.
  for(auto& pool: m_memoryPools) {
    if(pool->GetType() != type) continue;
    auto alloc = pool->Allocate(size, alignment);
    if(alloc.IsValid()) return alloc;
  }

  // All pools failed, add a new one.
  u64 defaultPoolSize;
  switch(type) {
    case MemoryType::Main:
      defaultPoolSize = m_desc.memMainPoolSize;
      break;
    case MemoryType::Upload:
      defaultPoolSize = m_desc.memUploadPoolSize;
      break;
    case MemoryType::Download:
      defaultPoolSize = m_desc.memDownloadPoolSize;
      break;
  }

  auto pool = std::make_unique<MemoryPool>(
    *this, type, std::max(size, defaultPoolSize),
    static_cast<u32>(m_memoryPools.size()));
  auto alloc = pool->Allocate(size, alignment);

  m_memoryPools.push_back(std::move(pool));
  return alloc;
}
#include "vuldir/api/Device.hpp"

#include "vuldir/api/Binder.hpp"
#include "vuldir/api/Buffer.hpp"
#include "vuldir/api/CommandBuffer.hpp"
#include "vuldir/api/Image.hpp"

using namespace vd;

void Device::validateResourceStates(
  const Arr<CommandBuffer*>& cmds) const
{
  Map<Buffer*, ResourceState> bufferStates;
  Map<Image*, ResourceState>  imageStates;

  for(const auto* cmd: cmds) {
    if(!cmd)
      throw std::invalid_argument("Cannot submit a null command buffer");

    for(const auto& [buffer, pending]: cmd->m_barriers.pendingBuffers) {
      const auto found = bufferStates.find(buffer);
      const ResourceState current =
        found == bufferStates.end() ? buffer->GetState() : found->second;
      if(current != pending.initial)
        throw std::runtime_error(
          "Command buffer has stale buffer resource state");
      bufferStates[buffer] = pending.final;
    }
    for(const auto& [image, pending]: cmd->m_barriers.pendingImages) {
      const auto found = imageStates.find(image);
      const ResourceState current =
        found == imageStates.end() ? image->GetState() : found->second;
      if(current != pending.initial)
        throw std::runtime_error(
          "Command buffer has stale image resource state");
      imageStates[image] = pending.final;
    }
  }
}

Device::Stats Device::GetStats()
{
  Stats result{
    .buffers       = m_bufferCount.load(),
    .images        = m_imageCount.load(),
    .pipelines     = m_pipelineCount.load(),
    .shadersLoaded = m_shaderLoadCount.load()};

  {
    std::scoped_lock lock(m_memoryMutex);
    for(const auto& pool: m_memoryPools) {
      if(pool->GetType() != MemoryType::Main) continue;
      result.gpuMemoryUsed += pool->GetUsedSize();
      result.gpuMemoryCommitted += pool->GetCapacity();
      ++result.gpuMemoryPools;
    }
  }

  const auto descriptorStats = m_binder->GetStats();
  result.descriptorsUsed     = descriptorStats.allocated;
  result.descriptorsCapacity = descriptorStats.capacity;
  return result;
}

MemoryPool::Allocation Device::AllocateMemory(
  MemoryType type, u64 size, u64 alignment, u32 memoryTypeBits)
{
  std::scoped_lock lock(m_memoryMutex);

  for(auto& pool: m_memoryPools) {
    if(pool->GetType() != type) continue;
#ifdef VD_API_VK
    if(!pool->SupportsMemoryTypeBits(memoryTypeBits)) continue;
#else
    (void)memoryTypeBits;
#endif
    auto alloc = pool->Allocate(size, alignment);
    if(alloc.IsValid()) return alloc;
  }

  // About to commit new device memory: release empty pools first, or
  // m_memoryPools only grows (a scene reload retires all its pools and
  // allocates a fresh set).
  std::erase_if(m_memoryPools, [type](const UPtr<MemoryPool>& pool) {
    return pool->GetType() == type && pool->IsEmpty();
  });

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

  // Serial, not m_memoryPools.size(): trimming reuses indices and would
  // duplicate pool names.
  auto pool = std::make_unique<MemoryPool>(
    *this, type, std::max(size, defaultPoolSize), m_memoryPoolSerial++
#ifdef VD_API_VK
      ,
    memoryTypeBits
#endif
  );
  auto alloc = pool->Allocate(size, alignment);

  m_memoryPools.push_back(std::move(pool));
  return alloc;
}

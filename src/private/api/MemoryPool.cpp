#include "vuldir/api/MemoryPool.hpp"

#include "vuldir/api/Device.hpp"
#include "vuldir/api/PhysicalDevice.hpp"

using namespace vd;

MemoryPool::Allocation MemoryPool::Allocate(u64 size, u64 alignment)
{
  std::scoped_lock lock(m_mutex);

  if(!m_handle) {
    VDLogW(
      "[MemoryPool %s] Device memory allocation of size %llu failed: "
      "invalid memory pool",
      m_name.c_str(), size);
    return {};
  }

  if(size == 0u || alignment == 0u) {
    VDLogW(
      "[MemoryPool %s] Allocation size and alignment must be non-zero",
      m_name.c_str());
    return {};
  }

  if(size > m_freeSize) {
    VDLogW(
      "[MemoryPool %s] Device memory allocation of size %llu failed: "
      "not enough free memory in the pool (%llu)",
      m_name.c_str(), size, m_freeSize);
    return {};
  }

  if(size > m_maxAllocSize) {
    VDLogW(
      "[MemoryPool %s] Device memory allocation of size %llu failed: "
      "fragmented memory pool (max alloc size is %llu)",
      m_name.c_str(), size, m_maxAllocSize);
    return {};
  }

  // Best fit by linear scan; the free list is not kept sorted.
  auto blockIt = m_freeBlocks.end();
  for(auto it = m_freeBlocks.begin(); it != m_freeBlocks.end(); ++it) {
    const auto padding = vd::getAlignmentDiff(it->offset, alignment);
    if(padding > it->size || size > it->size - padding) continue;
    if(blockIt == m_freeBlocks.end() || it->size < blockIt->size)
      blockIt = it;
  }

  if(blockIt == m_freeBlocks.end()) {
    VDLogW(
      "[MemoryPool %s] Device memory allocation of size %llu failed: "
      "no free blocks "
      "available.",
      m_name.c_str(), size);
    return {};
  }

  auto block = *blockIt;
  deleteFreeBlock(blockIt);

  const auto padding  = vd::getAlignmentDiff(block.offset, alignment);
  const auto sizeUsed = size + padding;
  const auto sizeLeft = block.size - sizeUsed;
  if(sizeLeft > 64u) {
    block.size = sizeUsed;
    pushFreeBlock({block.offset + sizeUsed, sizeLeft});
  }

  if(m_debugVerbose) {
    VDLogV(
      "[MemoryPool %s] Alloc:%llu - used:%llu free:%llu max:%llu ",
      m_name.c_str(), size, m_usedSize, m_freeSize, m_maxAllocSize);
    logDetailedUsage();
  }

  Allocation alloc{};
  alloc.pool = this;
  alloc.offset =
    block.offset + vd::getAlignmentDiff(block.offset, alignment);
  alloc.size        = size;
  alloc.blockOffset = block.offset;
  alloc.blockSize   = block.size;

  if(m_debugVerbose) {
    VDLogV(
      "[MemoryPool %s] Allocated [Offset: %llu, Size: %llu] from block "
      "[Offset: "
      "%llu, Size: %llu]",
      m_name.c_str(), alloc.offset, alloc.size, block.offset,
      block.size);
  }

  return alloc;
}

void MemoryPool::Free(const Allocation& allocation)
{
  std::scoped_lock lock(m_mutex);

  if(allocation.pool != this)
    throw std::runtime_error(
      "Trying to free an allocation from a different memory pool");

  pushFreeBlock({allocation.blockOffset, allocation.blockSize});

  if(m_debugVerbose) {
    VDLogV(
      "[MemoryPool %s] Free:%llu - used:%llu free:%llu max:%llu ",
      m_name.c_str(), allocation.size, m_usedSize, m_freeSize,
      m_maxAllocSize);
    logDetailedUsage();
  }
}

void MemoryPool::pushFreeBlock(const Block& newBlock)
{
  auto prevBlockIt = std::find_if(
    m_freeBlocks.begin(), m_freeBlocks.end(),
    [newBlock](const Block& block) {
      return (block.offset + block.size) == newBlock.offset;
    });
  auto nextBlockIt = std::find_if(
    m_freeBlocks.begin(), m_freeBlocks.end(),
    [newBlock](const Block& block) {
      return (newBlock.offset + newBlock.size) == block.offset;
    });

  // between two adjacent blocks
  if(
    prevBlockIt != m_freeBlocks.end() &&
    nextBlockIt != m_freeBlocks.end()) {
    prevBlockIt->size += newBlock.size + nextBlockIt->size;
    m_freeBlocks.erase(nextBlockIt);
  }
  // adjacent to previous block
  else if(prevBlockIt != m_freeBlocks.end()) {
    prevBlockIt->size += newBlock.size;
  }
  // adjacent to next block
  else if(nextBlockIt != m_freeBlocks.end()) {
    nextBlockIt->offset -= newBlock.size;
    nextBlockIt->size += newBlock.size;
  }
  // no adjacency
  else {
    m_freeBlocks.push_back(newBlock);
  }

  updateMaxAllocSize();

  m_freeSize += newBlock.size;
  m_usedSize -= newBlock.size;
}

void MemoryPool::deleteFreeBlock(const Arr<Block>::iterator& blockIt)
{
  m_freeSize -= blockIt->size;
  m_usedSize += blockIt->size;

  m_freeBlocks.erase(blockIt);
  updateMaxAllocSize();
}

void MemoryPool::updateMaxAllocSize()
{
  m_maxAllocSize = 0u;
  for(const auto& block: m_freeBlocks)
    m_maxAllocSize = std::max(m_maxAllocSize, block.size);
}

void MemoryPool::logDetailedUsage()
{
  VDLogV("[MemoryPool %s] Free blocks", m_name.c_str());
  for([[maybe_unused]] auto& block: m_freeBlocks) {
    VDLogV("- Offset: %.8llu Size: %.8llu", block.offset, block.size);
  }
}

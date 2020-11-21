#include "vuldir/api/dx/DxCPUDescriptorPool.hpp"

#include "vuldir/api/Device.hpp"
#include "vuldir/api/dx/DxUti.hpp"

using namespace vd;

static i32 getAllocOffset(const std::vector<bool>& vec, u32 count)
{
  u32 start  = 0u;
  u32 length = 0u;

  for(auto idx = 0u; idx < vec.size(); ++idx) {
    if(!vec[idx]) {
      length = 0u;
      continue;
    }

    if(length == 0u) start = idx;

    length++;
    if(length >= count) return toI32(start);
  }

  return -1;
}

CPUDescriptorPool::CPUDescriptorPool(Device& device): m_device{device}
{}

CPUDescriptorPool::~CPUDescriptorPool() {}

CPUDescriptorPool::Handle
CPUDescriptorPool::Allocate(D3D12_DESCRIPTOR_HEAP_TYPE type, u32 count)
{
  std::scoped_lock lock(m_mutex);

  Heap* targetHeap = nullptr;
  i32   heapOffset = -1;

  // Try getting a heap already available.
  for(auto& heap: m_heaps) {
    if(heap.type != type) continue;
    if(heap.freeCount < count) continue;

    heapOffset = getAllocOffset(heap.free, count);
    if(heapOffset < 0) continue;

    targetHeap = &heap;
    break;
  }

  // If all full, allocate a new one.
  if(!targetHeap) {
    auto& heap = m_heaps.emplace_back();
    heap.index = toU32(m_heaps.size() - 1u);
    heap.type  = type;
    heap.size  = HeapSize;

    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.NumDescriptors = toU32(heap.size);
    desc.Type           = heap.type;
    VDDxTry(m_device.api().CreateDescriptorHeap(
      &desc, IID_PPV_ARGS(&heap.heap)));

    heap.free.resize(heap.size, true);
    heap.freeCount = heap.size;
    heap.cpu       = heap.heap->GetCPUDescriptorHandleForHeapStart();
    heap.increment =
      m_device.api().GetDescriptorHandleIncrementSize(type);

    targetHeap = &heap;
    heapOffset = 0;
  }

  // We got our heap, allocate and update heap state.
  targetHeap->freeCount -= count;
  for(auto idx = 0u; idx < count; ++idx) {
    targetHeap->free[toU32(heapOffset) + idx] = false;
  }

  // Craft the handle.
  Handle handle{};
  handle.heapIdx   = targetHeap->index;
  handle.offset    = toU32(heapOffset);
  handle.count     = count;
  handle.increment = targetHeap->increment;
  handle.cpu.ptr   = targetHeap->cpu.ptr +
                   toU64(heapOffset) * toU64(targetHeap->increment);

  return handle;
}

void CPUDescriptorPool::Free(Handle& handle)
{
  auto lock = std::scoped_lock(m_mutex);

  if(handle.heapIdx >= m_heaps.size()) return;

  auto& heap = m_heaps[handle.heapIdx];

  heap.freeCount += handle.count;
  for(u32 idx = 0u; idx < handle.count; ++idx)
    heap.free[handle.offset + idx] = true;
}

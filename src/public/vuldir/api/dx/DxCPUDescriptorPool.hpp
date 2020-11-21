#pragma once

#include "vuldir/api/Common.hpp"

namespace vd {

class Device;

class CPUDescriptorPool
{
public:
  struct Handle {
    u32 heapIdx;

    u32 offset;
    u32 count;
    u32 increment;

    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
  };

public:
  VD_NONMOVABLE(CPUDescriptorPool);

  CPUDescriptorPool(Device& device);
  ~CPUDescriptorPool();

  Handle Allocate(D3D12_DESCRIPTOR_HEAP_TYPE type, u32 count = 1u);
  void   Free(Handle& handle);

private:
  struct Heap {
    u32                          index;
    D3D12_DESCRIPTOR_HEAP_TYPE   type;
    ComPtr<ID3D12DescriptorHeap> heap;
    std::vector<bool>            free;
    u32                          freeCount;
    u32                          size;
    u32                          increment;
    D3D12_CPU_DESCRIPTOR_HANDLE  cpu;
    D3D12_GPU_DESCRIPTOR_HANDLE  gpu;
  };

private:
  static const u32 HeapSize = 1024u;

  Device&    m_device;
  std::mutex m_mutex;
  Arr<Heap>  m_heaps;
};

} // namespace vd

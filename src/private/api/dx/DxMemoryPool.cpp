#include "vuldir/api/Device.hpp"
#include "vuldir/api/MemoryPool.hpp"
#include "vuldir/api/PhysicalDevice.hpp"
#include "vuldir/api/dx/DxUti.hpp"

using namespace vd;

MemoryPool::MemoryPool(
  vd::Device& device, MemoryType type, u64 capacity, u32 idx):
  m_device{device},
  m_type{type},
  m_name{},
  m_debugVerbose{false},
  m_desc{},
  m_handle{},
  m_freeBlocks{},
  m_capacity{capacity},
  m_usedSize{capacity},
  m_freeSize{0u},
  m_maxAllocSize{0u}
{
  m_desc.SizeInBytes = m_capacity;
  m_desc.Alignment   = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  m_desc.Flags       = D3D12_HEAP_FLAG_NONE;
  m_desc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  m_desc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  m_desc.Properties.CreationNodeMask     = 1u;
  m_desc.Properties.VisibleNodeMask      = 1u;

  switch(m_type) {
    case MemoryType::Main:
      m_name                 = formatString("Main %u", idx);
      m_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
      break;
    case MemoryType::Upload:
      m_name                 = formatString("Upload %u", idx);
      m_desc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
      break;
    case MemoryType::Download:
      m_name                 = formatString("Download %u", idx);
      m_desc.Properties.Type = D3D12_HEAP_TYPE_READBACK;
      break;
    default:
      throw std::runtime_error("Unknown memory type");
  }

  VDDxTry(m_device.api().CreateHeap(&m_desc, IID_PPV_ARGS(&m_handle)));
  pushFreeBlock({0u, m_capacity});
}

MemoryPool::~MemoryPool()
{
  if(m_usedSize > 0u)
    VDLogE(
      "[MemoryPool %s] %llu bytes leaked.", m_name.c_str(), m_usedSize);

  m_handle = nullptr;

  m_freeBlocks.clear();
  m_usedSize     = 0u;
  m_freeSize     = 0u;
  m_maxAllocSize = 0u;
}

#include "vuldir/api/PhysicalDevice.hpp"
#include "vuldir/api/dx/DxUti.hpp"

using namespace vd;

PhysicalDevice::PhysicalDevice(ComPtr<IDXGIAdapter4> handle):
  m_handle{std::move(handle)}, m_desc{}
{
  if(m_handle) VDDxTry(m_handle->GetDesc3(&m_desc));
}

PhysicalDevice::~PhysicalDevice() {}

Str PhysicalDevice::GetDescription() const
{
  return narrow(m_desc.Description);
}

u64 PhysicalDevice::GetDedicatedMemorySize() const
{
  return m_desc.DedicatedVideoMemory;
}
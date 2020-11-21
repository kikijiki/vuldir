#include "vuldir/api/Device.hpp"
#include "vuldir/api/Fence.hpp"
#include "vuldir/api/dx/DxUti.hpp"

using namespace vd;

Fence::Fence(Device& device, Type type, u64 initialValue):
  m_device{device}, m_type{type}, m_target{0u}, m_handle{}, m_event{}
{
  if(type != Type::Timeline)
    throw std::runtime_error(
      "Only the timeline type is supported with DX12");

  VDDxTry(m_device.api().CreateFence(
    initialValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_handle)));

  m_event = CreateEventEx(NULL, NULL, NULL, EVENT_ALL_ACCESS);
  if(!m_event)
    throw std::runtime_error("Could not create event for D3D12 fence");
}

Fence::Fence(
  Device& device, const Str& name, Type type, u64 initialValue):
  Fence{device, type, initialValue}
{
  switch(m_type) {
    case Type::Timeline:
      m_name = formatString(
        "%s (timeline) [%#llx]", name.c_str(), (u64)m_handle.Get());
      break;
  }
}

Fence::~Fence()
{
  if(m_event) {
    CloseHandle(m_event);
    m_event = NULL;
  }
}

bool Fence::Wait(u64 timeoutNs) const
{
  return WaitValue(m_target, timeoutNs);
}

bool Fence::WaitValue(u64 value, u64 timeoutNs) const
{
  // If already completed return immediately.
  if(m_handle->GetCompletedValue() >= value) return true;

  // If not, wait on the event.
  if(FAILED(m_handle->SetEventOnCompletion(value, m_event)))
    return false;

  const u32 timeoutMs =
    timeoutNs == MaxU64 ? MaxU32 : static_cast<u32>(timeoutNs / 1000u);
  const auto result = WaitForSingleObject(m_event, timeoutMs);

  return SUCCEEDED(result);
}

bool Fence::Signal() { return Signal(m_target); }

bool Fence::Signal(u64 value)
{
  const auto result = m_handle->Signal(value);
  return SUCCEEDED(result);
}

Opt<u64> Fence::GetValue() const
{
  const auto value = m_handle->GetCompletedValue();

  if(value == UINT64_MAX) return std::nullopt;
  else
    return value;
}

bool Fence::wait(
  Span<Fence> fences, Span<u64> values, u64 timeoutNs, bool all)
{
  constexpr u64 maxFences = 16u;

  if(fences.empty()) return true;
  if(fences.size() > maxFences) {
    VDLogE("Too many fences for wait function, max is %llu", maxFences);
    return false;
  }

  HANDLE    events[maxFences];
  u32       maxValueIdx = fences.empty() ? 0u : vd::size32(fences) - 1u;
  const u32 timeoutMs =
    timeoutNs == MaxU64 ? MaxU32 : static_cast<u32>(timeoutNs / 1000u);

  for(u32 idx = 0u; idx < vd::size32(fences); ++idx) {
    u64 value = 0u;
    if(values.empty()) value = fences[idx].m_target;
    else
      value = values[std::min(idx, maxValueIdx)];

    fences[idx].m_handle->SetEventOnCompletion(
      value, fences[idx].m_event);

    events[idx] = fences[idx].m_event;
  }

  return SUCCEEDED(
    WaitForMultipleObjects(size32(fences), events, all, timeoutMs));
}

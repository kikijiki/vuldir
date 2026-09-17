#include "vuldir/api/Device.hpp"
#include "vuldir/api/Fence.hpp"
#include "vuldir/api/dx/DxUti.hpp"

#include <chrono>

using namespace vd;

#define DGBLOG(...)

namespace {

// Win32 waits take milliseconds, with INFINITE meaning "no timeout".
DWORD toTimeoutMs(u64 timeoutNs)
{
  if(timeoutNs == MaxU64) return INFINITE;
  return static_cast<DWORD>(
    std::min<u64>(
      timeoutNs / 1'000'000ull + (timeoutNs % 1'000'000ull != 0u),
      static_cast<u64>(INFINITE) - 1u));
}

// Keeps a finite timeout honest across a retry loop: a stale wake-up must
// not hand the caller a fresh full timeout each time round.
class Deadline
{
public:
  explicit Deadline(u64 timeoutNs): m_infinite{timeoutNs == MaxU64}
  {
    const auto capped = std::min<u64>(
      timeoutNs,
      static_cast<u64>(std::numeric_limits<i64>::max()));
    m_end = std::chrono::steady_clock::now() +
            std::chrono::nanoseconds{static_cast<i64>(capped)};
  }

  u64 RemainingNs() const
  {
    if(m_infinite) return MaxU64;
    const auto left = std::chrono::duration_cast<std::chrono::nanoseconds>(
      m_end - std::chrono::steady_clock::now());
    return left.count() <= 0 ? 0u : static_cast<u64>(left.count());
  }

  DWORD RemainingMs() const { return toTimeoutMs(RemainingNs()); }

private:
  bool                                  m_infinite;
  std::chrono::steady_clock::time_point m_end;
};

void resetFenceEvent(HANDLE event)
{
  if(!ResetEvent(event))
    throw makeError<std::runtime_error>(
      "DX12 fence event reset failed with Win32 error %lu",
      GetLastError());
}

} // namespace

Fence::Fence(Device& device, Type type, u64 initialValue):
  m_device{device},
  m_type{type},
  m_target{initialValue},
  m_targetMutex{},
  m_handle{},
  m_event{}
{
  if(type != Type::Timeline)
    throw std::runtime_error(
      "Only the timeline type is supported with DX12");

  VDDxTry(m_device.api().CreateFence(
    initialValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_handle)));

  // Manual reset. An auto-reset event is consumed by whichever waiter
  // reaches it first, and a wait that ends without its own registration
  // firing (wait-any, or a timeout) leaves that registration live to signal
  // later. Both are handled by resetting under m_eventMutex before every
  // registration and letting GetCompletedValue, not the wake-up, decide.
  m_event = CreateEventEx(
    nullptr, nullptr, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
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
  return WaitValue(m_target.load(), timeoutNs);
}

bool Fence::WaitValue(u64 value, u64 timeoutNs) const
{
  DGBLOG("Fence %s waiting for value %llu", m_name.c_str(), value);

  if(m_handle->GetCompletedValue() >= value) {
    DGBLOG("Fence %s already completed", m_name.c_str());
    return true;
  }

  // The fence has a single event, so overlapping waits on it would clobber
  // each other's registration and steal each other's signal.
  std::scoped_lock eventLock(m_eventMutex);
  const Deadline   deadline{timeoutNs};

  if(m_handle->GetCompletedValue() >= value) return true;
  resetFenceEvent(m_event);
  if(FAILED(m_handle->SetEventOnCompletion(value, m_event)))
    return false;

  for(;;) {
    const auto result =
      WaitForSingleObject(m_event, deadline.RemainingMs());

    DGBLOG(
      "Fence %s waiting for value %llu completed with result %u",
      m_name.c_str(), value, result);

    if(result == WAIT_FAILED)
      throw makeError<std::runtime_error>(
        "DX12 fence wait failed with Win32 error %lu", GetLastError());
    if(result == WAIT_TIMEOUT)
      return m_handle->GetCompletedValue() >= value;
    if(result != WAIT_OBJECT_0) return false;
    if(m_handle->GetCompletedValue() >= value) return true;

    // Woken by a registration an earlier wait left live on this event. Ours
    // is still pending, so clear the stale signal and wait again, checking
    // once more in case the fence advanced across the reset.
    resetFenceEvent(m_event);
    if(m_handle->GetCompletedValue() >= value) return true;
  }
}

bool Fence::Signal() { return Signal(m_target.load()); }

bool Fence::Signal(u64 value)
{
  std::scoped_lock targetLock(m_targetMutex);
  if(value < m_target.load())
    throw std::invalid_argument(
      "Timeline fence target cannot move backwards");
  DGBLOG("Fence %s signaling value %llu", m_name.c_str(), value);
  const auto result = m_handle->Signal(value);
  if(FAILED(result)) return false;
  m_target.store(value);
  return true;
}

u64 Fence::GetValue() const { return m_handle->GetCompletedValue(); }

bool Fence::wait(
  Span<Fence> fences, Span<u64> values, u64 timeoutNs, bool all)
{
  constexpr u64 maxFences = 16u;

  if(fences.empty()) return true;
  if(fences.size() > maxFences) {
    VDLogE("Too many fences for wait function, max is %llu", maxFences);
    return false;
  }

  if(!values.empty() && values.size() != 1u && values.size() != fences.size())
    throw std::invalid_argument("Fence values must be singular or match the fence count");

  SArr<u64, maxFences> targets{};
  for(u32 idx = 0u; idx < vd::size32(fences); ++idx) {
    if(values.empty()) targets[idx] = fences[idx].m_target.load();
    else
      targets[idx] = values[values.size() == 1u ? 0u : idx];
  }

  const Deadline deadline{timeoutNs};

  if(all) {
    // Same meaning as WaitForMultipleObjects(bWaitAll), but it keeps the
    // event handling in one place instead of duplicating it here.
    for(u32 idx = 0u; idx < vd::size32(fences); ++idx)
      if(!fences[idx].WaitValue(targets[idx], deadline.RemainingNs()))
        return false;
    return true;
  }

  // Wait-any genuinely needs one wait across every event. Each fence owns
  // exactly one, and WaitForMultipleObjects rejects duplicate handles, so
  // make the duplicate an explicit error rather than an opaque Win32 one.
  for(u32 idx = 0u; idx < vd::size32(fences); ++idx)
    for(u32 other = idx + 1u; other < vd::size32(fences); ++other)
      if(&fences[idx] == &fences[other])
        throw std::invalid_argument(
          "A fence cannot appear twice in one wait");

  // Hold every event lock for the duration, ordered by address so two
  // overlapping batch waits that share fences cannot deadlock.
  Arr<Fence*> ordered;
  ordered.reserve(fences.size());
  for(auto& fence: fences) ordered.push_back(&fence);
  std::ranges::sort(ordered);

  Arr<std::unique_lock<std::mutex>> locks;
  locks.reserve(ordered.size());
  for(Fence* fence: ordered) locks.emplace_back(fence->m_eventMutex);

  const auto anyCompleted = [&] {
    for(u32 idx = 0u; idx < vd::size32(fences); ++idx)
      if(fences[idx].m_handle->GetCompletedValue() >= targets[idx])
        return true;
    return false;
  };

  HANDLE events[maxFences];
  for(;;) {
    if(anyCompleted()) return true;

    for(u32 idx = 0u; idx < vd::size32(fences); ++idx) {
      resetFenceEvent(fences[idx].m_event);
      events[idx] = fences[idx].m_event;
    }
    // A fence may have advanced across the resets above.
    if(anyCompleted()) return true;

    for(u32 idx = 0u; idx < vd::size32(fences); ++idx)
      if(FAILED(fences[idx].m_handle->SetEventOnCompletion(
           targets[idx], fences[idx].m_event)))
        return false;

    const auto result = WaitForMultipleObjects(
      vd::size32(fences), events, FALSE, deadline.RemainingMs());

    if(result == WAIT_FAILED)
      throw makeError<std::runtime_error>(
        "DX12 fence batch wait failed with Win32 error %lu",
        GetLastError());
    if(result == WAIT_TIMEOUT) return anyCompleted();
    if(
      result < WAIT_OBJECT_0 ||
      result >= WAIT_OBJECT_0 + vd::size32(fences))
      return false;
    if(anyCompleted()) return true;

    // Signalled only by a registration an earlier wait left live. Go round
    // again; the deadline keeps a finite timeout from restarting.
  }
}

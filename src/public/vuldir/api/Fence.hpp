#pragma once

#include "vuldir/api/Common.hpp"

namespace vd {

class Device;

class Fence
{
public:
  enum class Type {
    Timeline,
#ifdef VD_API_VK
    Binary,
    Fence
#endif
  };

public:
  VD_NONMOVABLE(Fence);

  Fence(
    Device& device, Type type = Type::Timeline, u64 initialValue = 0u);
  Fence(
    Device& device, const Str& name, Type type = Type::Timeline,
    u64 initialValue = 0u);
  ~Fence();

  Type GetType() const { return m_type; }

#ifdef VD_API_VK
  VkSemaphore GetSemaphoreHandle()
  {
    VDAssert(m_type != Type::Fence);
    return m_semaphoreHandle;
  }
  VkFence GetFenceHandle()
  {
    VDAssert(m_type == Type::Fence);
    return m_fenceHandle;
  }

  void Reset();

#elif VD_API_DX
  ID3D12Fence& GetHandle() { return *m_handle.Get(); }
#endif

public:
  bool Wait(u64 timeoutNs = MaxU64) const;
  bool WaitValue(u64 value, u64 timeoutNs = MaxU64) const;
  bool Signal();
  bool Signal(u64 value);

  const Str& GetName() const { return m_name; }

  Opt<u64> GetValue() const;
  u64      GetTarget() const { return m_target; }
  u64      Step(const u64 step = 1u)
  {
    if(m_type != Type::Timeline)
      throw std::runtime_error{"Fence is not a timeline fence"};

    m_target += step;
    return m_target;
  }

public:
  static bool WaitAny(Span<Fence> fences, u64 timeoutNs = MaxU64)
  {
    return wait(fences, {}, timeoutNs, false);
  }

  static bool
  WaitAny(Span<Fence> fences, u64 value, u64 timeoutNs = MaxU64)
  {
    return wait(fences, {&value, 1u}, timeoutNs, false);
  }

  static bool
  WaitAny(Span<Fence> fences, Span<u64> values, u64 timeoutNs = MaxU64)
  {
    return wait(fences, values, timeoutNs, false);
  }

  static bool WaitAll(Span<Fence> fences, u64 timeoutNs = MaxU64)
  {
    return wait(fences, {}, timeoutNs, true);
  }

  static bool
  WaitAll(Span<Fence> fences, u64 value, u64 timeoutNs = MaxU64)
  {
    return wait(fences, {&value, 1u}, timeoutNs, true);
  }

  static bool
  WaitAll(Span<Fence> fences, Span<u64> values, u64 timeoutNs = MaxU64)
  {
    return wait(fences, values, timeoutNs, true);
  }

  static bool SignalAll(Span<Fence> fences, u64 value)
  {
    bool result = true;
    for(u32 idx = 0u; idx < fences.size(); ++idx)
      result &= fences[idx].Signal(value);
    return result;
  }

  static bool SignalAll(Span<Fence> fences, Span<u64> values)
  {
    bool result = true;
    for(u32 idx = 0u; idx < fences.size(); ++idx)
      result &= fences[idx].Signal(values[idx]);
    return result;
  }

private:
  static bool
  wait(Span<Fence> fences, Span<u64> values, u64 timeoutNs, bool all);

private:
  Device& m_device;
  Type    m_type;
  u64     m_target;
  Str     m_name;

#ifdef VD_API_VK
  VkSemaphore m_semaphoreHandle;
  VkFence     m_fenceHandle;
#elif VD_API_DX
  ComPtr<ID3D12Fence> m_handle;
  HANDLE              m_event;
#endif
};

} // namespace vd

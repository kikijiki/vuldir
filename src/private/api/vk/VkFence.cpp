#include "vuldir/api/Device.hpp"
#include "vuldir/api/Fence.hpp"
#include "vuldir/api/vk/VkDispatcher.hpp"
#include "vuldir/api/vk/VkUti.hpp"

using namespace vd;

Fence::Fence(Device& device, Type type, u64 initialValue):
  m_device{device},
  m_type{type},
  m_target{0u},
  m_name{},
  m_semaphoreHandle{},
  m_fenceHandle{}
{
  if(m_type == Type::Fence) {
    VkFenceCreateInfo ci{
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0};
    m_device.api().CreateFence(&ci, &m_fenceHandle);
  } else {
    VkSemaphoreTypeCreateInfo typeInfo{
      .sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .pNext         = nullptr,
      .semaphoreType = m_type == Type::Binary
                         ? VK_SEMAPHORE_TYPE_BINARY
                         : VK_SEMAPHORE_TYPE_TIMELINE,
      .initialValue  = initialValue};

    VkSemaphoreCreateInfo ci{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = &typeInfo,
      .flags = 0u};

    VDVkTry(m_device.api().CreateSemaphore(&ci, &m_semaphoreHandle));
  }
}

Fence::Fence(
  Device& device, const Str& name, Type type, u64 initialValue):
  Fence(device, type, initialValue)
{
  switch(m_type) {
    case Type::Timeline:
      m_name = formatString(
        "%s (timeline) [%#llx]", name.c_str(), (u64)m_semaphoreHandle);
      break;
    case Type::Binary:
      m_name = formatString(
        "%s (binary) [%#llx]", name.c_str(), (u64)m_semaphoreHandle);
      break;
    case Type::Fence:
      m_name = formatString(
        "%s (fence) [%#llx]", name.c_str(), (u64)m_fenceHandle);
      break;
  }

  VDLogV("Creating fence %s", m_name.c_str());
}

Fence::~Fence()
{
  if(!m_name.empty()) { VDLogV("Destroying fence %s", m_name.c_str()); }

  if(m_type == Type::Fence) {
    m_device.api().DestroyFence(m_fenceHandle);
  } else {
    m_device.api().DestroySemaphore(m_semaphoreHandle);
  }

  m_fenceHandle     = nullptr;
  m_semaphoreHandle = nullptr;
}

void Fence::Reset()
{
  VDAssertMsg(
    m_type == Type::Fence, "Invalid for non-fence semaphores");

  //if(!m_name.empty()) { VDLogV("Resetting fence %s", m_name.c_str()); }

  m_device.api().ResetFences(1u, &m_fenceHandle);
}

bool Fence::Wait(u64 timeoutNs) const
{
  return WaitValue(m_target, timeoutNs);
}

bool Fence::WaitValue(u64 value, u64 timeoutNs) const
{
  if(m_type == Type::Binary)
    throw std::runtime_error("Invalid for binary semaphores");

  if(m_type == Type::Timeline) {
    VkSemaphoreWaitInfo info;
    info.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    info.pNext          = nullptr;
    info.flags          = 0u;
    info.semaphoreCount = 1u;
    info.pSemaphores    = &m_semaphoreHandle;
    info.pValues        = &value;

    //if(!m_name.empty()) {
    //  VDLogV("Waiting fence %s for value %llu", m_name.c_str(), value);
    //}

    const auto result = m_device.api().WaitSemaphores(&info, timeoutNs);
    return result == VK_SUCCESS;
  } else {
    //if(!m_name.empty()) { VDLogV("Waiting fence %s", m_name.c_str()); }

    const auto result =
      m_device.api().WaitForFences(1u, &m_fenceHandle, true, timeoutNs);
    return result == VK_SUCCESS;
  }
}

bool Fence::Signal() { return Signal(m_target); }

bool Fence::Signal(u64 value)
{
  if(m_type != Type::Timeline)
    throw std::runtime_error("Invalid for non-timeline fences");

  //if(!m_name.empty()) {
  //  VDLogV("Signaling fence %s for value %llu", m_name.c_str(), value);
  //}

  VkSemaphoreSignalInfo info;
  info.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
  info.pNext     = nullptr;
  info.semaphore = m_semaphoreHandle;
  info.value     = value;

  const auto result = m_device.api().SignalSemaphore(&info);
  return result == VK_SUCCESS;
}

u64 Fence::GetValue() const
{
  if(m_type != Type::Timeline)
    throw std::runtime_error("Invalid for non-timeline fences");

  u64 value = 0;

  const auto result =
    m_device.api().GetSemaphoreCounterValue(m_semaphoreHandle, &value);

  if(result == VK_SUCCESS) return value;
  else
    return MaxU64;
}

bool Fence::wait(
  Span<Fence> fences, Span<u64> values, u64 timeoutNs, bool all)
{
  constexpr u64 maxFences = 16u;

  for(const auto& fence: fences)
    if(fence.GetType() != Type::Timeline)
      throw std::runtime_error("Invalid for non-timeline fences");

  if(fences.empty()) return true;
  if(fences.size() > maxFences) {
    VDLogE("Too many fences for wait function, max is %llu", maxFences);
    return false;
  }

  auto& device = fences[0].m_device;

  VkSemaphoreWaitInfoKHR info{};
  info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR;
  info.pNext = nullptr;
  info.flags = all ? 0u : VK_SEMAPHORE_WAIT_ANY_BIT_KHR;

  u64 targetValues[maxFences];
  if(values.empty()) {
    for(u32 idx = 0u; idx < vd::size32(fences); ++idx)
      targetValues[idx] = fences[idx].GetTarget();

    info.pValues = targetValues;
  } else {
    info.pValues = std::data(values);
  }

  VkSemaphore handles[maxFences];

  //VDLogV("Waiting for %llu fences", fences.size());
  for(u32 idx = 0u; idx < vd::size32(fences); ++idx) {
    //if(!fences[idx].m_name.empty()) {
    //  VDLogV(
    //    "- %s for value %llu", fences[idx].m_name.c_str(), values[idx]);
    //}
    handles[idx] = fences[idx].GetSemaphoreHandle();
  }

  info.semaphoreCount = vd::size32(fences);
  info.pSemaphores    = handles;

  const auto result = device.api().WaitSemaphores(&info, timeoutNs);
  return result == VK_SUCCESS;
}

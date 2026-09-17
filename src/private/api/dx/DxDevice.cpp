#include "vuldir/api/Binder.hpp"
#include "vuldir/api/CommandBuffer.hpp"
#include "vuldir/api/Device.hpp"
#include "vuldir/api/Fence.hpp"
#include "vuldir/api/PhysicalDevice.hpp"
#include "vuldir/api/dx/DxCPUDescriptorPool.hpp"
#include "vuldir/api/dx/DxUti.hpp"

#include <dxgidebug.h>

using namespace vd;

Device::Device(const Desc& desc, const Swapchain::Desc& swapchainDesc):
  m_desc{desc},
  m_window{swapchainDesc.window},
  m_physicalDevice{},
  m_swapchain{},
  m_binder{},
  m_queues{},
  m_memoryPools{},
  m_memoryMutex{},
  m_handle{},
  m_descriptorPool{}
{
  create_api(desc);
  create_physicalDevice(desc);
  create_device(desc);

  m_swapchain = std::make_unique<Swapchain>(*this, swapchainDesc);
}

Device::~Device()
{
  // Owned swapchain, descriptor, and memory objects may still be referenced
  // by submitted command lists. Retire that work before releasing them.
  try {
    WaitIdle();
  } catch(const std::exception& error) {
    VDLogE("Device teardown could not wait for idle: %s", error.what());
  }

  m_binder         = nullptr;
  m_swapchain      = nullptr;
  m_descriptorPool = nullptr;

  m_memoryPools.clear();

  m_handle         = nullptr;
  m_physicalDevice = nullptr;

  if(m_desc.dbgEnable) ReportLiveObjects();
}

void Device::Submit(
  Arr<CommandBuffer*> cmds, Arr<Fence*> waits, Arr<Fence*> signals,
  Fence* submitFence, SwapchainDep swapchainDep)
{
  if(cmds.size() == 0u) return;
  if(!cmds[0])
    throw std::invalid_argument("Cannot submit a null command buffer");
  const auto  queueType = cmds[0]->GetQueueType();
  const auto& queue     = m_queues[enumValue(queueType)];
  std::scoped_lock queueLock(*queue.mutex);
  std::scoped_lock stateLock(m_resourceStateMutex);

  Arr<ID3D12CommandList*> dxCmds;
  for(auto* cmd: cmds) {
    if(!cmd)
      throw std::invalid_argument("Cannot submit a null command buffer");
    if(cmd->GetQueueType() != queueType)
      throw std::runtime_error("Command buffer queue mismatch");
    if(&cmd->m_device != this)
      throw std::invalid_argument(
        "Command buffer belongs to a different device");
    if(cmd->GetState() != CommandBuffer::State::Closed)
      throw std::runtime_error(
        "Only closed command buffers can be submitted");
    dxCmds.push_back(&cmd->GetHandle());
  }
  validateResourceStates(cmds);

  for(const auto* wait: waits) {
    if(!wait)
      throw std::invalid_argument("Cannot wait on a null fence");
    if(&wait->m_device != this)
      throw std::invalid_argument("Wait fence belongs to a different device");
  }
  for(const auto* signal: signals) {
    if(!signal)
      throw std::invalid_argument("Cannot signal a null fence");
    if(&signal->m_device != this)
      throw std::invalid_argument(
        "Signal fence belongs to a different device");
  }
  if(submitFence && &submitFence->m_device != this)
    throw std::invalid_argument(
      "Submit fence belongs to a different device");

  for(auto& wait: waits) {
    if(!Wait(queueType, *wait))
      throw std::runtime_error("Failed to queue a fence wait");
  }
  if(
    swapchainDep == SwapchainDep::Acquire ||
    swapchainDep == SwapchainDep::AcquireRelease) {
    if(!Wait(queueType, m_swapchain->GetAcquireFence()))
      throw std::runtime_error(
        "Failed to queue the swapchain acquire wait");
  }

  queue.handle->ExecuteCommandLists(size32(dxCmds), std::data(dxCmds));
  for(auto* cmd: cmds) cmd->commitResourceStates();

  for(auto& signal: signals) {
    if(!Signal(queueType, *signal))
      throw std::runtime_error("Failed to signal a fence");
  }

  if(
    swapchainDep == SwapchainDep::Release ||
    swapchainDep == SwapchainDep::AcquireRelease) {
    if(!Signal(queueType, m_swapchain->GetReleaseFence()))
      throw std::runtime_error(
        "Failed to signal the swapchain release fence");
  }

  if(submitFence) {
    if(!Signal(queueType, *submitFence))
      throw std::runtime_error("Failed to signal the submit fence");
  }
}

bool Device::Wait(QueueType queue, Fence& fence) const
{
  if(!isValid(queue))
    throw std::invalid_argument("Queue type is invalid");
  if(&fence.m_device != this)
    throw std::invalid_argument(
      "Wait fence belongs to a different device");
  std::scoped_lock queueLock(*m_queues[enumValue(queue)].mutex);
  return SUCCEEDED(
    GetQueueHandle(queue).Wait(&fence.GetHandle(), fence.GetTarget()));
}

bool Device::Wait(QueueType queue, Fence& fence, u64 value) const
{
  if(!isValid(queue))
    throw std::invalid_argument("Queue type is invalid");
  if(&fence.m_device != this)
    throw std::invalid_argument(
      "Wait fence belongs to a different device");
  std::scoped_lock queueLock(*m_queues[enumValue(queue)].mutex);
  return SUCCEEDED(
    GetQueueHandle(queue).Wait(&fence.GetHandle(), value));
}

bool Device::Signal(QueueType queue, Fence& fence)
{
  if(!isValid(queue))
    throw std::invalid_argument("Queue type is invalid");
  if(&fence.m_device != this)
    throw std::invalid_argument(
      "Signal fence belongs to a different device");
  std::scoped_lock queueLock(*m_queues[enumValue(queue)].mutex);
  std::scoped_lock targetLock(fence.m_targetMutex);
  const u64 target = fence.m_target.load();
  if(target == MaxU64)
    throw std::overflow_error("Timeline fence target overflow");
  const u64 nextTarget = target + 1u;
  if(FAILED(GetQueueHandle(queue).Signal(&fence.GetHandle(), nextTarget)))
    return false;
  fence.m_target.store(nextTarget);
  return true;
}

bool Device::Signal(QueueType queue, Fence& fence, u64 value)
{
  if(!isValid(queue))
    throw std::invalid_argument("Queue type is invalid");
  if(&fence.m_device != this)
    throw std::invalid_argument(
      "Signal fence belongs to a different device");
  std::scoped_lock queueLock(*m_queues[enumValue(queue)].mutex);
  std::scoped_lock targetLock(fence.m_targetMutex);
  if(value < fence.m_target.load())
    throw std::invalid_argument(
      "Timeline fence target cannot move backwards");
  if(FAILED(GetQueueHandle(queue).Signal(&fence.GetHandle(), value)))
    return false;
  fence.m_target.store(value);
  return true;
}

void Device::WaitIdle(QueueType queue)
{
  if(!isValid(queue))
    throw std::invalid_argument("Queue type is invalid");
  std::scoped_lock queueLock(*m_queues[enumValue(queue)].mutex);
  Fence fence{*this, "WaitIdle", Fence::Type::Timeline};
  if(!Signal(queue, fence))
    throw std::runtime_error("Failed to signal queue idle fence");
  if(!fence.Wait())
    throw std::runtime_error("Failed to wait for queue idle fence");
}

void Device::WaitIdle()
{
  for(auto queue: QueueTypes) WaitIdle(queue);
}

void Device::ReportLiveObjects() const
{
  ComPtr<IDXGIDebug1> dxgiDebug;
  if(SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug)))) {
    dxgiDebug->ReportLiveObjects(
      DXGI_DEBUG_ALL,
      DXGI_DEBUG_RLO_FLAGS(
        DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
  }
}

void Device::create_api(const Desc& desc) { VD_UNUSED(desc); }

void Device::create_physicalDevice(const Desc& desc)
{
  ComPtr<IDXGIFactory7> dxgiFactory;
  ComPtr<ID3D12Debug>   debugInterface;
  ComPtr<IDXGIAdapter4> adapter;

  UINT factoryFlags = 0u;

  if(desc.dbgEnable) {
    factoryFlags = DXGI_CREATE_FACTORY_DEBUG;

    VDDxTry(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
    debugInterface->EnableDebugLayer();

    ComPtr<ID3D12Debug>  spDebugController0;
    ComPtr<ID3D12Debug1> spDebugController1;
    VDDxTry(D3D12GetDebugInterface(IID_PPV_ARGS(&spDebugController0)));
    VDDxTry(spDebugController0->QueryInterface(
      IID_PPV_ARGS(&spDebugController1)));
    spDebugController1->SetEnableGPUBasedValidation(true);

    if(desc.dbgUseDRED) {
      ComPtr<ID3D12DeviceRemovedExtendedDataSettings> pDredSettings;
      VDDxTry(D3D12GetDebugInterface(IID_PPV_ARGS(&pDredSettings)));
      pDredSettings->SetAutoBreadcrumbsEnablement(
        D3D12_DRED_ENABLEMENT_FORCED_ON);
      pDredSettings->SetPageFaultEnablement(
        D3D12_DRED_ENABLEMENT_FORCED_ON);
    }
  }

  VDDxTry(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&dxgiFactory)));

  if(desc.dbgUseSoftwareRenderer) {
    ComPtr<IDXGIAdapter1> dxgiAdapter1;
    VDDxTry(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&dxgiAdapter1)));
    VDDxTry(dxgiAdapter1.As(&adapter));
  } else {
    ComPtr<IDXGIAdapter1> dxgiAdapter1;
    SIZE_T                maxMemory = 0u;
    for(UINT idx = 0;
        dxgiFactory->EnumAdapterByGpuPreference(
          idx, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
          IID_PPV_ARGS(&dxgiAdapter1)) != DXGI_ERROR_NOT_FOUND;
        ++idx) {
      DXGI_ADAPTER_DESC1 dxgiAdapterDesc1{};

      dxgiAdapter1->GetDesc1(&dxgiAdapterDesc1);
      if((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
        continue;

      const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
      };

      bool supportsD3D12 = false;
      for(const auto featureLevel: featureLevels) {
        if(SUCCEEDED(D3D12CreateDevice(
             dxgiAdapter1.Get(), featureLevel, IID_ID3D12Device,
             nullptr))) {
          supportsD3D12 = true;
          break;
        }
      }

      if(supportsD3D12) {
        if(dxgiAdapterDesc1.DedicatedVideoMemory > maxMemory) {
          maxMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
          VDDxTry(dxgiAdapter1.As(&adapter));
        }
      }
    }
  }

  if(!adapter)
    throw std::runtime_error("No compatible physical device found.");

  m_physicalDevice = std::make_unique<PhysicalDevice>(adapter);
}

void Device::create_device(const Desc& desc)
{
  VD_UNUSED(desc);

  VDLogI(
    "Creating device: %s", m_physicalDevice->GetDescription().c_str());

  UUID experimentalFeatures[] = {D3D12ExperimentalShaderModels};
  D3D12EnableExperimentalFeatures(
    size32(experimentalFeatures), std::data(experimentalFeatures),
    nullptr, nullptr);

  const D3D_FEATURE_LEVEL featureLevels[] = {
    D3D_FEATURE_LEVEL_12_1,
    D3D_FEATURE_LEVEL_12_0,
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
  };

  HRESULT createResult = E_FAIL;
  for(const auto featureLevel: featureLevels) {
    createResult = D3D12CreateDevice(
      m_physicalDevice->GetHandle(), featureLevel, IID_PPV_ARGS(&m_handle));
    if(SUCCEEDED(createResult)) break;
  }
  VDDxTry(createResult);

  m_handle->SetName(widen("Vuldir DX Device").c_str());

  if(desc.dbgEnable) {
    ComPtr<ID3D12InfoQueue> pInfoQueue;
    if(SUCCEEDED(m_handle.As(&pInfoQueue))) {
      if(desc.dbgBreakOnError) {
        pInfoQueue->SetBreakOnSeverity(
          D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        pInfoQueue->SetBreakOnSeverity(
          D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
      }

      if(desc.dbgBreakOnWarning) {
        pInfoQueue->SetBreakOnSeverity(
          D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
      }

      D3D12_MESSAGE_SEVERITY Severities[] = {
        D3D12_MESSAGE_SEVERITY_INFO};

      D3D12_MESSAGE_ID DenyIds[] = {
        D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
        D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
        D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
      };

      D3D12_INFO_QUEUE_FILTER NewFilter = {};
      NewFilter.DenyList.NumSeverities = _countof(Severities);
      NewFilter.DenyList.pSeverityList = Severities;
      NewFilter.DenyList.NumIDs        = _countof(DenyIds);
      NewFilter.DenyList.pIDList       = DenyIds;

      VDDxTry(pInfoQueue->PushStorageFilter(&NewFilter));
    }
  }

  { // Queues
    const std::tuple<QueueType, D3D12_COMMAND_LIST_TYPE> queueCI[] = {
      {QueueType::Graphics, D3D12_COMMAND_LIST_TYPE_DIRECT},
      {QueueType::Compute, D3D12_COMMAND_LIST_TYPE_COMPUTE},
      {QueueType::Copy, D3D12_COMMAND_LIST_TYPE_COPY},
    };

    for(const auto& ci: queueCI) {
      D3D12_COMMAND_QUEUE_DESC queueDesc{
        .Type     = std::get<1>(ci),
        .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
        .Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE,
        .NodeMask = 0};

      auto& queue = m_queues[enumValue(std::get<0>(ci))];
      queue.mutex = std::make_shared<std::recursive_mutex>();
      VDDxTry(m_handle->CreateCommandQueue(
        &queueDesc, IID_PPV_ARGS(&queue.handle)));
    }
  }

  m_descriptorPool = std::make_unique<CPUDescriptorPool>(*this);
  m_binder         = std::make_unique<Binder>(*this, Binder::Desc{});
}

void Device::onDeviceRemoved()
{
}

#include "vuldir/api/Binder.hpp"
#include "vuldir/api/CommandBuffer.hpp"
#include "vuldir/api/Device.hpp"
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
  const auto  queueType = cmds[0]->GetQueueType();
  const auto& queue     = m_queues[enumValue(queueType)];

  Arr<ID3D12CommandList*> dxCmds;
  for(u32 idx = 0u; idx < std::size(cmds); ++idx) {
    if(cmds[idx]) { dxCmds.push_back(&cmds[idx]->GetHandle()); }
  }

  for(auto& wait: waits) {
    if(wait) {
      Wait(queueType, *wait);
    } else
      break;
  }
  if(
    swapchainDep == SwapchainDep::Acquire ||
    swapchainDep == SwapchainDep::AcquireRelease) {
    Wait(queueType, m_swapchain->GetAcquireFence());
  }

  queue.handle->ExecuteCommandLists(size32(dxCmds), std::data(dxCmds));

  for(auto& signal: signals) {
    if(signal) {
      Signal(queueType, *signal);
    } else
      break;
  }

  if(
    swapchainDep == SwapchainDep::Release ||
    swapchainDep == SwapchainDep::AcquireRelease) {
    Signal(queueType, m_swapchain->GetReleaseFence());
  }

  if(submitFence) { Signal(queueType, *submitFence); }
}

bool Device::Wait(QueueType queue, Fence& fence) const
{
  return SUCCEEDED(
    GetQueueHandle(queue).Wait(&fence.GetHandle(), fence.GetTarget()));
}

bool Device::Wait(QueueType queue, Fence& fence, u64 value) const
{
  return SUCCEEDED(
    GetQueueHandle(queue).Wait(&fence.GetHandle(), value));
}

bool Device::Signal(QueueType queue, Fence& fence)
{
  return SUCCEEDED(GetQueueHandle(queue).Signal(
    &fence.GetHandle(), fence.GetTarget()));
}

bool Device::Signal(QueueType queue, Fence& fence, u64 value)
{
  return SUCCEEDED(
    GetQueueHandle(queue).Signal(&fence.GetHandle(), value));
}

void Device::WaitIdle(QueueType queue)
{
  // TODO
  VD_UNUSED(queue);
}

void Device::WaitIdle()
{
  // TODO
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

      if(SUCCEEDED(D3D12CreateDevice(
           dxgiAdapter1.Get(), D3D_FEATURE_LEVEL_12_1,
           __uuidof(ID3D12Device), nullptr))) {
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

  VDDxTry(D3D12CreateDevice(
    m_physicalDevice->GetHandle(), D3D_FEATURE_LEVEL_12_1,
    IID_PPV_ARGS(&m_handle)));

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

      // D3D12_MESSAGE_CATEGORY Categories[] = {};

      D3D12_MESSAGE_SEVERITY Severities[] = {
        D3D12_MESSAGE_SEVERITY_INFO};

      D3D12_MESSAGE_ID DenyIds[] = {
        D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
        D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
        D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
      };

      D3D12_INFO_QUEUE_FILTER NewFilter = {};
      // NewFilter.DenyList.NumCategories = _countof(Categories);
      // NewFilter.DenyList.pCategoryList = Categories;
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
      // TODO: Need this?
      //queue.fence =
      //  std::make_unique<Fence>(*this, Fence::Type::Timeline);
      VDDxTry(m_handle->CreateCommandQueue(
        &queueDesc, IID_PPV_ARGS(&queue.handle)));
    }
  }

  m_descriptorPool = std::make_unique<CPUDescriptorPool>(*this);
  m_binder         = std::make_unique<Binder>(*this, Binder::Desc{});
}

void Device::onDeviceRemoved()
{
  /*
  static constexpr const char* D3D12_OpNames[] = {
    "SetMarker",
    "BeginEvent",
    "EndEvent",
    "DrawInstanced",
    "DrawIndexedInstanced",
    "ExecuteIndirect",
    "Dispatch",
    "CopyBufferRegion",
    "CopyTextureRegion",
    "CopyResource",
    "CopyTiles",
    "ResolveSubresource",
    "ClearRenderTargetView",
    "ClearUnorderedAccessView",
    "ClearDepthStencilView",
    "ResourceBarrier",
    "ExecuteBundle",
    "Present",
    "ResolveQueryData",
    "BeginSubmission",
    "EndSubmission",
    "DecodeFrame",
    "ProcessFrames",
    "AtomicCopyBufferUint",
    "AtomicCopyBufferUint64",
    "ResolveSubresourceRegion",
    "WriteBufferImmediate",
    "DecodeFrame1",
    "SetProtectedResourceSession",
    "DecodeFrame2",
    "ProcessFrames1",
    "BuildRaytracingAccelerationStructure",
    "EmitRaytracingAccelerationStructurePostBuildInfo",
    "CopyRaytracingAccelerationStructure",
    "DispatchRays",
    "InitializeMetaCommand",
    "ExecuteMetaCommand",
    "EstimateMotion",
    "ResolveMotionVectorHeap",
    "SetPipelineState1",
    "InitializeExtensionCommand",
    "ExecuteExtensionCommand",
  };

  ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
  m_handle->QueryInterface(IID_PPV_ARGS(&dred));

  D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs{};
  dred->GetAutoBreadcrumbsOutput1(&breadcrumbs);

  VDLogW("Gathered auto-breadcrumbs output.");
  if(breadcrumbs.pHeadAutoBreadcrumbNode) {
    VDLogW("DRED: Last tracked GPU operations:");

    Str                          ContextStr;
    Map<int32_t, const wchar_t*> ContextStrings;

    u32  TracedCommandLists = 0;
    auto Node               = breadcrumbs.pHeadAutoBreadcrumbNode;

    while(Node && Node->pLastBreadcrumbValue) {
      int32_t LastCompletedOp = *Node->pLastBreadcrumbValue;

      if(
        LastCompletedOp != (i32)Node->BreadcrumbCount &&
        LastCompletedOp != 0) {
        VDLogW(
          "DRED: Commandlist \"%ls\" on CommandQueue \"%ls\", %d "
          "completed of %d",
          Node->pCommandListDebugNameW, Node->pCommandQueueDebugNameW,
          LastCompletedOp, Node->BreadcrumbCount);
        TracedCommandLists++;

        i32 FirstOp = std::max(LastCompletedOp - 100, 0);
        i32 LastOp  = std::min(
           LastCompletedOp + 20, i32(Node->BreadcrumbCount) - 1);

        ContextStrings.clear();
        for(u32 idx = 0; idx < Node->BreadcrumbContextsCount; ++idx) {
          const auto& Context = Node->pBreadcrumbContexts[idx];
          ContextStrings.emplace(
            Context.BreadcrumbIndex, Context.pContextString);
        }

        for(i32 Op = FirstOp; Op <= LastOp; ++Op) {
          D3D12_AUTO_BREADCRUMB_OP BreadcrumbOp =
            Node->pCommandHistory[Op];

          auto OpContextStr = ContextStrings.find(Op);
          if(OpContextStr != std::end(ContextStrings)) {
            ContextStr = " [";
            ContextStr += vd::narrow(OpContextStr->second);
            ContextStr += "]";
          } else {
            ContextStr.clear();
          }

          const auto* OpName =
            ((u32)BreadcrumbOp < std::size(D3D12_OpNames))
              ? D3D12_OpNames[BreadcrumbOp]
              : "Unknown Op";

          VDLogW(
            "\tOp: %d, %s%s%s", Op, OpName, ContextStr.c_str(),
            (Op + 1 == LastCompletedOp) ? " - LAST COMPLETED" : "");
        }
      }

      Node = Node->pNext;
    }

    if(TracedCommandLists == 0) {
      VDLogW(
        "DRED: No command list found with active outstanding "
        "operations (all finished or not started yet).");
    }
  }

  D3D12_DRED_PAGE_FAULT_OUTPUT pagefault{};
  dred->GetPageFaultAllocationOutput(&pagefault);

  VDLogW("Gathered page fault allocation output.");

  D3D12_GPU_VIRTUAL_ADDRESS OutPageFaultGPUAddress =
    pageFault->PageFaultVA;
  VDLogW(
    "DRED: PageFault at VA GPUAddress \"0x%llX\"",
    (long long)OutPageFaultGPUAddress);

  const auto* Node = pageFault->pHeadExistingAllocationNode;
  if(Node) {
    VDLogW(
      "DRED: Active objects with VA ranges that match the faulting "
      "VA:");
    while(Node) {
      // When tracking all allocations then empty named dummy resources (heap & buffer)
      // are created for each texture to extract the GPUBaseAddress so don't write these out
      if(Node->ObjectNameW) {
        int32_t alloc_type_index =
          Node->AllocationType -
          D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE;
        const TCHAR* AllocTypeName =
          (alloc_type_index < D3D12_AllocTypesNamesCount)
            ? D3D12_AllocTypesNames[alloc_type_index]
            : TEXT("Unknown Alloc");
        if constexpr(std::is_same_v<
                       std::remove_reference_t<T>,
                       D3D12_DRED_PAGE_FAULT_OUTPUT1>) {
          VDLogW(
            "\tObject: %p, Name: %ls (Type: %ls)", Node->pObject,
            Node->ObjectNameW, AllocTypeName);
        } else {
          VDLogW(
            "\tName: %ls (Type: %ls)", Node->ObjectNameW,
            AllocTypeName);
        }
      }
      Node = Node->pNext;
    }
  }

  Node = pageFault->pHeadRecentFreedAllocationNode;
  if(Node) {
    VDLogW(
      "DRED: Recent freed objects with VA ranges that match the "
      "faulting VA:");
    while(Node) {
      // See comments above
      if(Node->ObjectNameW) {
        int32_t alloc_type_index =
          Node->AllocationType -
          D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE;
        const TCHAR* AllocTypeName =
          (alloc_type_index < D3D12_AllocTypesNamesCount)
            ? D3D12_AllocTypesNames[alloc_type_index]
            : TEXT("Unknown Alloc");
        if constexpr(std::is_same_v<
                       std::remove_reference_t<T>,
                       D3D12_DRED_PAGE_FAULT_OUTPUT1>) {
          VDLogW(
            "\tObject: %p, Name: %ls (Type: %ls)", Node->pObject,
            Node->ObjectNameW, AllocTypeName);
        } else {
          VDLogW(
            "\tName: %ls (Type: %ls)", Node->ObjectNameW,
            AllocTypeName);
        }
      }

      Node = Node->pNext;
    }
  }
  */
}

#include "vuldir/api/RenderContext.hpp"

#include "vuldir/api/Binder.hpp"
#include "vuldir/api/Buffer.hpp"
#include "vuldir/api/CommandBuffer.hpp"
#include "vuldir/api/Device.hpp"
#include "vuldir/api/Image.hpp"
#include "vuldir/api/Swapchain.hpp"

using namespace vd;

namespace {

// Returns the staging buffer to the free list on every exit path, including
// the exceptions Begin/Copy/Submit can throw part-way through an upload.
template<typename Fn>
struct ScopeExit {
  Fn fn;
  ScopeExit(const ScopeExit&)            = delete;
  ScopeExit& operator=(const ScopeExit&) = delete;
  explicit ScopeExit(Fn fn_): fn{std::move(fn_)} {}
  ~ScopeExit() { fn(); }
};

} // namespace

RenderContext::RenderContext(Device& device, const Desc& desc):
  m_device{device},
  m_desc{desc},
  m_framesInFlight{std::max(1u, desc.maxFramesInFlight)},
  m_frameIndex{0u},
  m_binderContext{0u},
  m_frames{},
  m_transfersFence{device, "Transfer", Fence::Type::Timeline, 0u},
  m_stagingBuffers{},
  m_freeStagingBuffers{}
{
  m_frames.resize(m_framesInFlight);

  for(u32 frame = 0u; frame < m_framesInFlight; ++frame) {
    auto& slot = m_frames[frame];
    for(u32 idx = 0u; idx < QueueTypeCount; ++idx) {
      slot.cmdPools[idx] =
        std::make_unique<CommandPool>(m_device, QueueTypes[idx]);
      slot.cmdBufs[idx] =
        std::make_unique<CommandBuffer>(m_device, *slot.cmdPools[idx]);
    }
    slot.fence = std::make_unique<Fence>(
      m_device, formatString("FrameFence%u", frame),
#ifdef VD_API_VK
      Fence::Type::Fence
#elif VD_API_DX
      Fence::Type::Timeline
#endif
    );
    slot.submitted = false;
  }

  m_binderContext =
    m_device.GetBinder().RegisterFrameContext(m_framesInFlight);
}

RenderContext::~RenderContext()
{
  try {
    WaitInFlightOperations();
  } catch(const std::exception& error) {
    VDLogE(
      "RenderContext teardown could not drain GPU work: %s",
      error.what());
  }
  m_device.GetBinder().UnregisterFrameContext(m_binderContext);

  for(auto& slot: m_frames) {
    for(auto& cmd: slot.cmdBufs) cmd = nullptr;
    for(auto& pool: slot.cmdPools) pool = nullptr;
    slot.fence = nullptr;
  }
  m_frames.clear();
}

void RenderContext::ensureGraphicsReady(
  CommandBuffer& graphicsCmd) const
{
  if(graphicsCmd.GetState() != CommandBuffer::State::Ready) {
    throw std::runtime_error(
      "RenderContext::Write requires the Graphics command buffer to be "
      "Ready (finish/End the frame recording and Reset before Write)");
  }
}

void RenderContext::retireFrame(u32 slot)
{
  m_device.GetBinder().RetireFrame(m_binderContext, slot);
}

void RenderContext::Reset()
{
  m_frameIndex =
    m_device.GetSwapchain().GetFrameIndex() % m_framesInFlight;
  auto& slot = m_frames[m_frameIndex];

  if(slot.submitted) {
    if(!slot.fence->Wait())
      throw std::runtime_error("Failed to wait for frame fence");
#ifdef VD_API_VK
    slot.fence->Reset();
#endif
    slot.submitted = false;
  }

  retireFrame(m_frameIndex);
  m_device.GetBinder().BeginFrame(m_binderContext, m_frameIndex);

  for(auto& cmd: slot.cmdBufs) {
    if(cmd->GetState() == CommandBuffer::State::Ready) {
      // Already reset (e.g. after Write); leave Ready.
      continue;
    }
    cmd->reset(true);
  }
}

bool RenderContext::Write(
  Buffer& buffer, Span<u8 const> data, ResourceState finalState)
{
  const auto size = std::size(data);

  VDLogI(
    "Writing %zu bytes to buffer %s", size,
    buffer.GetDesc().name.c_str());

  if(buffer.GetMemoryType() != MemoryType::Main)
    return buffer.Write(data);
  if(size > buffer.GetSize()) {
    VDLogE(
      "Write exceeds the declared size of buffer '%s'",
      buffer.GetDesc().name.c_str());
    return false;
  }
  if(data.empty()) return true;

  auto& stagingBuffer = getStagingBuffer(size);
  const ScopeExit stagingGuard{
    [&] { returnStagingBuffer(stagingBuffer); }};
  if(!stagingBuffer.Write(data)) return false;

  // Copy queues cannot transition into graphics/compute states, so final
  // transitions run on the graphics queue. makeQueueInfo pins the Copy role
  // to the graphics family, so no ownership transfer is needed.
  auto& transferCmd = GetCmd(QueueType::Copy);
  auto& graphicsCmd = GetCmd(QueueType::Graphics);
  ensureGraphicsReady(graphicsCmd);

  transferCmd.Begin();
  transferCmd.AddBarrier(stagingBuffer, ResourceState::CopySrc);
  transferCmd.AddBarrier(buffer, ResourceState::CopyDst);
  transferCmd.FlushBarriers();
  transferCmd.Copy(stagingBuffer, buffer, 0, 0, size);
  transferCmd.End();
  m_device.Submit({&transferCmd}, {}, {&m_transfersFence});

  graphicsCmd.Begin();
  graphicsCmd.AddBarrier(buffer, finalState);
  graphicsCmd.FlushBarriers();
  graphicsCmd.End();

  m_device.Submit(
    {&graphicsCmd}, {&m_transfersFence}, {&m_transfersFence});

  if(!m_transfersFence.Wait())
    throw std::runtime_error("Failed to wait for buffer upload");
  transferCmd.Reset();
  graphicsCmd.Reset();

  return true;
}

bool RenderContext::Write(
  Image& image, Span<u8 const> data, ResourceState finalState)
{
  const auto size = std::size(data);

  VDLogI(
    "Writing %zu bytes to image %s", size,
    image.GetDesc().name.c_str());

  if(image.GetMemoryType() != MemoryType::Main)
    return image.Write(data);

  const auto& desc   = image.GetDesc();
  u32         layers = 1u;
  if(desc.dimension == Dimension::eCube) {
    layers = std::max(6u, desc.extent[2]);
  } else if(
    desc.dimension == Dimension::e1D ||
    desc.dimension == Dimension::e2D) {
    layers = std::max(1u, desc.extent[2]);
  }

  struct CopyRegion {
    u64 offset;
    u32 mip;
    u32 layer;
  };
  Arr<CopyRegion> copyRegions;
  const u64 regionCount = static_cast<u64>(layers) * desc.mips;
  if(regionCount > copyRegions.max_size())
    throw std::overflow_error(
      "RenderContext::Write: image region count overflow");
  copyRegions.reserve(static_cast<size_t>(regionCount));

  u64 inputOffset = 0u;
#ifdef VD_API_DX
  u64     stagingOffset = 0u;
  Arr<u8> paddedData;
#endif
  for(u32 layer = 0u; layer < layers; ++layer) {
    UInt3 mipExtent = desc.extent;
    if(desc.dimension != Dimension::e3D) mipExtent[2] = 1u;

    for(u32 mip = 0u; mip < desc.mips; ++mip) {
      const u32 depth = desc.dimension == Dimension::e3D
                          ? std::max(1u, mipExtent[2])
                          : 1u;
      const u64 formatSize = getFormatSize(desc.format);
      if(formatSize == 0u || mipExtent[0] > MaxU64 / formatSize)
        throw std::overflow_error(
          "RenderContext::Write: image row size overflow");
      const u64 rowSize = static_cast<u64>(mipExtent[0]) * formatSize;
      if(mipExtent[1] > MaxU64 / rowSize)
        throw std::overflow_error(
          "RenderContext::Write: image mip size overflow");
      const u64 sliceSize = rowSize * mipExtent[1];
      if(depth > MaxU64 / sliceSize)
        throw std::overflow_error(
          "RenderContext::Write: image mip size overflow");
      const u64 mipSize = sliceSize * depth;
      if(inputOffset > size || mipSize > size - inputOffset)
        throw std::runtime_error(
          "RenderContext::Write: image data too small for mips/layers");

#ifdef VD_API_DX
      const u64 rowPitch = alignUp(
        rowSize, static_cast<u64>(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));
      if(
        stagingOffset >
        MaxU64 - (D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1u))
        throw std::overflow_error(
          "RenderContext::Write: upload offset overflow");
      stagingOffset = alignUp(
        stagingOffset,
        static_cast<u64>(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT));
      if(mipExtent[1] > MaxU64 / rowPitch)
        throw std::overflow_error(
          "RenderContext::Write: upload size overflow");
      const u64 paddedSliceSize = rowPitch * mipExtent[1];
      if(depth > MaxU64 / paddedSliceSize)
        throw std::overflow_error(
          "RenderContext::Write: upload size overflow");
      const u64 paddedMipSize = paddedSliceSize * depth;
      if(stagingOffset > MaxU64 - paddedMipSize)
        throw std::runtime_error("RenderContext::Write: upload size overflow");
      paddedData.resize(stagingOffset + paddedMipSize);

      for(u32 z = 0u; z < depth; ++z) {
        for(u32 row = 0u; row < mipExtent[1]; ++row) {
          const u64 srcOffset =
            inputOffset + (static_cast<u64>(z) * mipExtent[1] + row) * rowSize;
          const u64 dstOffset = stagingOffset +
                                (static_cast<u64>(z) * mipExtent[1] + row) *
                                  rowPitch;
          std::memcpy(
            paddedData.data() + dstOffset, data.data() + srcOffset,
            rowSize);
        }
      }
      copyRegions.push_back({stagingOffset, mip, layer});
      stagingOffset += paddedMipSize;
#else
      copyRegions.push_back({inputOffset, mip, layer});
#endif
      inputOffset += mipSize;

      if(mipExtent[0] > 1u) mipExtent[0] /= 2u;
      if(mipExtent[1] > 1u) mipExtent[1] /= 2u;
      if(desc.dimension == Dimension::e3D && mipExtent[2] > 1u)
        mipExtent[2] /= 2u;
    }
  }
  if(inputOffset != size)
    throw std::invalid_argument(
      "RenderContext::Write: image data size does not match mips/layers");

#ifdef VD_API_DX
  const Span<u8 const> uploadData{paddedData};
#else
  const Span<u8 const> uploadData = data;
#endif
  auto& stagingBuffer = getStagingBuffer(uploadData.size());
  const ScopeExit stagingGuard{
    [&] { returnStagingBuffer(stagingBuffer); }};
  if(!stagingBuffer.Write(uploadData)) return false;

  auto& transferCmd = GetCmd(QueueType::Copy);
  auto& graphicsCmd = GetCmd(QueueType::Graphics);
  ensureGraphicsReady(graphicsCmd);

  transferCmd.Begin();
  transferCmd.AddBarrier(stagingBuffer, ResourceState::CopySrc);
  transferCmd.AddBarrier(image, ResourceState::CopyDst);
  transferCmd.FlushBarriers();

  for(const auto& region: copyRegions)
    transferCmd.Copy(
      stagingBuffer, image, region.offset, region.mip, region.layer);

  transferCmd.End();
  m_device.Submit({&transferCmd}, {}, {&m_transfersFence});

  graphicsCmd.Begin();
  graphicsCmd.AddBarrier(image, finalState);
  graphicsCmd.FlushBarriers();
  graphicsCmd.End();

  m_device.Submit(
    {&graphicsCmd}, {&m_transfersFence}, {&m_transfersFence});

  if(!m_transfersFence.Wait())
    throw std::runtime_error("Failed to wait for image upload");
  transferCmd.Reset();
  graphicsCmd.Reset();

  return true;
}

void vd::RenderContext::Submit(
  Arr<CommandBuffer*> cmdbufs, Arr<Fence*> waits, Arr<Fence*> signals,
  SwapchainDep swapchainDep)
{
  auto& slot = m_frames[m_frameIndex];
  m_device.Submit(
    std::move(cmdbufs), std::move(waits), std::move(signals),
    slot.fence.get(), swapchainDep);
  slot.submitted = true;
}

void vd::RenderContext::WaitInFlightOperations()
{
  for(u32 idx = 0u; idx < m_framesInFlight; ++idx) {
    auto& slot = m_frames[idx];
    if(slot.submitted) {
      if(!slot.fence->Wait())
        throw std::runtime_error("Failed to wait for frame fence");
#ifdef VD_API_VK
      slot.fence->Reset();
#endif
      slot.submitted = false;
    }
    retireFrame(idx);
  }
  m_device.GetBinder().FlushDeferred(m_binderContext);
}

Buffer& RenderContext::getStagingBuffer(u64 size)
{
  // Best fit: first fit would give a large buffer to a small upload.
  auto bestIt = std::end(m_freeStagingBuffers);
  for(auto bufIt = std::begin(m_freeStagingBuffers);
      bufIt != std::end(m_freeStagingBuffers); ++bufIt) {
    if((*bufIt)->GetSize() < size) continue;
    if(
      bestIt == std::end(m_freeStagingBuffers) ||
      (*bufIt)->GetSize() < (*bestIt)->GetSize())
      bestIt = bufIt;
  }
  if(bestIt != std::end(m_freeStagingBuffers)) {
    auto& buf = **bestIt;
    m_freeStagingBuffers.erase(bestIt);
    return buf;
  }

  auto buf = std::make_unique<Buffer>(
    m_device, Buffer::Desc{
                .name        = "Staging",
                .usage       = {},
                .size        = size,
                .defaultView = {},
                .memoryType  = MemoryType::Upload,
                .isStaging   = true});
  m_stagingBuffers.push_back(std::move(buf));
  return *m_stagingBuffers.back();
}

void RenderContext::returnStagingBuffer(Buffer& buffer)
{
  // Write() waits m_transfersFence, so the buffer is immediately reusable.
  // Deferring to frame-slot retirement would make every upload in a single
  // load allocate its own staging buffer.
  m_freeStagingBuffers.push_back(&buffer);
}

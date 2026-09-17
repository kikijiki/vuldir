// CommandBuffer::State machine (Ready -> Recording -> Closed -> Ready) and its
// guards: Reset cannot recover a Recording buffer, Begin requires Ready, End
// requires Recording. Needs a real Device but never submits or touches the
// swapchain.

#include "TestEnv.hpp"

#include "vuldir/Vuldir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <barrier>
#include <cstring>
#include <exception>
#include <thread>
#include <type_traits>

using namespace vd;
using namespace vdtest;

TEST_CASE("timeline fences retain their initial target", "[unit][fence]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  Fence fence(*env->device, Fence::Type::Timeline, 7u);
  REQUIRE(fence.GetTarget() == 7u);
}

#ifdef VD_API_VK
TEST_CASE("binary synchronization values are rejected", "[unit][fence]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  REQUIRE_THROWS_AS(
    Fence(*env->device, Fence::Type::Binary, 1u),
    std::invalid_argument);
  Fence semaphore(*env->device, Fence::Type::Binary);
  REQUIRE_THROWS_AS(semaphore.Reset(), std::runtime_error);
  REQUIRE_THROWS_AS(semaphore.GetFenceHandle(), std::runtime_error);

  Fence fence(*env->device, Fence::Type::Fence);
  REQUIRE_THROWS_AS(fence.GetSemaphoreHandle(), std::runtime_error);
}
#endif

TEST_CASE("timeline fence targets advance concurrently", "[unit][fence][threads]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  Fence fence(*env->device, Fence::Type::Timeline, 3u);
  constexpr u32 workerCount = 8u;
  constexpr u32 stepsPerWorker = 1000u;
  Arr<std::jthread> workers;
  for(u32 worker = 0u; worker < workerCount; ++worker) {
    workers.emplace_back([&] {
      for(u32 step = 0u; step < stepsPerWorker; ++step) fence.Step();
    });
  }
  for(auto& worker: workers) worker.join();

  REQUIRE(
    fence.GetTarget() ==
    3u + static_cast<u64>(workerCount) * stepsPerWorker);
}

TEST_CASE("timeline fence targets reject overflow", "[unit][fence]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  Fence fence(*env->device, Fence::Type::Timeline, MaxU64);
  REQUIRE_THROWS_AS(fence.Step(), std::overflow_error);
  REQUIRE(fence.GetTarget() == MaxU64);
}

TEST_CASE("explicit fence signals update the default target", "[unit][fence]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  Fence fence(*env->device);
  REQUIRE(fence.Signal(4u));
  REQUIRE(fence.GetTarget() == 4u);
  REQUIRE(fence.Wait(0u));
  REQUIRE_THROWS_AS(fence.Signal(3u), std::invalid_argument);
  REQUIRE(fence.GetTarget() == 4u);
}

#ifdef VD_API_VK
TEST_CASE("Vulkan queues wait and signal timeline fences", "[unit][fence]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  Fence upstream(*env->device);
  Fence downstream(*env->device);
  REQUIRE(upstream.Signal(2u));
  REQUIRE(env->device->Wait(QueueType::Graphics, upstream, 2u));
  REQUIRE(env->device->Signal(QueueType::Graphics, downstream, 5u));
  REQUIRE(downstream.GetTarget() == 5u);
  REQUIRE(downstream.Wait());
  REQUIRE(env->device->Signal(QueueType::Graphics, downstream));
  REQUIRE(downstream.GetTarget() == 6u);
  REQUIRE(downstream.Wait());
}
#endif

#ifdef VD_API_VK
TEST_CASE(
  "Vulkan copy queues support upload state transitions",
  "[unit][device][queue]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  const auto& device         = *env->device;
  const auto& physicalDevice = device.GetPhysicalDevice();
  REQUIRE(physicalDevice.HasGraphics(
    device.GetQueueFamily(QueueType::Copy)));
}
#endif

TEST_CASE("sampler anisotropy is validated", "[unit][sampler]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  Sampler::Desc desc{};
  desc.anisotropyEnable = true;
  desc.anisotropyMax    = 0.f;
  REQUIRE_THROWS_AS(Sampler(*env->device, desc), std::invalid_argument);

  desc.anisotropyMax = std::numeric_limits<f32>::infinity();
  REQUIRE_THROWS_AS(Sampler(*env->device, desc), std::invalid_argument);

  const auto& physicalDevice = env->device->GetPhysicalDevice();
  if(physicalDevice.GetFeatures()->samplerAnisotropy) {
    desc.anisotropyMax = std::min(
      2.f, physicalDevice.GetProperties()->limits.maxSamplerAnisotropy);
    REQUIRE_NOTHROW(Sampler(*env->device, desc));
  }
}

TEST_CASE("shader bytecode size is validated", "[unit][shader]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  REQUIRE_THROWS_AS(
    Shader(*env->device, std::span<char const>{}),
    std::invalid_argument);
  const SArr<char, 3u> truncated{};
  REQUIRE_THROWS_AS(
    Shader(*env->device, std::span<char const>{truncated}),
    std::invalid_argument);
}

TEST_CASE("buffer writes respect the declared size", "[unit][buffer]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  Buffer buffer(
    *env->device,
    Buffer::Desc{
      .name        = "Bounded upload buffer",
      .usage       = ResourceUsage::ShaderResource,
      .size        = 16u,
      .defaultView = std::nullopt,
      .memoryType  = MemoryType::Upload});
  const Arr<u8> exact(16u, 0x5au);
  const Arr<u8> oversized(17u, 0xa5u);
  const Arr<u8> empty;

  REQUIRE(buffer.Write(exact));
  REQUIRE(!buffer.Write(oversized));
  REQUIRE(buffer.Write(empty));

  Arr<u8> readback(16u);
  REQUIRE(buffer.Read(readback));
  REQUIRE(readback == exact);
  readback.resize(17u);
  REQUIRE(!buffer.Read(readback));
}

TEST_CASE("memory pool allocations release on scope exit", "[unit][memory]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  MemoryPool pool(*env->device, MemoryType::Upload, 4096u, MaxU32);
  REQUIRE(pool.GetUsedSize() == 0u);
  REQUIRE(!pool.Allocate(0u).IsValid());
  REQUIRE(!pool.Allocate(1u, 0u).IsValid());
  {
    auto leading = pool.Allocate(1u);
    REQUIRE(leading.IsValid());
    REQUIRE(!pool.Allocate(4095u, 1ull << 63u).IsValid());
  }
  {
    auto allocation = pool.Allocate(128u, 64u);
    REQUIRE(allocation.IsValid());
    REQUIRE(pool.GetUsedSize() >= 128u);
  }
  REQUIRE(pool.GetUsedSize() == 0u);
}

TEST_CASE("memory pools reject foreign allocations", "[unit][memory]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  REQUIRE_THROWS_AS(
    MemoryPool(*env->device, MemoryType::Upload, 0u, MaxU32),
    std::invalid_argument);
  MemoryPool first(*env->device, MemoryType::Upload, 4096u, MaxU32);
  MemoryPool second(*env->device, MemoryType::Upload, 4096u, MaxU32);
  auto allocation = first.Allocate(16u);
  Arr<u8> bytes(16u);
  REQUIRE_THROWS_AS(
    second.Write(allocation, bytes), std::invalid_argument);
}

TEST_CASE("zero-sized buffers are rejected", "[unit][buffer]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  REQUIRE_THROWS_AS(
    Buffer(
      *env->device,
      Buffer::Desc{
        .name        = "Empty buffer",
        .usage       = {},
        .size        = 0u,
        .defaultView = std::nullopt,
        .memoryType  = MemoryType::Main}),
    std::invalid_argument);
}

TEST_CASE("buffer views stay within the logical buffer", "[unit][buffer]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  Buffer buffer(
    *env->device,
    Buffer::Desc{
      .name        = "View range buffer",
      .usage       = ResourceUsage::ShaderResource,
      .size        = 16u,
      .defaultView = std::nullopt,
      .memoryType  = MemoryType::Main});

  REQUIRE(buffer.AddView(ViewType::SRV, {.offset = 0u, .size = 8u}) == 0u);
  REQUIRE_THROWS_AS(
    buffer.AddView(ViewType::SRV, {.offset = 17u, .size = 1u}),
    std::invalid_argument);
  REQUIRE_THROWS_AS(
    buffer.AddView(ViewType::SRV, {.offset = 12u, .size = 8u}),
    std::invalid_argument);
  REQUIRE_THROWS_AS(
    buffer.AddView(ViewType::SRV, {.offset = 16u}),
    std::invalid_argument);
}

TEST_CASE("image array and cube layer counts are normalized", "[unit][image]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  Image image(
    *env->device,
    Image::Desc{
      .name        = "Single-layer image",
      .usage       = ResourceUsage::ShaderResource,
      .format      = Format::R8G8B8A8_UNORM,
      .dimension   = Dimension::e2D,
      .extent      = {4u, 4u, 0u},
      .defaultView = std::nullopt});
  REQUIRE(image.GetDesc().extent[2] == 1u);

  Image cube(
    *env->device,
    Image::Desc{
      .name        = "Cube image",
      .usage       = ResourceUsage::ShaderResource,
      .format      = Format::R8G8B8A8_UNORM,
      .dimension   = Dimension::eCube,
      .extent      = {4u, 4u, 0u},
      .defaultView = std::nullopt});
  REQUIRE(cube.GetDesc().extent[2] == 6u);

  REQUIRE_THROWS_AS(
    Image(
      *env->device,
      Image::Desc{
        .name        = "Invalid cube array",
        .usage       = ResourceUsage::ShaderResource,
        .format      = Format::R8G8B8A8_UNORM,
        .dimension   = Dimension::eCube,
        .extent      = {4u, 4u, 7u},
        .defaultView = std::nullopt}),
    std::invalid_argument);
}

TEST_CASE("owned images reject directly mapped memory", "[unit][image]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  for(const auto memoryType: {MemoryType::Upload, MemoryType::Download}) {
    REQUIRE_THROWS_AS(
      Image(
        *env->device,
        Image::Desc{
          .name        = "Mapped image",
          .usage       = ResourceUsage::ShaderResource,
          .format      = Format::R8G8B8A8_UNORM,
          .dimension   = Dimension::e2D,
          .extent      = {4u, 4u, 1u},
          .defaultView = std::nullopt,
          .memoryType  = memoryType}),
      std::invalid_argument);
  }
}

TEST_CASE("image descriptors reject invalid native layouts", "[unit][image]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  const auto makeImage = [&](const Image::Desc& desc) {
    return std::make_unique<Image>(*env->device, desc);
  };
  const Image::Desc valid{
    .name        = "Validated image",
    .usage       = ResourceUsage::ShaderResource,
    .format      = Format::R8G8B8A8_UNORM,
    .dimension   = Dimension::e2D,
    .extent      = {4u, 4u, 1u},
    .defaultView = std::nullopt};

  auto desc = valid;
  desc.format = Format::UNDEFINED;
  REQUIRE_THROWS_AS(makeImage(desc), std::invalid_argument);
  desc = valid;
  desc.samples = 3u;
  REQUIRE_THROWS_AS(makeImage(desc), std::invalid_argument);
  desc = valid;
  desc.mips = 4u;
  REQUIRE_THROWS_AS(makeImage(desc), std::invalid_argument);
  desc = valid;
  desc.dimension = Dimension::eCube;
  desc.extent    = {4u, 8u, 6u};
  REQUIRE_THROWS_AS(makeImage(desc), std::invalid_argument);
  desc = valid;
  desc.usage = ResourceUsage::DepthStencil;
  REQUIRE_THROWS_AS(makeImage(desc), std::invalid_argument);
  desc = valid;
  desc.samples = 2u;
  desc.mips    = 2u;
  REQUIRE_THROWS_AS(makeImage(desc), std::invalid_argument);
}

#ifdef VD_API_VK
TEST_CASE("external images reject direct writes", "[unit][image]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  const Image::Desc desc{
    .name        = "Owned image",
    .usage       = ResourceUsage::ShaderResource,
    .format      = Format::R8G8B8A8_UNORM,
    .dimension   = Dimension::e2D,
    .extent      = {1u, 1u, 1u},
    .defaultView = std::nullopt};
  Image owned(*env->device, desc);
  auto wrappedDesc   = desc;
  wrappedDesc.name   = "Wrapped image";
  wrappedDesc.handle = owned.GetHandle();
  Image wrapped(*env->device, wrappedDesc);
  const Arr<u8> pixel(4u, 0xffu);
  REQUIRE(!wrapped.Write(pixel));
}
#endif

TEST_CASE("pipeline descriptors reject unsafe attachment counts", "[unit][pipeline]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  Pipeline::GraphicsDesc desc{};
  desc.colorFormats.resize(9u, Format::R8G8B8A8_UNORM);
  desc.blendAttachments.resize(9u);
  REQUIRE_THROWS_AS(Pipeline(*env->device, desc), std::invalid_argument);

  desc.colorFormats.resize(1u);
  desc.blendAttachments.clear();
  REQUIRE_THROWS_AS(Pipeline(*env->device, desc), std::invalid_argument);

  desc = {};
  desc.dynamicLineWidth = true;
  REQUIRE_THROWS_AS(Pipeline(*env->device, desc), std::invalid_argument);

  desc = {};
  desc.sampleQuality = 1u;
  REQUIRE_THROWS_AS(Pipeline(*env->device, desc), std::invalid_argument);

  REQUIRE_THROWS_AS(
    Pipeline(*env->device, Pipeline::ComputeDesc{}),
    std::invalid_argument);
}

TEST_CASE("image views validate ranges and usage", "[unit][image]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  Image image(
    *env->device,
    Image::Desc{
      .name        = "View range image",
      .usage       = ResourceUsage::ShaderResource,
      .format      = Format::R8G8B8A8_UNORM,
      .dimension   = Dimension::e2D,
      .extent      = {8u, 8u, 2u},
      .defaultView = std::nullopt,
      .mips        = 3u});

  REQUIRE(
    image.AddView(
      ViewType::SRV,
      {.mipOffset = 1u, .mipCount = 2u,
       .layerOffset = 1u, .layerCount = 1u}) == 0u);
  REQUIRE_THROWS_AS(
    image.AddView(ViewType::SRV, {.mipOffset = 3u}),
    std::invalid_argument);
  REQUIRE_THROWS_AS(
    image.AddView(ViewType::SRV, {.layerOffset = 2u}),
    std::invalid_argument);
  REQUIRE_THROWS_AS(
    image.AddView(ViewType::RTV), std::invalid_argument);
}

TEST_CASE("fence batch waits broadcast a single value", "[unit][fence]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  Fence fences[]{
    {*env->device, Fence::Type::Timeline, 7u},
    {*env->device, Fence::Type::Timeline, 7u}};
  REQUIRE(Fence::WaitAll(fences, 7u, 0u));
  REQUIRE(Fence::WaitAny(fences, 7u, 0u));
}

TEST_CASE("fence batch operations reject mismatched values", "[unit][fence]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  Fence fences[]{{*env->device}, {*env->device}};
  u64 values[]{1u, 2u, 3u};
  REQUIRE_THROWS_AS(Fence::WaitAll(fences, values), std::invalid_argument);
  REQUIRE_THROWS_AS(Fence::SignalAll(fences, values), std::invalid_argument);
}

TEST_CASE(
  "reset discards unsubmitted resource-state changes",
  "[unit][commandbuffer][barrier]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  Buffer buffer(
    *env->device,
    Buffer::Desc{
      .name        = "Abandoned barrier buffer",
      .usage       = ResourceUsage::ShaderResource,
      .size        = 256u,
      .defaultView = std::nullopt,
      .memoryType  = MemoryType::Main});
  CommandPool pool(*env->device, QueueType::Graphics);
  CommandBuffer cmd(*env->device, pool);

  cmd.Begin();
  cmd.AddBarrier(buffer, ResourceState::CopyDst);
  cmd.FlushBarriers();
  cmd.End();
  REQUIRE(buffer.GetState() == ResourceState::Undefined);

  cmd.Reset();
  cmd.Begin();
  cmd.End();
  env->device->Submit({&cmd});
  REQUIRE(buffer.GetState() == ResourceState::Undefined);
  env->device->WaitIdle(QueueType::Graphics);
}

TEST_CASE(
  "submission commits the final state across barrier flushes",
  "[unit][commandbuffer][barrier]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  Image image(
    *env->device,
    Image::Desc{
      .name        = "Submitted barrier image",
      .usage       = ResourceUsage::ShaderResource,
      .format      = Format::R8G8B8A8_UNORM,
      .dimension   = Dimension::e2D,
      .extent      = {8u, 8u, 1u},
      .defaultView = std::nullopt,
      .mips        = 1u,
      .memoryType  = MemoryType::Main});
  CommandPool pool(*env->device, QueueType::Graphics);
  CommandBuffer cmd(*env->device, pool);

  cmd.Begin();
  cmd.AddBarrier(image, ResourceState::CopyDst);
  cmd.FlushBarriers();
  REQUIRE(image.GetState() == ResourceState::Undefined);
  cmd.AddBarrier(image, ResourceState::ShaderResourceGraphics);
  cmd.FlushBarriers();
  cmd.End();
  REQUIRE(image.GetState() == ResourceState::Undefined);

  env->device->Submit({&cmd});
  REQUIRE(image.GetState() == ResourceState::ShaderResourceGraphics);
  env->device->WaitIdle(QueueType::Graphics);
}

TEST_CASE(
  "concurrent stale resource-state submissions are rejected",
  "[unit][commandbuffer][barrier][concurrency]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  Buffer buffer(
    *env->device,
    Buffer::Desc{
      .name        = "Concurrent state buffer",
      .usage       = ResourceUsage::ShaderResource,
      .size        = 256u,
      .defaultView = std::nullopt,
      .memoryType  = MemoryType::Main});

  constexpr u32 workerCount = 2u;
  const ResourceState destinations[workerCount] = {
    ResourceState::CopyDst,
    ResourceState::ShaderResourceGraphics};
  std::barrier<> recorded(static_cast<std::ptrdiff_t>(workerCount));
  SArr<bool, workerCount> submitted{};
  SArr<std::exception_ptr, workerCount> errors{};
  Arr<std::jthread> workers;

  for(u32 worker = 0u; worker < workerCount; ++worker) {
    workers.emplace_back([&, worker] {
      try {
        CommandPool pool(*env->device, QueueType::Graphics);
        CommandBuffer cmd(*env->device, pool);
        cmd.Begin();
        cmd.AddBarrier(buffer, destinations[worker]);
        cmd.FlushBarriers();
        cmd.End();

        // Both recordings captured Undefined before either may submit.
        recorded.arrive_and_wait();
        env->device->Submit({&cmd});
        submitted[worker] = true;
        env->device->WaitIdle(QueueType::Graphics);
      } catch(...) {
        errors[worker] = std::current_exception();
      }
    });
  }
  for(auto& worker: workers) worker.join();

  const u32 successCount =
    static_cast<u32>(submitted[0]) + static_cast<u32>(submitted[1]);
  REQUIRE(successCount == 1u);
  const u32 winner = submitted[0] ? 0u : 1u;
  const u32 loser  = 1u - winner;
  REQUIRE(buffer.GetState() == destinations[winner]);
  REQUIRE(!errors[winner]);
  REQUIRE(errors[loser]);
  try {
    std::rethrow_exception(errors[loser]);
  } catch(const std::runtime_error& error) {
    REQUIRE(Strv{error.what()}.find("stale") != Strv::npos);
  }
}

namespace {

// Not a fixture class: SKIP() must run inside the TEST_CASE body.
struct Cmd {
  GpuEnv*             env;
  UPtr<CommandPool>   pool;
  UPtr<CommandBuffer> cmd;

  Cmd(): env{GetGpuEnv()}
  {
    if(!env) return;
    pool = std::make_unique<CommandPool>(*env->device, QueueType::Graphics);
    cmd  = std::make_unique<CommandBuffer>(*env->device, *pool);
  }
};

} // namespace

TEST_CASE(
  "CommandBuffer starts Ready right after construction",
  "[unit][commandbuffer]")
{
  Cmd c;
  if(!c.env) { SKIP("No GPU/display available"); }

  REQUIRE(c.cmd->GetState() == CommandBuffer::State::Ready);
  REQUIRE_THROWS_AS(c.env->device->Submit({c.cmd.get()}), std::runtime_error);
  REQUIRE_THROWS_AS(
    c.env->device->GetSwapchain().GetView(MaxU32), std::out_of_range);
}

TEST_CASE(
  "CommandBuffer walks Ready -> Recording -> Closed -> Ready",
  "[unit][commandbuffer]")
{
  Cmd c;
  if(!c.env) { SKIP("No GPU/display available"); }

  c.cmd->Begin();
  REQUIRE(c.cmd->GetState() == CommandBuffer::State::Recording);

  c.cmd->End();
  REQUIRE(c.cmd->GetState() == CommandBuffer::State::Closed);

  c.cmd->Reset();
  REQUIRE(c.cmd->GetState() == CommandBuffer::State::Ready);
}

TEST_CASE(
  "Device rejects invalid command synchronization objects",
  "[unit][commandbuffer][fence]")
{
  Cmd c;
  if(!c.env) { SKIP("No GPU/display available"); }

  c.cmd->Begin();
  c.cmd->End();
  Fence* nullFence = nullptr;
  REQUIRE_THROWS_AS(
    c.env->device->Submit({c.cmd.get()}, {nullFence}),
    std::invalid_argument);
  REQUIRE_THROWS_AS(
    c.env->device->Submit({c.cmd.get()}, {}, {nullFence}),
    std::invalid_argument);

  Fence timeline(*c.env->device, Fence::Type::Timeline);
  REQUIRE_THROWS_AS(
    c.env->device->Submit({c.cmd.get()}, {}, {}, &timeline),
    std::invalid_argument);
}

TEST_CASE(
  "CommandBuffer::Reset is a no-op when already Ready",
  "[unit][commandbuffer]")
{
  Cmd c;
  if(!c.env) { SKIP("No GPU/display available"); }

  REQUIRE(c.cmd->GetState() == CommandBuffer::State::Ready);
  c.cmd->Reset();
  REQUIRE(c.cmd->GetState() == CommandBuffer::State::Ready);
}

TEST_CASE(
  "CommandBuffer::Reset cannot recover a Recording buffer",
  "[unit][commandbuffer]")
{
  Cmd c;
  if(!c.env) { SKIP("No GPU/display available"); }

  c.cmd->Begin();
  REQUIRE(c.cmd->GetState() == CommandBuffer::State::Recording);

  REQUIRE_THROWS_AS(c.cmd->Reset(), std::runtime_error);
  // State must be untouched by the failed Reset.
  REQUIRE(c.cmd->GetState() == CommandBuffer::State::Recording);

  // The documented recovery path (End, then Reset) must still work.
  c.cmd->End();
  c.cmd->Reset();
  REQUIRE(c.cmd->GetState() == CommandBuffer::State::Ready);
}

TEST_CASE(
  "CommandBuffer::Begin requires Ready state", "[unit][commandbuffer]")
{
  Cmd c;
  if(!c.env) { SKIP("No GPU/display available"); }

  c.cmd->Begin();
  REQUIRE_THROWS_AS(c.cmd->Begin(), std::runtime_error); // already Recording
  REQUIRE(c.cmd->GetState() == CommandBuffer::State::Recording);

  c.cmd->End(); // -> Closed
  REQUIRE_THROWS_AS(c.cmd->Begin(), std::runtime_error); // Closed, needs Reset
  REQUIRE(c.cmd->GetState() == CommandBuffer::State::Closed);

  c.cmd->Reset();
  c.cmd->Begin(); // now legal again
  REQUIRE(c.cmd->GetState() == CommandBuffer::State::Recording);
  c.cmd->End();
}

TEST_CASE(
  "CommandBuffer::End requires Recording state", "[unit][commandbuffer]")
{
  Cmd c;
  if(!c.env) { SKIP("No GPU/display available"); }

  REQUIRE(c.cmd->GetState() == CommandBuffer::State::Ready);
  REQUIRE_THROWS_AS(c.cmd->End(), std::runtime_error);
  REQUIRE(c.cmd->GetState() == CommandBuffer::State::Ready);

  c.cmd->Begin();
  c.cmd->End();
  REQUIRE_THROWS_AS(c.cmd->End(), std::runtime_error); // already Closed
  REQUIRE(c.cmd->GetState() == CommandBuffer::State::Closed);
}

TEST_CASE(
  "constructing a command buffer preserves recording siblings",
  "[unit][commandbuffer]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  CommandPool pool(*env->device, QueueType::Graphics);
  CommandBuffer first(*env->device, pool);
  first.Begin();
  CommandBuffer second(*env->device, pool);

  REQUIRE(first.GetState() == CommandBuffer::State::Recording);
  REQUIRE(second.GetState() == CommandBuffer::State::Ready);
  REQUIRE_NOTHROW(first.End());
}

TEST_CASE("invalid queue types are rejected", "[unit][commandbuffer]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  using Raw = std::underlying_type_t<QueueType>;
  const Raw raw = static_cast<Raw>(QueueTypeCount);
  QueueType invalid{};
  std::memcpy(&invalid, &raw, sizeof(invalid));
  REQUIRE_THROWS_AS(
    CommandPool(*env->device, invalid), std::invalid_argument);
  REQUIRE_THROWS_AS(env->device->WaitIdle(invalid), std::invalid_argument);
}

TEST_CASE(
  "CommandBuffer recording operations require Recording state",
  "[unit][commandbuffer]")
{
  Cmd c;
  if(!c.env) { SKIP("No GPU/display available"); }

  REQUIRE_THROWS_AS(c.cmd->AddBarrier(), std::runtime_error);
  REQUIRE_THROWS_AS(c.cmd->FlushBarriers(), std::runtime_error);
  REQUIRE_THROWS_AS(c.cmd->Draw(1u), std::runtime_error);
  REQUIRE_THROWS_AS(c.cmd->Dispatch(1u), std::runtime_error);
  REQUIRE_THROWS_AS(c.cmd->BeginRendering(), std::runtime_error);
  REQUIRE_THROWS_AS(c.cmd->EndRendering(), std::runtime_error);

  c.cmd->Begin();
  REQUIRE_THROWS_AS(c.cmd->EndRendering(), std::runtime_error);
  CommandBuffer::Attachment invalidAttachment;
  REQUIRE_THROWS_AS(
    c.cmd->BeginRendering(
      Span<CommandBuffer::Attachment const>{&invalidAttachment, 1u}),
    std::invalid_argument);
  c.cmd->End();

  REQUIRE_THROWS_AS(c.cmd->AddBarrier(), std::runtime_error);
}

TEST_CASE(
  "CommandBuffer validates push constant size",
  "[unit][commandbuffer][pipeline]")
{
  Cmd c;
  if(!c.env) { SKIP("No GPU/display available"); }

  SArr<u32, 5u> oversized{};
  c.cmd->Begin();
  REQUIRE_THROWS_AS(c.cmd->PushConstants({}), std::invalid_argument);
  REQUIRE_THROWS_AS(
    c.cmd->PushConstants(Span<u32 const>{oversized}),
    std::invalid_argument);
  c.cmd->End();
}

TEST_CASE(
  "CommandBuffer rejects work without the required pipeline state",
  "[unit][commandbuffer][pipeline]")
{
  Cmd c;
  if(!c.env) { SKIP("No GPU/display available"); }

  c.cmd->Begin();
  REQUIRE_THROWS_AS(c.cmd->Draw(1u), std::runtime_error);
  REQUIRE_THROWS_AS(c.cmd->DrawIndexed(1u), std::runtime_error);
  REQUIRE_THROWS_AS(c.cmd->Dispatch(1u), std::runtime_error);
  c.cmd->End();

  CommandPool copyPool(*c.env->device, QueueType::Copy);
  CommandBuffer copyCmd(*c.env->device, copyPool);
  copyCmd.Begin();
  REQUIRE_THROWS_AS(copyCmd.BeginRendering(), std::runtime_error);
  REQUIRE_THROWS_AS(copyCmd.SetViewport({}), std::runtime_error);
  REQUIRE_THROWS_AS(copyCmd.SetScissor({}), std::runtime_error);
  REQUIRE_THROWS_AS(copyCmd.PushConstants(u32{}), std::runtime_error);
  REQUIRE_THROWS_AS(copyCmd.Draw(1u), std::runtime_error);
  REQUIRE_THROWS_AS(copyCmd.Dispatch(1u), std::runtime_error);
  copyCmd.End();
}

TEST_CASE(
  "CommandBuffer copy operations validate ranges",
  "[unit][commandbuffer][copy]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  Buffer src(
    *env->device,
    Buffer::Desc{
      .name = "Copy source", .usage = {}, .size = 256u,
      .defaultView = std::nullopt, .memoryType = MemoryType::Upload});
  Buffer dst(
    *env->device,
    Buffer::Desc{
      .name = "Copy destination", .usage = {}, .size = 16u,
      .defaultView = std::nullopt, .memoryType = MemoryType::Main});
  Image image(
    *env->device,
    Image::Desc{
      .name = "Copy destination image",
      .usage = ResourceUsage::ShaderResource,
      .format = Format::R8G8B8A8_UNORM,
      .dimension = Dimension::e2D,
      .extent = {4u, 4u, 1u},
      .defaultView = std::nullopt,
      .mips = 1u});
  CommandPool pool(*env->device, QueueType::Graphics);
  CommandBuffer cmd(*env->device, pool);
  cmd.Begin();

  REQUIRE_THROWS_AS(cmd.Copy(src, dst, 0u, 0u, 0u), std::invalid_argument);
  REQUIRE_THROWS_AS(
    cmd.Copy(src, dst, MaxU64, 0u, 1u), std::out_of_range);
  REQUIRE_THROWS_AS(
    cmd.Copy(src, dst, 0u, 12u, 8u), std::out_of_range);
  REQUIRE_THROWS_AS(cmd.Copy(src, image, 0u, 1u, 0u), std::out_of_range);
  REQUIRE_THROWS_AS(cmd.Copy(src, image, 0u, 0u, 1u), std::out_of_range);
  REQUIRE_THROWS_AS(
    cmd.Copy(src, dst, 0u, 0u, 8u), std::invalid_argument);

  cmd.AddBarrier(src, ResourceState::CopySrc);
  cmd.AddBarrier(dst, ResourceState::CopyDst);
  cmd.AddBarrier(image, ResourceState::CopyDst);
  REQUIRE_NOTHROW(cmd.Copy(src, dst, 0u, 0u, 8u));
  REQUIRE_NOTHROW(cmd.Copy(src, image));

  cmd.End();
}

TEST_CASE(
  "CommandBuffer validates index-buffer bindings",
  "[unit][commandbuffer][buffer]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  Buffer buffer(
    *env->device,
    Buffer::Desc{
      .name = "Index binding buffer", .usage = ResourceUsage::IndexBuffer,
      .size = 8u, .defaultView = std::nullopt,
      .memoryType = MemoryType::Main});
  CommandPool pool(*env->device, QueueType::Graphics);
  CommandBuffer cmd(*env->device, pool);
  cmd.Begin();

  REQUIRE_THROWS_AS(
    cmd.BindIndexBuffer(buffer, IndexType::U32), std::invalid_argument);
  buffer.SetState(ResourceState::IndexBuffer);
  REQUIRE_THROWS_AS(
    cmd.BindIndexBuffer(buffer, IndexType::U32, 2u),
    std::invalid_argument);
  REQUIRE_THROWS_AS(
    cmd.BindIndexBuffer(buffer, IndexType::U32, 8u), std::out_of_range);
  REQUIRE_NOTHROW(cmd.BindIndexBuffer(buffer, IndexType::U32));
  cmd.End();
}

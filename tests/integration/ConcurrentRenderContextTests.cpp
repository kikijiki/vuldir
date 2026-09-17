// Multiple RenderContext instances on one Device: one test with real threads,
// the rest with deterministic interleavings on the test thread.

#include "TestEnv.hpp"

#include "vuldir/Vuldir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <barrier>
#include <exception>
#include <thread>

using namespace vd;
using namespace vdtest;

TEST_CASE(
  "RenderContexts upload concurrently and merge their results",
  "[integration][rendercontext][concurrency][threads]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  constexpr u32 workerCount = 4u;
  constexpr u32 uploadCount = 4u;
  constexpr u32 payloadSize = 4096u;

  struct WorkerResult {
    u64           checksum = 0u;
    u32           uploads  = 0u;
    ResourceState finalState = ResourceState::Undefined;
  };

  auto makePayload = [](u32 worker, u32 upload) {
    Arr<u8> payload(payloadSize);
    for(u32 byte = 0u; byte < payloadSize; ++byte) {
      payload[byte] = static_cast<u8>(
        (worker * 53u + upload * 29u + byte * 7u) & 0xffu);
    }
    return payload;
  };
  auto checksum = [](Span<u8 const> bytes) {
    u64 result = 1469598103934665603ull;
    for(u8 byte: bytes) {
      result ^= byte;
      result *= 1099511628211ull;
    }
    return result;
  };

  std::barrier<> startLine(static_cast<std::ptrdiff_t>(workerCount));
  std::atomic<u32> activeWorkers{0u};
  std::atomic<u32> maxActiveWorkers{0u};
  SArr<WorkerResult, workerCount> results{};
  SArr<std::exception_ptr, workerCount> errors{};
  Arr<std::jthread> workers;
  workers.reserve(workerCount);

  for(u32 worker = 0u; worker < workerCount; ++worker) {
    workers.emplace_back([&, worker] {
      startLine.arrive_and_wait();

      const u32 active = activeWorkers.fetch_add(1u) + 1u;
      u32       observedMaximum = maxActiveWorkers.load();
      while(
        observedMaximum < active &&
        !maxActiveWorkers.compare_exchange_weak(
          observedMaximum, active)) {}

      // Spin until all workers are active so their work overlaps.
      while(activeWorkers.load() != workerCount) std::this_thread::yield();

      try {
        auto& dev = *env->device;
        RenderContext context(
          dev, RenderContext::Desc{.maxFramesInFlight = 2u});
        Buffer destination(
          dev, Buffer::Desc{
                 .name = formatString("Thread upload %u", worker),
                 .usage = ResourceUsage::ShaderResource,
                 .size = payloadSize,
                 .defaultView = ViewType::SRV,
                 .memoryType = MemoryType::Main});
        Buffer readback(
          dev, Buffer::Desc{
                 .name = formatString("Thread readback %u", worker),
                 .usage = {},
                 .size = payloadSize,
                 .defaultView = std::nullopt,
                 .memoryType = MemoryType::Download});

        for(u32 upload = 0u; upload < uploadCount; ++upload) {
          context.Reset();
          const auto payload = makePayload(worker, upload);
          if(!context.Write(destination, Span<u8 const>{payload})) {
            throw std::runtime_error("Concurrent buffer upload failed");
          }

          auto& verifyCmd = context.GetCmd(QueueType::Graphics);
          verifyCmd.Begin();
          verifyCmd.AddBarrier(destination, ResourceState::CopySrc);
          verifyCmd.AddBarrier(readback, ResourceState::CopyDst);
          verifyCmd.FlushBarriers();
          verifyCmd.Copy(destination, readback, 0u, 0u, payloadSize);
          verifyCmd.AddBarrier(
            destination, ResourceState::ShaderResourceGraphics);
          verifyCmd.FlushBarriers();
          verifyCmd.End();
          context.Submit({&verifyCmd});
          context.WaitInFlightOperations();

          Arr<u8> copiedPayload(payloadSize);
          if(!readback.Read(copiedPayload))
            throw std::runtime_error("Concurrent buffer readback failed");
          results[worker].checksum ^= checksum(copiedPayload);
          ++results[worker].uploads;
        }

        context.WaitInFlightOperations();
        results[worker].finalState = destination.GetState();
      } catch(...) {
        errors[worker] = std::current_exception();
      }

      activeWorkers.fetch_sub(1u);
    });
  }

  for(auto& worker: workers) worker.join();

  REQUIRE(maxActiveWorkers.load() == workerCount);
  for(const auto& error: errors) {
    if(error) std::rethrow_exception(error);
  }

  u64 mergedChecksum = 0u;
  u32 mergedUploads  = 0u;
  u64 expectedChecksum = 0u;
  for(u32 worker = 0u; worker < workerCount; ++worker) {
    REQUIRE(results[worker].uploads == uploadCount);
    REQUIRE(
      results[worker].finalState ==
      ResourceState::ShaderResourceGraphics);

    mergedChecksum ^= results[worker].checksum;
    mergedUploads += results[worker].uploads;
    for(u32 upload = 0u; upload < uploadCount; ++upload) {
      const auto payload = makePayload(worker, upload);
      expectedChecksum ^= checksum(payload);
    }
  }

  REQUIRE(mergedUploads == workerCount * uploadCount);
  REQUIRE(mergedChecksum == expectedChecksum);
  env->device->WaitIdle();
}

TEST_CASE(
  "RenderContext rejects mismatched upload sizes before recording",
  "[integration][rendercontext][validation]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  RenderContext context(
    *env->device, RenderContext::Desc{.maxFramesInFlight = 1u});
  Buffer buffer(
    *env->device,
    Buffer::Desc{
      .name = "Bounded context upload", .usage = {}, .size = 16u,
      .defaultView = std::nullopt, .memoryType = MemoryType::Main});
  Image image(
    *env->device,
    Image::Desc{
      .name = "Exact context upload",
      .usage = ResourceUsage::ShaderResource,
      .format = Format::R8G8B8A8_UNORM,
      .dimension = Dimension::e2D,
      .extent = {2u, 2u, 1u},
      .defaultView = std::nullopt,
      .mips = 1u});
  const Arr<u8> empty;
  const Arr<u8> oversizedBuffer(17u);
  const Arr<u8> shortImage(15u);
  const Arr<u8> longImage(17u);

  REQUIRE(context.Write(buffer, empty));
  REQUIRE(!context.Write(buffer, oversizedBuffer));
  REQUIRE_THROWS_AS(context.Write(image, shortImage), std::runtime_error);
  REQUIRE_THROWS_AS(context.Write(image, longImage), std::invalid_argument);
  REQUIRE(
    context.GetCmd(QueueType::Graphics).GetState() ==
    CommandBuffer::State::Ready);
  REQUIRE(
    context.GetCmd(QueueType::Copy).GetState() ==
    CommandBuffer::State::Ready);
}

TEST_CASE(
  "Two RenderContexts on the same Device drain independently",
  "[integration][rendercontext][concurrency]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  auto& dev = *env->device;
  auto& sc  = dev.GetSwapchain();

  // Different ring sizes: ctxA matches the swapchain, ctxB does not, so
  // frame-slot bookkeeping must not be shared.
  RenderContext ctxA(
    dev, RenderContext::Desc{.maxFramesInFlight = sc.GetMaxFramesInFlight()});
  RenderContext ctxB(dev, RenderContext::Desc{.maxFramesInFlight = 3u});

  Buffer scratch(
    dev, Buffer::Desc{
           .name        = "Concurrent scratch",
           .usage       = ResourceUsage::ShaderResource,
           .size        = 256u,
           .defaultView = ViewType::SRV,
           .memoryType  = MemoryType::Main});
  const Arr<u8> payload(256u, 0x7Eu);

  constexpr u32 frameCount = 8u;
  for(u32 frame = 0u; frame < frameCount; ++frame) {
    // ctxA: present loop.
    ctxA.Reset();
    Image* backbuffer = sc.AcquireNextImage(true);
    REQUIRE(backbuffer != nullptr);

    auto& cmdA = ctxA.GetCmd(QueueType::Graphics);
    cmdA.Begin();
    cmdA.AddBarrier(*backbuffer, ResourceState::RenderTarget);
    cmdA.AddBarrier(*backbuffer, ResourceState::Present);
    cmdA.FlushBarriers();
    cmdA.End();
    ctxA.Submit({&cmdA}, {}, {}, SwapchainDep::AcquireRelease);
    sc.Present();
    sc.NextFrame();

    // ctxB: independent of the swapchain. Even frames use
    // RenderContext::Write; odd frames submit an empty Compute cmdbuf.
    ctxB.Reset();
    if(frame % 2u == 0u) {
      REQUIRE(ctxB.Write(scratch, Span<u8 const>(payload)));
    }
    else {
      auto& cmdB = ctxB.GetCmd(QueueType::Compute);
      cmdB.Begin();
      cmdB.AddBarrier();
      cmdB.FlushBarriers();
      cmdB.End();
      ctxB.Submit({&cmdB});
    }
  }

  // Each context drains its own work independently.
  ctxA.WaitInFlightOperations();
  ctxB.WaitInFlightOperations();
  dev.WaitIdle();
}

TEST_CASE(
  "Interleaved RenderContext::Reset does not require waiting on the "
  "other context's fence",
  "[integration][rendercontext][concurrency]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  auto& dev = *env->device;
  auto& sc  = dev.GetSwapchain();

  RenderContext ctxA(dev, RenderContext::Desc{.maxFramesInFlight = 2u});
  RenderContext ctxB(dev, RenderContext::Desc{.maxFramesInFlight = 2u});

  auto submitBarrierOnly = [&](RenderContext& ctx) {
    ctx.Reset();
    auto& cmd = ctx.GetCmd(QueueType::Graphics);
    cmd.Begin();
    cmd.AddBarrier();
    cmd.FlushBarriers();
    cmd.End();
    ctx.Submit({&cmd});
  };

  // Alternate A, B, A, B. Reset() waits only its own slot fence, so this
  // must not deadlock or throw.
  for(u32 i = 0u; i < 6u; ++i) {
    submitBarrierOnly(ctxA);
    submitBarrierOnly(ctxB);
  }

  ctxA.WaitInFlightOperations();
  ctxB.WaitInFlightOperations();
  dev.WaitIdle();

  // The device must still handle a normal acquire/present cycle, which also
  // consumes the semaphores so the next test starts clean.
  ctxA.Reset();
  Image* backbuffer = sc.AcquireNextImage(true);
  REQUIRE(backbuffer != nullptr);

  auto& cmd = ctxA.GetCmd(QueueType::Graphics);
  cmd.Begin();
  cmd.AddBarrier(*backbuffer, ResourceState::RenderTarget);
  cmd.AddBarrier(*backbuffer, ResourceState::Present);
  cmd.FlushBarriers();
  cmd.End();
  ctxA.Submit({&cmd}, {}, {}, SwapchainDep::AcquireRelease);
  sc.Present();
  sc.NextFrame();

  ctxA.WaitInFlightOperations();
  dev.WaitIdle();
}

TEST_CASE(
  "Descriptor reclaim waits for every active RenderContext",
  "[integration][rendercontext][concurrency][binder]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  auto& dev = *env->device;
  RenderContext ctxA(dev, RenderContext::Desc{.maxFramesInFlight = 2u});
  RenderContext ctxB(dev, RenderContext::Desc{.maxFramesInFlight = 3u});
  ctxA.Reset();
  ctxB.Reset();

  auto makeBuffer = [&](const char* name) {
    return std::make_unique<Buffer>(
      dev, Buffer::Desc{
             .name        = name,
             .usage       = ResourceUsage::ShaderResource,
             .size        = 256u,
             .defaultView = ViewType::SRV,
             .memoryType  = MemoryType::Main});
  };

  auto released = makeBuffer("Shared deferred descriptor");
  const u32 releasedIndex =
    released->GetView(ViewType::SRV)->binding.index;
  released.reset();

  auto beforeRetire = makeBuffer("Before either context retires");
  REQUIRE(
    beforeRetire->GetView(ViewType::SRV)->binding.index != releasedIndex);

  ctxA.WaitInFlightOperations();
  auto afterA = makeBuffer("After only context A retires");
  REQUIRE(afterA->GetView(ViewType::SRV)->binding.index != releasedIndex);

  ctxB.WaitInFlightOperations();
  auto afterBoth = makeBuffer("After both contexts retire");
  REQUIRE(afterBoth->GetView(ViewType::SRV)->binding.index == releasedIndex);

  afterBoth.reset();
  afterA.reset();
  beforeRetire.reset();
  dev.WaitIdle();
}

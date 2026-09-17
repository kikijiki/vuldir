// Single-RenderContext frame loop (Reset, Begin, End, Submit, Present,
// NextFrame) against a live 64x64 swapchain, plus the "Write requires
// Graphics Ready" guard on RenderContext::Write.

#include "TestEnv.hpp"

#include "vuldir/Vuldir.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace vd;
using namespace vdtest;

TEST_CASE(
  "RenderContext frame loop: Reset/Begin/End/Submit/Present/NextFrame "
  "over several frames",
  "[integration][rendercontext]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  auto& dev = *env->device;
  auto& sc  = dev.GetSwapchain();

  RenderContext ctx(
    dev, RenderContext::Desc{.maxFramesInFlight = sc.GetMaxFramesInFlight()});

  constexpr u32 frameCount = 6u;
  for(u32 frame = 0u; frame < frameCount; ++frame) {
    // Reset waits the slot fence; must precede Acquire so the acquire
    // semaphore is idle (VUID-vkAcquireNextImageKHR-semaphore-01779).
    ctx.Reset();

    Image* backbuffer = sc.AcquireNextImage(true);
    REQUIRE(backbuffer != nullptr);

    auto& cmd = ctx.GetCmd(QueueType::Graphics);
    REQUIRE(cmd.GetState() == CommandBuffer::State::Ready);

    cmd.Begin();

    cmd.SetViewport(
      Viewport{.offset = {}, .extent = sc.GetExtentF(), .depthExtent = {0, 1}});
    cmd.SetScissor(Rect{.offset = {0, 0}, .extent = sc.GetExtent()});

    cmd.AddBarrier(*backbuffer, ResourceState::RenderTarget);
    cmd.FlushBarriers();

    CommandBuffer::Attachment color{
      .view       = backbuffer->GetView(),
      .state      = ResourceState::RenderTarget,
      .loadOp     = LoadOp::Clear,
      .storeOp    = StoreOp::Store,
      .clearValue = Float4{0.1f, 0.2f, 0.3f, 1.f}};

    cmd.BeginRendering(Span<CommandBuffer::Attachment const>{&color, 1u});
    cmd.EndRendering();

    cmd.AddBarrier(*backbuffer, ResourceState::Present);
    cmd.FlushBarriers();

    cmd.End();
    REQUIRE(cmd.GetState() == CommandBuffer::State::Closed);

    ctx.Submit({&cmd}, {}, {}, SwapchainDep::AcquireRelease);
    sc.Present();
    sc.NextFrame();
  }

  ctx.WaitInFlightOperations();
  dev.WaitIdle();
}

TEST_CASE(
  "RenderContext::Write throws if the Graphics cmdbuf is Recording",
  "[integration][rendercontext]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  auto& dev = *env->device;

  RenderContext ctx(dev, RenderContext::Desc{.maxFramesInFlight = 2u});
  ctx.Reset();

  Buffer buffer(
    dev, Buffer::Desc{
           .name        = "WriteGuardTarget",
           .usage       = ResourceUsage::ShaderResource,
           .size        = 256u,
           .defaultView = ViewType::SRV,
           .memoryType  = MemoryType::Main});

  const Arr<u8> payload(256u, 0xABu);

  // In-progress recording on Graphics.
  auto& graphics = ctx.GetCmd(QueueType::Graphics);
  graphics.Begin();
  REQUIRE(graphics.GetState() == CommandBuffer::State::Recording);

  REQUIRE_THROWS_AS(
    ctx.Write(buffer, Span<u8 const>(payload)), std::runtime_error);

  // The failed Write must not change the Graphics state.
  REQUIRE(graphics.GetState() == CommandBuffer::State::Recording);

  // Leave the cmdbuf Ready for other tests.
  graphics.End();
  graphics.Reset();
  REQUIRE(graphics.GetState() == CommandBuffer::State::Ready);

  // Graphics is Ready again, so Write succeeds.
  ctx.Reset();
  REQUIRE(ctx.Write(buffer, Span<u8 const>(payload)));

  ctx.WaitInFlightOperations();
  dev.WaitIdle();
}

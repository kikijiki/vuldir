// Binder deferred descriptor reclaim: after RenderContext::Reset() calls
// BeginFrame, a destroyed resource's descriptor index must not return to the
// free list until that frame slot retires. Observed through the index given to
// newly created views (the free list is a LIFO stack, see VkBinder.cpp Heap).

#include "TestEnv.hpp"

#include "vuldir/Vuldir.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace vd;
using namespace vdtest;

namespace {

UPtr<Buffer> makeSrvBuffer(Device& dev, const char* name)
{
  return std::make_unique<Buffer>(
    dev, Buffer::Desc{
           .name        = name,
           .usage       = ResourceUsage::ShaderResource,
           .size        = 256u,
           .defaultView = ViewType::SRV,
           .memoryType  = MemoryType::Main});
}

u32 srvIndex(const Buffer& buf)
{
  return buf.GetView(ViewType::SRV)->binding.index;
}

} // namespace

TEST_CASE(
  "Binder defers descriptor reclaim until the owning frame slot retires",
  "[integration][binder]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  auto& dev = *env->device;

  // Reset() calls Binder::BeginFrame(slot), enabling deferred unbinding, and
  // retires anything still pending for that slot.
  RenderContext ctx(dev, RenderContext::Desc{.maxFramesInFlight = 2u});

  ctx.Reset(); // slot 0, BeginFrame(0): deferred unbinding is now active.

  auto bufA  = makeSrvBuffer(dev, "DeferredReclaim A");
  const auto idxA = srvIndex(*bufA);

  // Destroyed while slot 0's window is open: index must not be freed yet.
  bufA.reset();

  auto bufB      = makeSrvBuffer(dev, "DeferredReclaim B");
  const auto idxB = srvIndex(*bufB);
  REQUIRE(idxB != idxA); // idxA still held back.

  // Nothing was submitted, so there is no fence to wait on, but Reset()
  // still retires slot 0's pending unbinds, which frees idxA.
  ctx.Reset();

  auto bufC      = makeSrvBuffer(dev, "DeferredReclaim C");
  const auto idxC = srvIndex(*bufC);
  REQUIRE(idxC == idxA);

  // Repeat with several destroyed resources per window (no double free).
  for(u32 i = 0u; i < 4u; ++i) {
    auto d1 = makeSrvBuffer(dev, "Churn 1");
    auto d2 = makeSrvBuffer(dev, "Churn 2");
    auto d3 = makeSrvBuffer(dev, "Churn 3");
    d1.reset();
    d2.reset();
    d3.reset();
    ctx.Reset(); // same slot again: retires everything just deferred.
  }

  bufB.reset();
  bufC.reset();

  ctx.WaitInFlightOperations();
  dev.WaitIdle();
}

TEST_CASE(
  "Binder::FlushDeferred reclaims every pending slot", "[integration][binder]")
{
  auto* env = GetGpuEnv();
  if(!env) { SKIP("No GPU/display available"); }

  auto& dev = *env->device;

  RenderContext ctx(dev, RenderContext::Desc{.maxFramesInFlight = 2u});

  ctx.Reset(); // slot 0
  auto buf0      = makeSrvBuffer(dev, "FlushDeferred slot0");
  const auto idx0 = srvIndex(*buf0);
  buf0.reset(); // deferred into slot 0's pending list

  // Advance to slot 1 without retiring slot 0.
  dev.GetSwapchain().NextFrame();
  ctx.Reset(); // slot 1
  auto buf1      = makeSrvBuffer(dev, "FlushDeferred slot1");
  const auto idx1 = srvIndex(*buf1);
  buf1.reset(); // deferred into slot 1's pending list

  // Neither index is reusable yet. Keep the probes alive so their own
  // destruction is not deferred into the free list before the check below.
  auto probe0      = makeSrvBuffer(dev, "Probe 0");
  const auto pidx0 = srvIndex(*probe0);
  auto probe1      = makeSrvBuffer(dev, "Probe 1");
  const auto pidx1 = srvIndex(*probe1);
  REQUIRE(pidx0 != idx0);
  REQUIRE(pidx0 != idx1);
  REQUIRE(pidx1 != idx0);
  REQUIRE(pidx1 != idx1);

  // WaitInFlightOperations uses FlushDeferred, which must reclaim every
  // slot's pending unbinds, not just the current one.
  ctx.WaitInFlightOperations();

  auto after0      = makeSrvBuffer(dev, "After 0");
  const auto aidx0 = srvIndex(*after0);
  auto after1      = makeSrvBuffer(dev, "After 1");
  const auto aidx1 = srvIndex(*after1);

  // Both deferred indices are available again (order is unspecified).
  const bool sawBoth =
    (aidx0 == idx0 || aidx0 == idx1) && (aidx1 == idx0 || aidx1 == idx1) &&
    aidx0 != aidx1;
  REQUIRE(sawBoth);

  after0.reset();
  after1.reset();
  probe0.reset();
  probe1.reset();

  ctx.WaitInFlightOperations();
  dev.WaitIdle();
}

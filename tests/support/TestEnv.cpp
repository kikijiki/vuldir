#include "TestEnv.hpp"

#include "TestWindow.hpp"

#include <cstdio>

using namespace vd;

namespace vdtest {

namespace {
std::unique_ptr<GpuEnv> g_env;
} // namespace

bool InitGpuEnv()
{
  if(g_env) return true; // Already initialized (should not happen).

  if(!TestWindow::HasDisplay()) {
    std::fprintf(
      stderr,
#if defined(VD_WINDOW_WAYLAND)
      "[vuldir-tests] No Wayland compositor reachable "
      "(are $WAYLAND_DISPLAY and $XDG_RUNTIME_DIR set?) - skipping "
      "GPU-backed tests.\n");
#else
      "[vuldir-tests] No X display reachable "
      "(is $DISPLAY set?) - skipping GPU-backed tests.\n");
#endif
    return false;
  }

  try {
    auto env    = std::make_unique<GpuEnv>();
    env->window = std::make_unique<TestWindow>(64u, 64u);

    env->device = std::make_unique<Device>(
      Device::Desc{
        .appName     = "vuldir-tests",
        .dbgEnable   = true,
        .dbgLogLevel = LogLevel::Warning,
      },
      Swapchain::Desc{
        .window            = env->window->GetHandle(),
        .size              = UInt2{64u, 64u},
        .minImageCount     = 2u,
        .maxFramesInFlight = 2u,
      });

    g_env = std::move(env);
    return true;
  }
  catch(const std::exception& ex) {
    std::fprintf(
      stderr, "[vuldir-tests] GPU environment unavailable: %s\n",
      ex.what());
    return false;
  }
  catch(...) {
    std::fprintf(
      stderr,
      "[vuldir-tests] GPU environment unavailable: unknown error\n");
    return false;
  }
}

void ShutdownGpuEnv()
{
  if(!g_env) return;

  // Do not destroy a Device/Swapchain with work in flight.
  if(g_env->device) g_env->device->WaitIdle();

  // The Device (owns the Swapchain) must go before the window.
  g_env.reset();
}

GpuEnv* GetGpuEnv() { return g_env.get(); }

} // namespace vdtest

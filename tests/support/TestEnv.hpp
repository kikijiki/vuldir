#pragma once

#include "vuldir/Vuldir.hpp"

#include <memory>

namespace vdtest {

class TestWindow;

// Shared Device + Swapchain (tiny 64x64 window) for the whole test binary.
// A single instance keeps setup cost down and lets tests use several
// RenderContexts on one Device.
struct GpuEnv {
  std::unique_ptr<TestWindow> window;
  std::unique_ptr<vd::Device> device;
};

// GpuEnv is created and destroyed explicitly from main() (see TestMain.cpp),
// not through a static, to avoid exit-time teardown races with the Vulkan
// loader.

// Called once from main() before the Catch2 session. Returns false (after
// logging the reason) if no display or compatible Vulkan device is available;
// tests should SKIP() in that case.
bool InitGpuEnv();

// Called once from main() after the Catch2 session.
void ShutdownGpuEnv();

// Returns nullptr if InitGpuEnv() was never called or failed.
GpuEnv* GetGpuEnv();

} // namespace vdtest

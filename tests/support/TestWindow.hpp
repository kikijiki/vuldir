#pragma once

#include "vuldir/core/Core.hpp"

namespace vdtest {

// Minimal presentable window for Vulkan swapchain tests. No event loop;
// tests drive rendering explicitly at a fixed small size.
class TestWindow
{
public:
  TestWindow(vd::u32 width, vd::u32 height);
  ~TestWindow();

  VD_NONMOVABLE(TestWindow);

  const vd::WindowHandle& GetHandle() const { return m_handle; }

  vd::u32 GetWidth() const { return m_width; }
  vd::u32 GetHeight() const { return m_height; }

  // Whether a display server is reachable; tests SKIP when headless.
  static bool HasDisplay();

private:
  vd::u32          m_width    = 0u;
  vd::u32          m_height   = 0u;
  vd::WindowHandle m_handle;
#if defined(VD_WINDOW_WAYLAND)
  // Backend state (WlTestState*). Only declared for Wayland: an unused
  // private field breaks the XCB build under -Werror.
  void* m_platform = nullptr;
#endif
};

} // namespace vdtest

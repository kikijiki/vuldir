#pragma once

#include "vuldir/core/Definitions.hpp"
#include "vuldir/core/Platform.hpp"
#include "vuldir/core/STL.hpp"
#include "vuldir/core/Types.hpp"
#include "vuldir/core/Uti.hpp"

namespace vd {

struct WindowHandle {
#ifdef VD_OS_WINDOWS
  HINSTANCE hInstance = nullptr;
  HWND      hWnd      = nullptr;
#elif defined(VD_OS_LINUX) && defined(VD_WINDOW_WAYLAND)
  struct wl_display* display = nullptr;
  struct wl_surface* surface = nullptr;
#elif defined(VD_OS_LINUX) && defined(VD_WINDOW_XCB)
  xcb_connection_t* connection = nullptr;
  xcb_window_t      window     = {};
#endif
};

} // namespace vd

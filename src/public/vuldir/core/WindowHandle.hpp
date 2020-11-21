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
#elif VD_OS_LINUX
  xcb_connection_t* connection = nullptr;
  xcb_window_t      window     = {};
#endif
};

} // namespace vd

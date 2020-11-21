#pragma once

#include "vuldir/core/Definitions.hpp"

#ifdef VD_OS_WINDOWS

  #ifndef NOMINMAX
    #define NOMINMAX
  #endif

  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif

  #ifndef VC_EXTRALEAN
    #define VC_EXTRALEAN
  #endif

  #include <Windows.h>

  #undef CreateSemaphore

#endif

#ifdef VD_OS_LINUX
  #include <dlfcn.h>
  #include <xcb/xcb.h>
#endif

namespace Vuldir {}

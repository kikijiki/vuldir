#pragma once

#ifdef __clang__
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wreserved-id-macro"
  #pragma clang diagnostic ignored "-Wkeyword-macro"
#endif

#ifndef VD_LOG_LEVEL
  #if defined(DEBUG) || defined(_DEBUG)
    #define VD_LOG_LEVEL 4
  #else
    #define VD_LOG_LEVEL 1
  #endif
#endif

#ifdef VD_OS_WINDOWS
  #define VD_EXPORT extern "C" __declspec(dllexport)
#else
  #define VD_EXPORT
#endif

#if defined(VD_OS_WINDOWS) && defined(_DEBUG)
//  #define ENABLE_CRTDEBUG
#endif

#ifdef ENABLE_CRTDEBUG
  #define _CRTDBG_MAP_ALLOC
  #define _CRTDBG_MAP_ALLOC_NEW
  #include <crtdbg.h>
  #include <stdlib.h>
  #define DEBUG_NEW new(_NORMAL_BLOCK, __FILE__, __LINE__)
  #define new       DEBUG_NEW
#endif

#define VD_UNUSED(x) (void)(x)

#define VD_XSTR(s) VD_STR(s)
#define VD_STR(s)  #s

#define VD_XCONCAT(X, Y) X##Y
#define VD_CONCAT(X, Y)  VD_XCONCAT(X, Y)

#define VD_PARAM(T, N)      \
  auto& set_##N(T const& v) \
  {                         \
    N = v;                  \
    return *this;           \
  }                         \
  T N

#define VD_UNIQUE(X) VD_CONCAT(X, __COUNTER__)

#define VD_NONMOVABLE(X)           \
  X(const X&)            = delete; \
  X(X&&)                 = delete; \
  X& operator=(const X&) = delete; \
  X& operator=(X&&)      = delete;

#define VD_NONCOPYABLE(X)          \
  X(const X&)            = delete; \
  X& operator=(const X&) = delete;

#define VD_NONCOPYABLE_DEFAULTMOVE(X) \
  X(const X&)            = delete;    \
  X(X&&)                 = default;   \
  X& operator=(const X&) = delete;    \
  X& operator=(X&&)      = default;

#define VD_NONCOPYABLE_DEFAULTMOVE_DEFAULTCTOR(X) \
  X()                    = default;               \
  X(const X&)            = delete;                \
  X(X&&)                 = default;               \
  X& operator=(const X&) = delete;                \
  X& operator=(X&&)      = default;

#ifdef __clang__
  #pragma clang diagnostic pop
#endif

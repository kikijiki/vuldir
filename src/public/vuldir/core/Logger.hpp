//! \author Matteo Bernacchia <git@kikijiki.com>
//! \date   2017/04/20

#pragma once

#include "vuldir/core/Definitions.hpp"
#include "vuldir/core/Platform.hpp"
#include "vuldir/core/STL.hpp"
#include "vuldir/core/Types.hpp"
#include "vuldir/core/Uti.hpp"

namespace {
inline const char* GetFilename(const char* path)
{
  if(!path) { return path; }

  const size_t len = std::strlen(path);
  if(len == 0u) { return path; }

  for(size_t index = 0u; index < len - 1u; index++) {
    const size_t charIndex = len - index - 1u;
    const char   c         = path[charIndex];
    if(c == '\\' || c == '/') { return path + charIndex + 1u; }
  }

  return path;
}

inline void Print(const char* fmt, ...)
{
  const size_t bufferSize = 2048u;
  char         buffer[bufferSize]{};

  va_list argList;
  va_start(argList, fmt);
#ifdef VD_OS_WINDOWS
  const auto length =
    static_cast<size_t>(vsprintf_s(buffer, fmt, argList));
#else
  const auto length =
    static_cast<size_t>(vsprintf(buffer, fmt, argList));
#endif
  fwrite(buffer, 1u, length, stdout);
  fflush(stdout);
#ifdef VD_OS_WINDOWS
//  OutputDebugStringA(buffer);
#endif
  va_end(argList);
}

inline void PrintLn(const char* fmt, ...)
{
  const size_t bufferSize = 2048u;
  char         buffer[bufferSize]{};

  va_list argList;
  va_start(argList, fmt);
#ifdef VD_OS_WINDOWS
  auto length = static_cast<size_t>(vsprintf_s(buffer, fmt, argList));
#else
  auto length = static_cast<size_t>(vsprintf(buffer, fmt, argList));
#endif
  va_end(argList);

  if(length + 1u < bufferSize) {
    buffer[length + 0] = '\n';
    buffer[length + 1] = '\0';
    length++;
  }

  fwrite(buffer, 1u, length, stdout);
  fflush(stdout);
#ifdef VD_OS_WINDOWS
//  OutputDebugStringA(buffer);
#endif
}

} // namespace

namespace vd {
enum class LogLevel { None, Error, Warning, Info, Verbose };

#ifdef VD_OS_WINDOWS
  #define VD_LOG_COLOR_INFO()          \
    SetConsoleTextAttribute(           \
      GetStdHandle(STD_OUTPUT_HANDLE), \
      FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);

  #define VD_LOG_COLOR_WARNING()       \
    SetConsoleTextAttribute(           \
      GetStdHandle(STD_OUTPUT_HANDLE), \
      FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);

  #define VD_LOG_COLOR_ERROR()         \
    SetConsoleTextAttribute(           \
      GetStdHandle(STD_OUTPUT_HANDLE), \
      FOREGROUND_RED | FOREGROUND_INTENSITY);

#else
  #define VD_LOG_COLOR_INFO()
  #define VD_LOG_COLOR_WARNING()
  #define VD_LOG_COLOR_ERROR()
#endif

#if VD_LOG_LEVEL >= 4
  #define VDLogV(...)       \
    {                       \
      VD_LOG_COLOR_INFO();  \
      VDLogTag(VRB);        \
      PrintLn(__VA_ARGS__); \
    }
  #define VdCheckV(condition, ...)             \
    {                                          \
      if(!(condition)) { VDLogV(__VA_ARGS__) } \
    }
#else
  #define VDLogV(...)
  #define VdCheckV(condition, ...)
#endif

#define VDLogTag(TAG) Print("[" #TAG "]: ");
// #define VDLogTag(TAG) Print("[" #TAG "] %s(%.04u): ",
//  GetFilename(__FILE__), __LINE__);

#if VD_LOG_LEVEL >= 3
  #define VDLogI(...)       \
    {                       \
      VD_LOG_COLOR_INFO();  \
      VDLogTag(INF);        \
      PrintLn(__VA_ARGS__); \
    }

  #define VdCheckI(condition, ...)             \
    {                                          \
      if(!(condition)) { VDLogI(__VA_ARGS__) } \
    }
#else
  #define VDLogI(...)
  #define VdCheckI(condition, ...)
#endif

#if VD_LOG_LEVEL >= 2
  #define VDLogW(...)         \
    {                         \
      VD_LOG_COLOR_WARNING(); \
      VDLogTag(WRN);          \
      PrintLn(__VA_ARGS__);   \
    }

  #define VdCheckW(condition, ...)             \
    {                                          \
      if(!(condition)) { VDLogW(__VA_ARGS__) } \
    }
#else
  #define VDLogW(...)
  #define VdCheckW(condition, ...)
#endif

#if VD_LOG_LEVEL >= 1
  #define VDLogE(...)       \
    {                       \
      VD_LOG_COLOR_ERROR(); \
      VDLogTag(ERR);        \
      PrintLn(__VA_ARGS__); \
    }

  #define VDCheckE(condition, ...)             \
    {                                          \
      if(!(condition)) { VDLogE(__VA_ARGS__) } \
    }
#else
  #define VDLogE(...)
  #define VDCheckE(condition, ...)
#endif

#define VDPanic()                                    \
  {                                                  \
    Print("\n!!PANIC!! %s(%u)", __FILE__, __LINE__); \
    fflush(stdout);                                  \
    std::abort();                                    \
  }
#define VDPanicMsg(...) \
  {                     \
    Print(__VA_ARGS__); \
    VDPanic();          \
  }

#define VDAssert(condition)                      \
  if(!(condition)) {                             \
    Print("ASSERTION FAILED: %s\n", #condition); \
    VDPanic();                                   \
  }
#define VDAssertMsg(condition, ...)              \
  if(!(condition)) {                             \
    Print("ASSERTION FAILED: %s\n", #condition); \
    VDPanicMsg(__VA_ARGS__);                     \
  }

} // namespace vd

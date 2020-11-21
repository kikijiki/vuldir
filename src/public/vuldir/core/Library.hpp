//! \author Matteo Bernacchia <git@kikijiki.com>
//! \date   2017/04/20

#pragma once

#include "vuldir/core/Definitions.hpp"
#include "vuldir/core/Platform.hpp"
#include "vuldir/core/STL.hpp"
#include "vuldir/core/Types.hpp"
#include "vuldir/core/Uti.hpp"

namespace vd {

class Library
{
public:
  Library(const char* path = nullptr): m_module{nullptr} { Load(path); }

  ~Library() { Unload(); }

#ifdef VD_OS_WINDOWS
public:
  void Load(const char* path)
  {
    if(!path) return;

    Unload();
    m_module = ::LoadLibraryA(path);
  }

  void Unload()
  {
    if(!m_module) return;

    ::FreeLibrary(m_module);
    m_module = nullptr;
  }

  template<typename T>
  T GetFunction(const char* name)
  {
    return m_module ? (T)::GetProcAddress(m_module, name) : nullptr;
  }

private:
  HMODULE m_module;
#endif

#if defined(VD_OS_LINUX) || defined(VD_OS_ANDROID)
public:
  void Load(const char* path)
  {
    if(!path) return;

    Unload();
    m_module = ::dlopen(path, RTLD_LAZY);
  }

  void Unload()
  {
    if(!m_module) return;

    ::dlclose(m_module);
    m_module = nullptr;
  }

  template<typename T>
  T GetFunction(const char* name)
  {
    return m_module ? (T)::dlsym(m_module, name) : nullptr;
  }

private:
  void* m_module;
#endif
};
} // namespace vd

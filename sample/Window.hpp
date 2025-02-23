#pragma once

#include "vuldir/core/Core.hpp"

using namespace vd;

class Window
{
public:
  Window(const Str& title, const u32 width, const u32 height);
  ~Window();

public:
  void Run(const std::function<bool()>& main);

public:
  const vd::WindowHandle& GetHandle() const { return m_handle; }

  u32  GetWidth() const;
  u32  GetHeight() const;
  u32  GetContentWidth() const;
  u32  GetContentHeight() const;
  bool IsVisible() const;

public:
  std::function<void()>                OnResize;
  std::function<void(const Arr<Str>&)> OnFileDrop;

private:
#ifdef VD_OS_WINDOWS
  static LRESULT CALLBACK _WndProc(HWND, UINT, WPARAM, LPARAM);
#elif VD_OS_LINUX
  //
#endif

  vd::WindowHandle m_handle;
};

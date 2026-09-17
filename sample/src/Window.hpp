#pragma once

#include "Input.hpp"
#include "vuldir/core/Core.hpp"

using namespace vd;

class Window
{
public:
  Window(const Str& title, const u32 width, const u32 height);
  ~Window();

public:
  void Run(const std::function<bool()>& main);
  void SetTitle(const Str& title);

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

  // Input forwarding for the sample UI. Return true if consumed (e.g. mouse
  // over a panel) so Window skips resize grips.
  std::function<bool(i32 x, i32 y, i32 button, bool down)>
                                                    OnMouseButton;
  // Motion deltas use Cartesian signs: +X is right and +Y is up.
  std::function<bool(i32 x, i32 y, i32 dx, i32 dy)> OnMouseMove;
  std::function<bool(i32 x, i32 y, f32 delta)>      OnMouseWheel;
  std::function<bool(u32 keysym, bool down)>        OnKey;
  std::function<bool(u32 codepoint)>                OnTextInput;

private:
#ifdef VD_OS_WINDOWS
  static LRESULT CALLBACK _WndProc(HWND, UINT, WPARAM, LPARAM);
#elif defined(VD_OS_LINUX)
  // Backend .cpp files (WindowXcb / WindowWayland) reach these helpers
  // from protocol listeners; keep the XCB/Wayland types out of this header.
  friend struct WindowPlatformAccess;

  void setContentSize(u32 w, u32 h, bool notifyResize);
  vd::sample::MouseDeltaTracker& mouseDelta() { return m_mouseDelta; }

  u32   m_width         = 0;
  u32   m_height        = 0;
  u32   m_contentWidth  = 0;
  u32   m_contentHeight = 0;
  bool  m_running       = true;
  // Backend state (XcbState* or WaylandState*); kept opaque so this
  // header does not pull xcb/wayland protocol types.
  void* m_platform      = nullptr;
#endif

  vd::sample::MouseDeltaTracker m_mouseDelta;
  vd::WindowHandle              m_handle;
};

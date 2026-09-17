#if defined(VD_OS_LINUX) && defined(VD_WINDOW_WAYLAND)

#include "Window.hpp"

#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "xdg-shell-client-protocol.h"

struct WindowPlatformAccess {
  static void setContentSize(Window* w, u32 width, u32 height, bool notify)
  {
    w->setContentSize(width, height, notify);
  }

  static vd::sample::MouseDeltaTracker& mouseDelta(Window* w)
  {
    return w->mouseDelta();
  }
};

namespace {

constexpr i32 kResizeBorder = 8;

enum : int8_t {
  kResizeTopLeft     = 0,
  kResizeTop         = 1,
  kResizeTopRight    = 2,
  kResizeRight       = 3,
  kResizeBottomRight = 4,
  kResizeBottom      = 5,
  kResizeBottomLeft  = 6,
  kResizeLeft        = 7,
  kResizeNone        = -1,
};

static int resizeDirectionAt(i32 x, i32 y, i32 w, i32 h)
{
  const bool left   = x < kResizeBorder;
  const bool right  = x >= w - kResizeBorder;
  const bool top    = y < kResizeBorder;
  const bool bottom = y >= h - kResizeBorder;

  if(top && left) return kResizeTopLeft;
  if(top && right) return kResizeTopRight;
  if(bottom && left) return kResizeBottomLeft;
  if(bottom && right) return kResizeBottomRight;
  if(left) return kResizeLeft;
  if(right) return kResizeRight;
  if(top) return kResizeTop;
  if(bottom) return kResizeBottom;
  return kResizeNone;
}

static uint32_t xdgResizeEdge(int dir)
{
  switch(dir) {
    case kResizeTopLeft:
      return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
    case kResizeTop:
      return XDG_TOPLEVEL_RESIZE_EDGE_TOP;
    case kResizeTopRight:
      return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
    case kResizeRight:
      return XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
    case kResizeBottomRight:
      return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
    case kResizeBottom:
      return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
    case kResizeBottomLeft:
      return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
    case kResizeLeft:
      return XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
    default:
      return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
  }
}

struct WaylandState {
  Window* self = nullptr;

  wl_display*    display    = nullptr;
  wl_registry*   registry   = nullptr;
  wl_compositor* compositor = nullptr;
  xdg_wm_base*   wmBase     = nullptr;
  wl_shm*        shm        = nullptr;
  wl_seat*       seat       = nullptr;
  wl_pointer*    pointer    = nullptr;
  wl_keyboard*   keyboard   = nullptr;

  wl_surface*   surface    = nullptr;
  xdg_surface*  xdgSurface = nullptr;
  xdg_toplevel* toplevel   = nullptr;

  xkb_context* xkbCtx   = nullptr;
  xkb_keymap*  xkbKeymap = nullptr;
  xkb_state*   xkbState = nullptr;

  Str      title;
  bool     configured   = false;
  bool     running      = true;
  uint32_t seatCaps     = 0;
  uint32_t pointerSerial = 0;
  i32      pointerX     = 0;
  i32      pointerY     = 0;
};

// wl_pointer/wl_keyboard gained release requests at interface version 3.
// Destroying instead leaks the server-side object; the seat is bound at up
// to version 5, so pick per the version actually negotiated.
static void releasePointer(wl_pointer* pointer)
{
  if(!pointer) return;
  if(wl_pointer_get_version(pointer) >= WL_POINTER_RELEASE_SINCE_VERSION)
    wl_pointer_release(pointer);
  else
    wl_pointer_destroy(pointer);
}

static void releaseKeyboard(wl_keyboard* keyboard)
{
  if(!keyboard) return;
  if(
    wl_keyboard_get_version(keyboard) >=
    WL_KEYBOARD_RELEASE_SINCE_VERSION)
    wl_keyboard_release(keyboard);
  else
    wl_keyboard_destroy(keyboard);
}

static void ensureWaylandEnv()
{
  const char* display = std::getenv("WAYLAND_DISPLAY");
  if(!display || !*display)
    ::setenv("WAYLAND_DISPLAY", "wayland-1", 0);
}

static void registryGlobal(
  void* data, wl_registry* registry, uint32_t name,
  const char* interface, uint32_t version)
{
  auto* wl = static_cast<WaylandState*>(data);
  if(std::strcmp(interface, wl_compositor_interface.name) == 0) {
    wl->compositor = static_cast<wl_compositor*>(wl_registry_bind(
      registry, name, &wl_compositor_interface,
      version >= 4 ? 4 : version));
  } else if(std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
    wl->wmBase = static_cast<xdg_wm_base*>(wl_registry_bind(
      registry, name, &xdg_wm_base_interface,
      version >= 2 ? 2 : version));
  } else if(std::strcmp(interface, wl_shm_interface.name) == 0) {
    wl->shm = static_cast<wl_shm*>(
      wl_registry_bind(registry, name, &wl_shm_interface, 1));
  } else if(std::strcmp(interface, wl_seat_interface.name) == 0) {
    wl->seat = static_cast<wl_seat*>(wl_registry_bind(
      registry, name, &wl_seat_interface, version >= 5 ? 5 : version));
  }
}

static void registryGlobalRemove(void*, wl_registry*, uint32_t) {}

static const wl_registry_listener kRegistryListener = {
  .global        = registryGlobal,
  .global_remove = registryGlobalRemove,
};

static void wmBasePing(void*, xdg_wm_base* wmBase, uint32_t serial)
{
  xdg_wm_base_pong(wmBase, serial);
}

static const xdg_wm_base_listener kWmBaseListener = {
  .ping = wmBasePing,
};

static void xdgSurfaceConfigure(
  void* data, xdg_surface* xdgSurface, uint32_t serial)
{
  auto* wl = static_cast<WaylandState*>(data);
  xdg_surface_ack_configure(xdgSurface, serial);
  wl->configured = true;
}

static const xdg_surface_listener kXdgSurfaceListener = {
  .configure = xdgSurfaceConfigure,
};

static void toplevelConfigure(
  void* data, xdg_toplevel*, int32_t width, int32_t height, wl_array*)
{
  auto* wl = static_cast<WaylandState*>(data);
  if(width > 0 && height > 0) {
    WindowPlatformAccess::setContentSize(
      wl->self, static_cast<u32>(width), static_cast<u32>(height),
      wl->configured);
  }
}

static void toplevelClose(void* data, xdg_toplevel*)
{
  auto* wl = static_cast<WaylandState*>(data);
  wl->running = false;
}

static const xdg_toplevel_listener kToplevelListener = {
  .configure = toplevelConfigure,
  .close     = toplevelClose,
};

static void pointerEnter(
  void* data, wl_pointer*, uint32_t serial, wl_surface*, wl_fixed_t,
  wl_fixed_t)
{
  auto* wl           = static_cast<WaylandState*>(data);
  wl->pointerSerial  = serial;
}

static void pointerLeave(void*, wl_pointer*, uint32_t, wl_surface*) {}

static void pointerMotion(
  void* data, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y)
{
  auto* wl = static_cast<WaylandState*>(data);
  wl->pointerX = wl_fixed_to_int(x);
  wl->pointerY = wl_fixed_to_int(y);
  if(!wl->self->OnMouseMove) return;
  const auto delta =
    WindowPlatformAccess::mouseDelta(wl->self).Update(
      wl->pointerX, wl->pointerY);
  wl->self->OnMouseMove(
    wl->pointerX, wl->pointerY, delta[0], delta[1]);
}

static void pointerButton(
  void* data, wl_pointer*, uint32_t serial, uint32_t, uint32_t button,
  uint32_t state)
{
  auto* wl          = static_cast<WaylandState*>(data);
  wl->pointerSerial = serial;
  const bool down   = state == WL_POINTER_BUTTON_STATE_PRESSED;
  i32        btn    = -1;
  if(button == BTN_LEFT) btn = 0;
  else if(button == BTN_RIGHT) btn = 1;
  else if(button == BTN_MIDDLE) btn = 2;
  if(btn < 0) return;

  bool consumed = false;
  if(wl->self->OnMouseButton) {
    consumed = wl->self->OnMouseButton(
      wl->pointerX, wl->pointerY, btn, down);
  }
  if(!consumed && down && btn == 0 && wl->toplevel) {
    const int dir = resizeDirectionAt(
      wl->pointerX, wl->pointerY,
      static_cast<i32>(wl->self->GetContentWidth()),
      static_cast<i32>(wl->self->GetContentHeight()));
    if(dir >= 0) {
      xdg_toplevel_resize(
        wl->toplevel, wl->seat, serial, xdgResizeEdge(dir));
    }
  }
}

static void pointerAxis(
  void* data, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value)
{
  auto* wl = static_cast<WaylandState*>(data);
  if(!wl->self->OnMouseWheel) return;
  if(axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
  // Wayland: positive values are down; sample UI wants wheel-up positive.
  const f32 delta = -wl_fixed_to_double(value) / 10.0;
  wl->self->OnMouseWheel(wl->pointerX, wl->pointerY, delta);
}

static void pointerFrame(void*, wl_pointer*) {}
static void pointerAxisSource(void*, wl_pointer*, uint32_t) {}
static void pointerAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
static void pointerAxisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}

static const wl_pointer_listener kPointerListener = {
  .enter         = pointerEnter,
  .leave         = pointerLeave,
  .motion        = pointerMotion,
  .button        = pointerButton,
  .axis          = pointerAxis,
  .frame         = pointerFrame,
  .axis_source   = pointerAxisSource,
  .axis_stop     = pointerAxisStop,
  .axis_discrete = pointerAxisDiscrete,
};

static void keyboardKeymap(
  void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size)
{
  auto* wl = static_cast<WaylandState*>(data);
  if(format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
    close(fd);
    return;
  }
  char* mapStr = static_cast<char*>(
    mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
  close(fd);
  if(mapStr == MAP_FAILED) return;

  xkb_keymap* keymap = xkb_keymap_new_from_string(
    wl->xkbCtx, mapStr, XKB_KEYMAP_FORMAT_TEXT_V1,
    XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap(mapStr, size);
  if(!keymap) return;

  xkb_state* state = xkb_state_new(keymap);
  if(!state) {
    xkb_keymap_unref(keymap);
    return;
  }
  if(wl->xkbState) xkb_state_unref(wl->xkbState);
  if(wl->xkbKeymap) xkb_keymap_unref(wl->xkbKeymap);
  wl->xkbKeymap = keymap;
  wl->xkbState  = state;
}

static void keyboardEnter(
  void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*)
{
}

static void keyboardLeave(void*, wl_keyboard*, uint32_t, wl_surface*) {}

static void keyboardKey(
  void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t key,
  uint32_t state)
{
  auto* wl       = static_cast<WaylandState*>(data);
  const bool down = state == WL_KEYBOARD_KEY_STATE_PRESSED;
  // Wayland key codes are evdev codes; XKB expects +8.
  const xkb_keycode_t keycode = static_cast<xkb_keycode_t>(key + 8);
  if(wl->xkbState) {
    const xkb_keysym_t* syms = nullptr;
    const int n =
      xkb_state_key_get_syms(wl->xkbState, keycode, &syms);
    if(n > 0 && syms && wl->self->OnKey)
      wl->self->OnKey(static_cast<u32>(syms[0]), down);
    if(down && wl->self->OnTextInput) {
      char buf[8]{};
      const int len =
        xkb_state_key_get_utf8(wl->xkbState, keycode, buf, sizeof(buf));
      // xkb writes UTF-8 but RmlUi wants a codepoint: taking buf[0] fed it
      // the lead byte of every non-ASCII sequence.
      if(len > 0 && static_cast<size_t>(len) < sizeof(buf)) {
        try {
          u64        offset = 0u;
          const auto text = Strv{buf, static_cast<size_t>(len)};
          const u32  cp   = vd::decodeUtf8Codepoint(text, offset);
          if(cp >= 32u) wl->self->OnTextInput(cp);
        } catch(const std::exception& error) {
          // Drop the key rather than feed nonsense on.
          VDLogW("Undecodable key text ignored: %s", error.what());
        }
      }
    }
    xkb_state_update_key(
      wl->xkbState, keycode,
      down ? XKB_KEY_DOWN : XKB_KEY_UP);
  } else if(wl->self->OnKey) {
    wl->self->OnKey(key, down);
  }
}

static void keyboardModifiers(
  void* data, wl_keyboard*, uint32_t, uint32_t modsDepressed,
  uint32_t modsLatched, uint32_t modsLocked, uint32_t group)
{
  auto* wl = static_cast<WaylandState*>(data);
  if(wl->xkbState) {
    xkb_state_update_mask(
      wl->xkbState, modsDepressed, modsLatched, modsLocked, 0, 0,
      group);
  }
}

static void keyboardRepeatInfo(void*, wl_keyboard*, int32_t, int32_t) {}

static const wl_keyboard_listener kKeyboardListener = {
  .keymap      = keyboardKeymap,
  .enter       = keyboardEnter,
  .leave       = keyboardLeave,
  .key         = keyboardKey,
  .modifiers   = keyboardModifiers,
  .repeat_info = keyboardRepeatInfo,
};

static void seatCapabilities(void* data, wl_seat* seat, uint32_t caps)
{
  auto* wl = static_cast<WaylandState*>(data);
  wl->seatCaps = caps;
  if((caps & WL_SEAT_CAPABILITY_POINTER) && !wl->pointer) {
    wl->pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(wl->pointer, &kPointerListener, wl);
  }
  if(!(caps & WL_SEAT_CAPABILITY_POINTER) && wl->pointer) {
    releasePointer(wl->pointer);
    wl->pointer = nullptr;
  }
  if((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !wl->keyboard) {
    wl->keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(wl->keyboard, &kKeyboardListener, wl);
  }
  if(!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && wl->keyboard) {
    releaseKeyboard(wl->keyboard);
    wl->keyboard = nullptr;
  }
}

static void seatName(void*, wl_seat*, const char*) {}

static const wl_seat_listener kSeatListener = {
  .capabilities = seatCapabilities,
  .name         = seatName,
};

} // namespace

Window::Window(const Str& title, const u32 width, const u32 height)
{
  ensureWaylandEnv();

  m_width         = width;
  m_height        = height;
  m_contentWidth  = width;
  m_contentHeight = height;
  m_running       = true;

  auto* wl  = new WaylandState{};
  wl->self  = this;
  wl->title = title;
  m_platform      = wl;

  wl->display = wl_display_connect(nullptr);
  if(!wl->display) {
    const char* display = std::getenv("WAYLAND_DISPLAY");
    delete wl;
    m_platform = nullptr;
    throw std::runtime_error(
      std::string("wl_display_connect failed (WAYLAND_DISPLAY=") +
      ((display && *display) ? display : "<unset>") + ")");
  }

  wl->registry = wl_display_get_registry(wl->display);
  wl_registry_add_listener(wl->registry, &kRegistryListener, wl);
  wl_display_roundtrip(wl->display);

  if(!wl->compositor || !wl->wmBase) {
    wl_display_disconnect(wl->display);
    delete wl;
    m_platform = nullptr;
    throw std::runtime_error(
      "Wayland compositor or xdg_wm_base missing");
  }

  xdg_wm_base_add_listener(wl->wmBase, &kWmBaseListener, wl);
  if(wl->seat)
    wl_seat_add_listener(wl->seat, &kSeatListener, wl);

  wl->xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if(!wl->xkbCtx) {
    wl_display_disconnect(wl->display);
    delete wl;
    m_platform = nullptr;
    throw std::runtime_error("xkb_context_new failed");
  }

  wl->surface = wl_compositor_create_surface(wl->compositor);
  wl->xdgSurface =
    xdg_wm_base_get_xdg_surface(wl->wmBase, wl->surface);
  xdg_surface_add_listener(wl->xdgSurface, &kXdgSurfaceListener, wl);
  wl->toplevel = xdg_surface_get_toplevel(wl->xdgSurface);
  xdg_toplevel_add_listener(wl->toplevel, &kToplevelListener, wl);
  xdg_toplevel_set_title(wl->toplevel, wl->title.c_str());
  xdg_toplevel_set_app_id(wl->toplevel, "vuldir");
  xdg_toplevel_set_min_size(wl->toplevel, 64, 64);

  wl_surface_commit(wl->surface);

  // Wait for the first configure before presenting.
  while(!wl->configured) {
    if(wl_display_dispatch(wl->display) < 0) {
      wl_display_disconnect(wl->display);
      delete wl;
      m_platform = nullptr;
      throw std::runtime_error("Wayland dispatch failed during configure");
    }
  }

  // Commit again after ack so the surface is ready for Vulkan.
  wl_surface_commit(wl->surface);
  wl_display_flush(wl->display);

  m_handle.display = wl->display;
  m_handle.surface = wl->surface;
}

Window::~Window()
{
  auto* wl = static_cast<WaylandState*>(m_platform);
  if(!wl) return;

  releasePointer(wl->pointer);
  releaseKeyboard(wl->keyboard);
  if(wl->seat) wl_seat_destroy(wl->seat);
  if(wl->toplevel) xdg_toplevel_destroy(wl->toplevel);
  if(wl->xdgSurface) xdg_surface_destroy(wl->xdgSurface);
  if(wl->surface) wl_surface_destroy(wl->surface);
  if(wl->wmBase) xdg_wm_base_destroy(wl->wmBase);
  if(wl->compositor) wl_compositor_destroy(wl->compositor);
  if(wl->shm) wl_shm_destroy(wl->shm);
  if(wl->registry) wl_registry_destroy(wl->registry);
  if(wl->xkbState) xkb_state_unref(wl->xkbState);
  if(wl->xkbKeymap) xkb_keymap_unref(wl->xkbKeymap);
  if(wl->xkbCtx) xkb_context_unref(wl->xkbCtx);
  if(wl->display) wl_display_disconnect(wl->display);

  m_handle.display = nullptr;
  m_handle.surface = nullptr;
  delete wl;
  m_platform = nullptr;
}

void Window::setContentSize(u32 w, u32 h, bool notifyResize)
{
  const bool changed = m_contentWidth != w || m_contentHeight != h;
  m_width            = w;
  m_height           = h;
  m_contentWidth     = w;
  m_contentHeight    = h;
  if(changed && notifyResize && OnResize) OnResize();
}

void Window::SetTitle(const Str& title)
{
  auto* wl = static_cast<WaylandState*>(m_platform);
  if(!wl || !wl->toplevel) return;
  wl->title = title;
  xdg_toplevel_set_title(wl->toplevel, wl->title.c_str());
  wl_display_flush(wl->display);
}

void Window::Run(const std::function<bool()>& main)
{
  auto* wl = static_cast<WaylandState*>(m_platform);
  if(!wl || !wl->display) return;

  while(m_running && wl->running) {
    while(wl_display_prepare_read(wl->display) != 0) {
      if(wl_display_dispatch_pending(wl->display) < 0) {
        m_running = false;
        break;
      }
    }
    if(!m_running) break;

    wl_display_flush(wl->display);
    // On error the read is already cancelled and the connection is gone.
    // Dispatching anyway just spins the loop forever on a dead socket.
    if(wl_display_read_events(wl->display) < 0) {
      m_running = false;
      break;
    }
    if(wl_display_dispatch_pending(wl->display) < 0) {
      m_running = false;
      break;
    }

    if(!m_running || !wl->running) break;
    if(!main()) break;
  }
}

uint32_t Window::GetWidth() const { return m_width; }

uint32_t Window::GetHeight() const { return m_height; }

uint32_t Window::GetContentWidth() const { return m_contentWidth; }

uint32_t Window::GetContentHeight() const { return m_contentHeight; }

bool Window::IsVisible() const
{
  return m_contentWidth > 0u && m_contentHeight > 0u;
}

#endif // VD_OS_LINUX && VD_WINDOW_WAYLAND

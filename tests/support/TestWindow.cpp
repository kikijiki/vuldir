#include "TestWindow.hpp"

#include <cstdlib>

#if defined(VD_WINDOW_XCB)
  #include <xcb/xcb.h>
#elif defined(VD_WINDOW_WAYLAND)
  #include <cstring>
  #include <wayland-client.h>

  #include "xdg-shell-client-protocol.h"
#endif

using namespace vdtest;

#if defined(VD_WINDOW_XCB)

static void ensureDisplayEnv()
{
  const char* display = std::getenv("DISPLAY");
  if(!display || !*display)
    ::setenv("DISPLAY", ":0", 0);
}

bool TestWindow::HasDisplay()
{
  ensureDisplayEnv();

  int   screenNum  = 0;
  auto* connection = xcb_connect(nullptr, &screenNum);

  const bool ok =
    connection && xcb_connection_has_error(connection) == 0;

  if(connection) xcb_disconnect(connection);
  return ok;
}

TestWindow::TestWindow(vd::u32 width, vd::u32 height):
    m_width{width}, m_height{height}, m_handle{}
{
  ensureDisplayEnv();
  m_handle.connection = xcb_connect(nullptr, nullptr);

  if(!m_handle.connection || xcb_connection_has_error(m_handle.connection))
    throw std::runtime_error("TestWindow: xcb_connect failed");

  auto setup      = xcb_get_setup(m_handle.connection);
  auto screen     = xcb_setup_roots_iterator(setup).data;
  m_handle.window = xcb_generate_id(m_handle.connection);

  const uint32_t valueMask   = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
  const uint32_t valueList[] = {
    screen->white_pixel, XCB_EVENT_MASK_STRUCTURE_NOTIFY};

  xcb_create_window(
    m_handle.connection, screen->root_depth, m_handle.window,
    screen->root, 0, 0, static_cast<uint16_t>(width),
    static_cast<uint16_t>(height), 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
    screen->root_visual, valueMask, valueList);

  static const char title[] = "vuldir-tests";
  xcb_change_property(
    m_handle.connection, XCB_PROP_MODE_REPLACE, m_handle.window,
    XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, sizeof(title) - 1, title);

  // Map the window: some WMs report a zero current extent for unmapped
  // windows, which fails swapchain creation.
  xcb_map_window(m_handle.connection, m_handle.window);
  xcb_flush(m_handle.connection);
}

TestWindow::~TestWindow()
{
  if(m_handle.connection) {
    xcb_destroy_window(m_handle.connection, m_handle.window);
    xcb_disconnect(m_handle.connection);
  }
}

#elif defined(VD_WINDOW_WAYLAND)

namespace {

struct WlTestState {
  wl_display*    display    = nullptr;
  wl_registry*   registry   = nullptr;
  wl_compositor* compositor = nullptr;
  xdg_wm_base*   wmBase     = nullptr;
  wl_surface*    surface    = nullptr;
  xdg_surface*   xdgSurface = nullptr;
  xdg_toplevel*  toplevel   = nullptr;
  bool           configured = false;
  vd::u32        width      = 0;
  vd::u32        height     = 0;
};

void ensureWaylandEnv()
{
  const char* display = std::getenv("WAYLAND_DISPLAY");
  if(!display || !*display)
    ::setenv("WAYLAND_DISPLAY", "wayland-1", 0);
}

void registryGlobal(
  void* data, wl_registry* registry, uint32_t name,
  const char* interface, uint32_t version)
{
  auto* st = static_cast<WlTestState*>(data);
  if(std::strcmp(interface, wl_compositor_interface.name) == 0) {
    st->compositor = static_cast<wl_compositor*>(wl_registry_bind(
      registry, name, &wl_compositor_interface,
      version >= 4 ? 4 : version));
  } else if(std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
    st->wmBase = static_cast<xdg_wm_base*>(wl_registry_bind(
      registry, name, &xdg_wm_base_interface,
      version >= 2 ? 2 : version));
  }
}

void registryGlobalRemove(void*, wl_registry*, uint32_t) {}

const wl_registry_listener kRegistryListener = {
  .global        = registryGlobal,
  .global_remove = registryGlobalRemove,
};

void wmBasePing(void*, xdg_wm_base* wmBase, uint32_t serial)
{
  xdg_wm_base_pong(wmBase, serial);
}

const xdg_wm_base_listener kWmBaseListener = {.ping = wmBasePing};

void xdgSurfaceConfigure(
  void* data, xdg_surface* xdgSurface, uint32_t serial)
{
  auto* st = static_cast<WlTestState*>(data);
  xdg_surface_ack_configure(xdgSurface, serial);
  st->configured = true;
}

const xdg_surface_listener kXdgSurfaceListener = {
  .configure = xdgSurfaceConfigure,
};

void toplevelConfigure(
  void* data, xdg_toplevel*, int32_t width, int32_t height, wl_array*)
{
  auto* st = static_cast<WlTestState*>(data);
  if(width > 0) st->width = static_cast<vd::u32>(width);
  if(height > 0) st->height = static_cast<vd::u32>(height);
}

void toplevelClose(void*, xdg_toplevel*) {}

const xdg_toplevel_listener kToplevelListener = {
  .configure = toplevelConfigure,
  .close     = toplevelClose,
};

} // namespace

bool TestWindow::HasDisplay()
{
  ensureWaylandEnv();
  wl_display* display = wl_display_connect(nullptr);
  if(!display) return false;
  wl_display_disconnect(display);
  return true;
}

TestWindow::TestWindow(vd::u32 width, vd::u32 height):
    m_width{width}, m_height{height}, m_handle{}
{
  ensureWaylandEnv();

  auto* st   = new WlTestState{};
  st->width  = width;
  st->height = height;
  m_platform = st;

  st->display = wl_display_connect(nullptr);
  if(!st->display) {
    delete st;
    m_platform = nullptr;
    throw std::runtime_error("TestWindow: wl_display_connect failed");
  }

  st->registry = wl_display_get_registry(st->display);
  wl_registry_add_listener(st->registry, &kRegistryListener, st);
  wl_display_roundtrip(st->display);

  if(!st->compositor || !st->wmBase) {
    wl_display_disconnect(st->display);
    delete st;
    m_platform = nullptr;
    throw std::runtime_error(
      "TestWindow: Wayland compositor/xdg_wm_base missing");
  }

  xdg_wm_base_add_listener(st->wmBase, &kWmBaseListener, st);
  st->surface    = wl_compositor_create_surface(st->compositor);
  st->xdgSurface = xdg_wm_base_get_xdg_surface(st->wmBase, st->surface);
  xdg_surface_add_listener(st->xdgSurface, &kXdgSurfaceListener, st);
  st->toplevel = xdg_surface_get_toplevel(st->xdgSurface);
  xdg_toplevel_add_listener(st->toplevel, &kToplevelListener, st);
  xdg_toplevel_set_title(st->toplevel, "vuldir-tests");
  xdg_toplevel_set_app_id(st->toplevel, "vuldir-tests");
  xdg_toplevel_set_min_size(
    st->toplevel, static_cast<int32_t>(width),
    static_cast<int32_t>(height));

  wl_surface_commit(st->surface);
  while(!st->configured) {
    if(wl_display_dispatch(st->display) < 0) {
      wl_display_disconnect(st->display);
      delete st;
      m_platform = nullptr;
      throw std::runtime_error("TestWindow: Wayland configure failed");
    }
  }
  wl_surface_commit(st->surface);
  wl_display_flush(st->display);

  m_handle.display = st->display;
  m_handle.surface = st->surface;
  if(st->width) m_width = st->width;
  if(st->height) m_height = st->height;
}

TestWindow::~TestWindow()
{
  auto* st = static_cast<WlTestState*>(m_platform);
  if(!st) return;

  if(st->toplevel) xdg_toplevel_destroy(st->toplevel);
  if(st->xdgSurface) xdg_surface_destroy(st->xdgSurface);
  if(st->surface) wl_surface_destroy(st->surface);
  if(st->wmBase) xdg_wm_base_destroy(st->wmBase);
  if(st->compositor) wl_compositor_destroy(st->compositor);
  if(st->registry) wl_registry_destroy(st->registry);
  if(st->display) wl_display_disconnect(st->display);
  delete st;
  m_platform = nullptr;
  m_handle   = {};
}

#endif

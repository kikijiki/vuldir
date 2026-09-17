#if defined(VD_OS_LINUX) && defined(VD_WINDOW_XCB)

#include "Window.hpp"

#include <cstdlib>
#include <cstring>
#include <sys/select.h>
#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>

namespace {

struct XcbState {
  xcb_atom_t        wmDeleteWindow = 0;
  xcb_window_t      root           = 0;
  xcb_font_t        cursorFont     = 0;
  xcb_cursor_t      cursors[9]     = {};
  int               lastResizeDir  = -2; // -2 unset, -1 default, 0..7 edges
  xcb_key_symbols_t* keySymbols    = nullptr;
};

xcb_atom_t get_atom(xcb_connection_t* connection, const char* name)
{
  xcb_intern_atom_cookie_t cookie =
    xcb_intern_atom(connection, 0, strlen(name), name);
  xcb_intern_atom_reply_t* reply =
    xcb_intern_atom_reply(connection, cookie, nullptr);
  if(!reply) return 0;

  xcb_atom_t atom = reply->atom;
  free(reply);
  return atom;
}

// EWMH interactive resize directions (_NET_WM_MOVERESIZE). Used as a CSD-style
// fallback when the WM/compositor does not provide interactive resize borders
// (common on tiling Wayland compositors even when Motif SSD is requested).
enum : int8_t {
  kMoveResizeTopLeft     = 0,
  kMoveResizeTop         = 1,
  kMoveResizeTopRight    = 2,
  kMoveResizeRight       = 3,
  kMoveResizeBottomRight = 4,
  kMoveResizeBottom      = 5,
  kMoveResizeBottomLeft  = 6,
  kMoveResizeLeft        = 7,
  kMoveResizeNone        = -1,
};

enum : uint8_t {
  kMwmHintsFunctions   = 1u << 0,
  kMwmHintsDecorations = 1u << 1,
  kMwmFuncAll          = 1u << 0,
  kMwmDecorAll         = 1u << 0,
};

enum : uint8_t {
  kXcBottomLeftCorner  = 12,
  kXcBottomRightCorner = 14,
  kXcBottomSide        = 16,
  kXcLeftPtr           = 68,
  kXcLeftSide          = 70,
  kXcRightSide         = 96,
  kXcTopLeftCorner     = 134,
  kXcTopRightCorner    = 136,
  kXcTopSide           = 138,
};

constexpr i32 kResizeBorder = 8;

int resizeDirectionAt(i32 x, i32 y, i32 w, i32 h)
{
  const bool left   = x < kResizeBorder;
  const bool right  = x >= w - kResizeBorder;
  const bool top    = y < kResizeBorder;
  const bool bottom = y >= h - kResizeBorder;

  if(top && left) return kMoveResizeTopLeft;
  if(top && right) return kMoveResizeTopRight;
  if(bottom && left) return kMoveResizeBottomLeft;
  if(bottom && right) return kMoveResizeBottomRight;
  if(left) return kMoveResizeLeft;
  if(right) return kMoveResizeRight;
  if(top) return kMoveResizeTop;
  if(bottom) return kMoveResizeBottom;
  return kMoveResizeNone;
}

void sendNetWmMoveResize(
  xcb_connection_t* conn, xcb_window_t root, xcb_window_t window,
  i32 rootX, i32 rootY, int direction, i32 button)
{
  const xcb_atom_t atom = get_atom(conn, "_NET_WM_MOVERESIZE");
  if(atom == 0) return;

  xcb_client_message_event_t ev{};
  ev.response_type  = XCB_CLIENT_MESSAGE;
  ev.format         = 32;
  ev.window         = window;
  ev.type           = atom;
  ev.data.data32[0] = static_cast<uint32_t>(rootX);
  ev.data.data32[1] = static_cast<uint32_t>(rootY);
  ev.data.data32[2] = static_cast<uint32_t>(direction);
  ev.data.data32[3] = static_cast<uint32_t>(button);
  ev.data.data32[4] = 1; // source indication: normal application

  xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
  xcb_send_event(
    conn, 0, root,
    XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
      XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
    reinterpret_cast<const char*>(&ev));
  xcb_flush(conn);
}

xcb_cursor_t createGlyphCursor(
  xcb_connection_t* conn, xcb_font_t font, uint16_t glyph)
{
  const xcb_cursor_t cursor = xcb_generate_id(conn);
  xcb_create_glyph_cursor(
    conn, cursor, font, font, glyph, static_cast<uint16_t>(glyph + 1),
    0, 0, 0, 0xffff, 0xffff, 0xffff);
  return cursor;
}

} // namespace

void Window::setContentSize(u32 w, u32 h, bool notifyResize)
{
  const bool changed = m_contentWidth != w || m_contentHeight != h;
  m_width            = w;
  m_height           = h;
  m_contentWidth     = w;
  m_contentHeight    = h;
  if(changed && notifyResize && OnResize) OnResize();
}

Window::Window(const Str& title, const u32 width, const u32 height)
{
  m_width         = width;
  m_height        = height;
  m_contentWidth  = width;
  m_contentHeight = height;
  m_running       = true;

  auto* xcb = new XcbState{};
  m_platform = xcb;

  m_handle.connection = xcb_connect(nullptr, nullptr);

  if(const int xcbErr = xcb_connection_has_error(m_handle.connection)) {
    const char* display = std::getenv("DISPLAY");
    delete xcb;
    m_platform = nullptr;
    throw std::runtime_error(
      std::string("xcb_connect failed (error ") +
      std::to_string(xcbErr) + ", DISPLAY=" +
      ((display && *display) ? display : "<unset>") +
      "); need an X11 display (Xwayland is fine)");
  }

  auto setup      = xcb_get_setup(m_handle.connection);
  auto screen     = xcb_setup_roots_iterator(setup).data;
  m_handle.window = xcb_generate_id(m_handle.connection);

  xcb->root = screen->root;

  const uint32_t valueMask   = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
  const uint32_t valueList[] = {
    screen->white_pixel,
    XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY |
      XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_BUTTON_PRESS |
      XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION |
      XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
      XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW,
  };

  xcb_create_window(
    m_handle.connection, screen->root_depth, m_handle.window,
    screen->root, 0, 0, width, height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
    screen->root_visual, valueMask, valueList);

  xcb_change_property(
    m_handle.connection, XCB_PROP_MODE_REPLACE, m_handle.window,
    XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, title.size(), title.c_str());

  xcb_change_property(
    m_handle.connection, XCB_PROP_MODE_REPLACE, m_handle.window,
    XCB_ATOM_WM_ICON_NAME, XCB_ATOM_STRING, 8, title.size(),
    title.c_str());

  {
    const char wmClass[] = "vuldir\0Vuldir";
    xcb_change_property(
      m_handle.connection, XCB_PROP_MODE_REPLACE, m_handle.window,
      XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, sizeof(wmClass), wmClass);
  }

  xcb->wmDeleteWindow =
    get_atom(m_handle.connection, "WM_DELETE_WINDOW");
  xcb_atom_t wmProtocols =
    get_atom(m_handle.connection, "WM_PROTOCOLS");
  if(xcb->wmDeleteWindow != 0 && wmProtocols != 0) {
    xcb_change_property(
      m_handle.connection, XCB_PROP_MODE_REPLACE, m_handle.window,
      wmProtocols, XCB_ATOM_ATOM, 32, 1, &xcb->wmDeleteWindow);
  }

  {
    xcb_size_hints_t sizeHints{};
    xcb_icccm_size_hints_set_min_size(&sizeHints, 64, 64);
    xcb_icccm_size_hints_set_resize_inc(&sizeHints, 1, 1);
    xcb_icccm_set_wm_normal_hints(
      m_handle.connection, m_handle.window, &sizeHints);
  }

  xcb_ewmh_connection_t     ewmh{};
  xcb_intern_atom_cookie_t* ewmhCookies =
    xcb_ewmh_init_atoms(m_handle.connection, &ewmh);
  if(xcb_ewmh_init_atoms_replies(&ewmh, ewmhCookies, nullptr)) {
    xcb_atom_t windowType = ewmh._NET_WM_WINDOW_TYPE_NORMAL;
    xcb_ewmh_set_wm_window_type(&ewmh, m_handle.window, 1, &windowType);
  }

  {
    const xcb_atom_t motifHints =
      get_atom(m_handle.connection, "_MOTIF_WM_HINTS");
    if(motifHints != 0) {
      const uint32_t hints[5] = {
        kMwmHintsFunctions | kMwmHintsDecorations,
        kMwmFuncAll,
        kMwmDecorAll,
        0,
        0,
      };
      xcb_change_property(
        m_handle.connection, XCB_PROP_MODE_REPLACE, m_handle.window,
        motifHints, motifHints, 32, 5, hints);
    }
  }

  {
    xcb->cursorFont      = xcb_generate_id(m_handle.connection);
    const char* fontName = "cursor";
    xcb_open_font(
      m_handle.connection, xcb->cursorFont, strlen(fontName), fontName);

    static constexpr uint16_t kGlyphs[8] = {
      kXcTopLeftCorner,    kXcTopSide,           kXcTopRightCorner,
      kXcRightSide,        kXcBottomRightCorner, kXcBottomSide,
      kXcBottomLeftCorner, kXcLeftSide,
    };
    for(int i = 0; i < 8; ++i) {
      xcb->cursors[i] = createGlyphCursor(
        m_handle.connection, xcb->cursorFont, kGlyphs[i]);
    }
    xcb->cursors[8] =
      createGlyphCursor(m_handle.connection, xcb->cursorFont, kXcLeftPtr);
  }

  xcb_atom_t xdnd_aware  = get_atom(m_handle.connection, "XdndAware");
  const uint32_t version = 5;
  if(xdnd_aware != 0) {
    xcb_change_property(
      m_handle.connection, XCB_PROP_MODE_REPLACE, m_handle.window,
      xdnd_aware, XCB_ATOM_ATOM, 32, 1, &version);
  }

  xcb_map_window(m_handle.connection, m_handle.window);
  xcb_flush(m_handle.connection);

  xcb->keySymbols = xcb_key_symbols_alloc(m_handle.connection);

  {
    const int fd = xcb_get_file_descriptor(m_handle.connection);
    for(u32 i = 0; i < 50; ++i) {
      if(fd >= 0) {
        fd_set         fds;
        struct timeval tv{0, 20 * 1000};
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        select(fd + 1, &fds, nullptr, nullptr, &tv);
      }
      xcb_generic_event_t* event =
        xcb_poll_for_event(m_handle.connection);
      if(!event) continue;
      const auto type = event->response_type & ~0x80;
      if(type == XCB_CONFIGURE_NOTIFY) {
        auto* cfg = (xcb_configure_notify_event_t*)event;
        setContentSize(cfg->width, cfg->height, false);
        free(event);
        break;
      }
      free(event);
    }
  }
}

Window::~Window()
{
  auto* xcb = static_cast<XcbState*>(m_platform);
  if(m_handle.connection) {
    if(xcb && xcb->keySymbols) {
      xcb_key_symbols_free(xcb->keySymbols);
      xcb->keySymbols = nullptr;
    }
    if(xcb) {
      for(xcb_cursor_t cursor: xcb->cursors) {
        if(cursor) xcb_free_cursor(m_handle.connection, cursor);
      }
      if(xcb->cursorFont)
        xcb_close_font(m_handle.connection, xcb->cursorFont);
    }
    xcb_disconnect(m_handle.connection);
    m_handle.connection = nullptr;
  }
  delete xcb;
  m_platform = nullptr;
}

void Window::SetTitle(const Str& title)
{
  xcb_change_property(
    m_handle.connection, XCB_PROP_MODE_REPLACE, m_handle.window,
    XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
    static_cast<u32>(title.size()), title.c_str());
  xcb_change_property(
    m_handle.connection, XCB_PROP_MODE_REPLACE, m_handle.window,
    XCB_ATOM_WM_ICON_NAME, XCB_ATOM_STRING, 8,
    static_cast<u32>(title.size()), title.c_str());
  xcb_flush(m_handle.connection);
}

void Window::Run(const std::function<bool()>& main)
{
  auto* xcb = static_cast<XcbState*>(m_platform);
  if(!xcb) return;

  xcb_atom_t xdnd_drop = get_atom(m_handle.connection, "XdndDrop");
  xcb_atom_t xdnd_finished =
    get_atom(m_handle.connection, "XdndFinished");
  xcb_atom_t xdnd_selection =
    get_atom(m_handle.connection, "XdndSelection");
  xcb_atom_t text_uri_list =
    get_atom(m_handle.connection, "text/uri-list");
  xcb_atom_t wmProtocols =
    get_atom(m_handle.connection, "WM_PROTOCOLS");

  while(m_running) {
    xcb_generic_event_t* event;
    while((event = xcb_poll_for_event(m_handle.connection))) {
      switch(event->response_type & ~0x80) {
        case XCB_CLIENT_MESSAGE: {
          xcb_client_message_event_t* cm =
            (xcb_client_message_event_t*)event;

          if(
            cm->type == wmProtocols &&
            cm->data.data32[0] == xcb->wmDeleteWindow) {
            m_running = false;
            break;
          }

          if(
            cm->type == xdnd_drop && OnFileDrop &&
            xdnd_selection != 0) {
            xcb_atom_t selection_target = text_uri_list;
            xcb_atom_t property         = XCB_ATOM_PRIMARY;

            xcb_convert_selection(
              m_handle.connection, m_handle.window, xdnd_selection,
              selection_target, property, XCB_CURRENT_TIME);

            xcb_flush(m_handle.connection);

            xcb_generic_event_t* selection_event;
            while(
              (selection_event =
                 xcb_wait_for_event(m_handle.connection))) {
              if(
                (selection_event->response_type & ~0x80) ==
                XCB_SELECTION_NOTIFY) {
                xcb_get_property_cookie_t cookie = xcb_get_property(
                  m_handle.connection, 0, m_handle.window, property,
                  selection_target, 0, 4096);

                xcb_get_property_reply_t* reply =
                  xcb_get_property_reply(
                    m_handle.connection, cookie, nullptr);

                if(reply) {
                  Arr<Str> files;
                  char*    data = (char*)xcb_get_property_value(reply);
                  int length    = xcb_get_property_value_length(reply);

                  Str    uri_list(data, length);
                  size_t pos = 0;
                  while((pos = uri_list.find("\r\n")) != Str::npos) {
                    Str uri = uri_list.substr(0, pos);
                    if(uri.starts_with("file://")) {
                      Str path = uri.substr(7);
                      files.push_back(path);
                    }
                    uri_list = uri_list.substr(pos + 2);
                  }

                  if(xdnd_finished != 0) {
                    xcb_client_message_event_t finished;
                    memset(&finished, 0, sizeof(finished));
                    finished.response_type  = XCB_CLIENT_MESSAGE;
                    finished.window         = cm->data.data32[0];
                    finished.type           = xdnd_finished;
                    finished.format         = 32;
                    finished.data.data32[0] = m_handle.window;
                    finished.data.data32[1] = 1;

                    xcb_send_event(
                      m_handle.connection, 0, cm->data.data32[0],
                      XCB_EVENT_MASK_NO_EVENT, (char*)&finished);

                    xcb_flush(m_handle.connection);
                  }

                  if(!files.empty()) { OnFileDrop(files); }

                  free(reply);
                }

                free(selection_event);
                break;
              }
              free(selection_event);
            }
          }
          break;
        }
        case XCB_CONFIGURE_NOTIFY: {
          auto* cfg = (xcb_configure_notify_event_t*)event;
          setContentSize(cfg->width, cfg->height, true);
          break;
        }
        case XCB_BUTTON_PRESS: {
          auto* bp       = (xcb_button_press_event_t*)event;
          bool  consumed = false;
          if(bp->detail >= 4 && bp->detail <= 7) {
            if(OnMouseWheel) {
              const f32 delta =
                (bp->detail == 4 || bp->detail == 6) ? 1.f : -1.f;
              consumed = OnMouseWheel(bp->event_x, bp->event_y, delta);
            }
          } else if(OnMouseButton) {
            consumed = OnMouseButton(
              bp->event_x, bp->event_y,
              static_cast<i32>(bp->detail) - 1, true);
          }
          if(!consumed && bp->detail == 1) {
            const int dir = resizeDirectionAt(
              bp->event_x, bp->event_y,
              static_cast<i32>(m_contentWidth),
              static_cast<i32>(m_contentHeight));
            if(dir >= 0) {
              sendNetWmMoveResize(
                m_handle.connection, xcb->root, m_handle.window,
                bp->root_x, bp->root_y, dir, bp->detail);
            }
          }
          break;
        }
        case XCB_BUTTON_RELEASE: {
          auto* br = (xcb_button_release_event_t*)event;
          if(br->detail >= 1 && br->detail <= 3 && OnMouseButton) {
            OnMouseButton(
              br->event_x, br->event_y,
              static_cast<i32>(br->detail) - 1, false);
          }
          break;
        }
        case XCB_MOTION_NOTIFY: {
          auto*      mv = (xcb_motion_notify_event_t*)event;
          const auto delta =
            m_mouseDelta.Update(mv->event_x, mv->event_y);
          bool consumed = false;
          if(OnMouseMove) {
            consumed =
              OnMouseMove(mv->event_x, mv->event_y, delta[0], delta[1]);
          }
          if(!consumed) {
            const int dir = resizeDirectionAt(
              mv->event_x, mv->event_y,
              static_cast<i32>(m_contentWidth),
              static_cast<i32>(m_contentHeight));
            if(dir != xcb->lastResizeDir) {
              xcb->lastResizeDir = dir;
              const xcb_cursor_t cursor =
                (dir >= 0) ? xcb->cursors[dir] : xcb->cursors[8];
              if(cursor) {
                const uint32_t cursorVal = cursor;
                xcb_change_window_attributes(
                  m_handle.connection, m_handle.window, XCB_CW_CURSOR,
                  &cursorVal);
                xcb_flush(m_handle.connection);
              }
            }
          }
          break;
        }
        case XCB_KEY_PRESS:
        case XCB_KEY_RELEASE: {
          auto*      ke = (xcb_key_press_event_t*)event;
          const bool down =
            (event->response_type & ~0x80) == XCB_KEY_PRESS;
          xcb_keysym_t keysym = 0;
          if(xcb->keySymbols) {
            keysym =
              xcb_key_press_lookup_keysym(xcb->keySymbols, ke, 0);
          }
          if(OnKey) { OnKey(static_cast<u32>(keysym), down); }
          if(down && OnTextInput && keysym >= 32 && keysym < 127) {
            OnTextInput(static_cast<u32>(keysym));
          }
          break;
        }
        case XCB_EXPOSE:
          xcb_flush(m_handle.connection);
          break;
        default:
          break;
      }
      free(event);
    }

    if(!m_running) break;
    if(!main()) { break; }
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

#endif // VD_OS_LINUX && VD_WINDOW_XCB

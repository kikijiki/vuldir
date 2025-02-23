#include "Window.hpp"

#include <shellapi.h>

///////////////////////////////////////////////////////////////////////
#ifdef VD_OS_WINDOWS
///////////////////////////////////////////////////////////////////////

Window::Window(const Str& title, const u32 width, const u32 height)
{
  m_handle.hInstance = GetModuleHandle(nullptr);

  WNDCLASSEX wcex{};

  wcex.cbSize        = sizeof(WNDCLASSEX);
  wcex.style         = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc   = _WndProc;
  wcex.cbClsExtra    = 0;
  wcex.cbWndExtra    = 0;
  wcex.hInstance     = m_handle.hInstance;
  wcex.hIcon         = LoadIcon(m_handle.hInstance, IDI_APPLICATION);
  wcex.hCursor       = LoadCursor(nullptr, IDC_ARROW);
  wcex.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
  wcex.lpszMenuName  = nullptr;
  wcex.lpszClassName = "VuldirSampleClass";
  wcex.hIconSm       = LoadIcon(m_handle.hInstance, IDI_APPLICATION);

  if(!::RegisterClassExA(&wcex)) {
    throw std::runtime_error("RegisterClassExA failed");
  }

  m_handle.hWnd = CreateWindowExA(
    0L, wcex.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
    CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr, nullptr,
    m_handle.hInstance, nullptr);

  if(m_handle.hWnd == nullptr) {
    throw std::runtime_error("CreateWindowExA failed");
  }

  SetPropA(m_handle.hWnd, "VuldirWindow", this);

  // Enable drag and drop
  DragAcceptFiles(m_handle.hWnd, TRUE);

  ShowWindow(m_handle.hWnd, SW_SHOW);
  UpdateWindow(m_handle.hWnd);
}

Window::~Window() {}

void Window::Run(const std::function<bool()>& main)
{
  MSG msg{};
  while(msg.message != WM_QUIT) {
    if(PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      // Translate message
      TranslateMessage(&msg);

      // Dispatch message
      DispatchMessage(&msg);
    } else {
      if(!main()) { break; }
    }
  }
}

uint32_t Window::GetWidth() const
{
  RECT rect;
  GetWindowRect(m_handle.hWnd, &rect);
  return rect.right - rect.left;
}

uint32_t Window::GetHeight() const
{
  RECT rect;
  GetWindowRect(m_handle.hWnd, &rect);
  return rect.bottom - rect.top;
}

uint32_t Window::GetContentWidth() const
{
  RECT rect;
  GetClientRect(m_handle.hWnd, &rect);
  return rect.right - rect.left;
}

uint32_t Window::GetContentHeight() const
{
  RECT rect;
  GetClientRect(m_handle.hWnd, &rect);
  return rect.bottom - rect.top;
}

bool Window::IsVisible() const
{
  return !IsIconic(m_handle.hWnd) && IsWindowVisible(m_handle.hWnd) &&
         GetWidth() > 0u && GetHeight() > 0u;
}

LRESULT CALLBACK
Window::_WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  auto* wnd = reinterpret_cast<Window*>(GetPropA(hWnd, "VuldirWindow"));

  switch(message) {
    case WM_DESTROY:
      PostQuitMessage(0);
      break;
    case WM_EXITSIZEMOVE:
      if(wnd && wnd->OnResize) wnd->OnResize();
      break;
    case WM_DROPFILES: {
      if(wnd && wnd->OnFileDrop) {
        HDROP hDrop     = reinterpret_cast<HDROP>(wParam);
        UINT  fileCount = DragQueryFileA(hDrop, 0xFFFFFFFF, nullptr, 0);
        Arr<Str> files;
        files.reserve(fileCount);

        char filepath[MAX_PATH];
        for(UINT i = 0; i < fileCount; i++) {
          if(DragQueryFileA(hDrop, i, filepath, MAX_PATH) != 0) {
            files.emplace_back(filepath);
          }
        }

        DragFinish(hDrop);
        wnd->OnFileDrop(files);
      }
      break;
    }
    default:
      return DefWindowProc(hWnd, message, wParam, lParam);
  }

  return 0;
}
///////////////////////////////////////////////////////////////////////
#endif
///////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////
#ifdef VD_OS_LINUX
///////////////////////////////////////////////////////////////////////

Window::Window(const Str& title, const u32 width, const u32 height)
{
  m_handle.connection = xcb_connect(nullptr, nullptr);

  if(xcb_connection_has_error(m_handle.connection))
    throw std::runtime_error("xcb_connection_has_error failed");

  auto setup      = xcb_get_setup(m_handle.connection);
  auto screen     = xcb_setup_roots_iterator(setup).data;
  m_handle.window = xcb_generate_id(m_handle.connection);

  xcb_create_window(
    m_handle.connection, screen->root_depth, m_handle.window,
    screen->root, 0, 0, width, height, 1, XCB_WINDOW_CLASS_INPUT_OUTPUT,
    screen->root_visual, XCB_CW_BACK_PIXEL, &screen->white_pixel);

  xcb_change_property(
    m_handle.connection, XCB_PROP_MODE_REPLACE, m_handle.window,
    XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, title.size(), title.c_str());

  xcb_change_property(
    m_handle.connection, XCB_PROP_MODE_REPLACE, m_handle.window,
    XCB_ATOM_WM_ICON_NAME, XCB_ATOM_STRING, 8, title.size(),
    title.c_str());

  // Set up XDnD (X11 Drag and Drop)
  xcb_atom_t atoms[] = {XCB_ATOM_XdndAware,    XCB_ATOM_XdndEnter,
                        XCB_ATOM_XdndPosition, XCB_ATOM_XdndStatus,
                        XCB_ATOM_XdndDrop,     XCB_ATOM_XdndLeave,
                        XCB_ATOM_XdndFinished};

  // Register window as XDnD aware (version 5)
  const uint32_t version = 5;
  xcb_change_property(
    m_handle.connection, XCB_PROP_MODE_REPLACE, m_handle.window,
    XCB_ATOM_XdndAware, XCB_ATOM_ATOM, 32, 1, &version);

  xcb_map_window(m_handle.connection, m_handle.window);
  xcb_flush(m_handle.connection);
}

Window::~Window() { xcb_disconnect(m_handle.connection); }

void Window::Run(const std::function<bool()>& main)
{
  for(;;) {
    xcb_generic_event_t* event;
    while((event = xcb_poll_for_event(m_handle.connection))) {
      switch(event->response_type & ~0x80) {
        case XCB_CLIENT_MESSAGE: {
          xcb_client_message_event_t* cm =
            (xcb_client_message_event_t*)event;
          if(cm->type == XCB_ATOM_XdndDrop && OnFileDrop) {
            xcb_atom_t selection = XCB_ATOM_PRIMARY;
            xcb_atom_t targets =
              4; // Typically text/uri-list for drag and drop

            // Send selection request
            xcb_convert_selection(
              m_handle.connection, m_handle.window,
              XCB_ATOM_XdndSelection, targets, selection,
              XCB_CURRENT_TIME);

            xcb_flush(m_handle.connection);

            // Wait for selection notify event
            xcb_generic_event_t* selection_event;
            while(
              (selection_event =
                 xcb_wait_for_event(m_handle.connection))) {
              if(
                (selection_event->response_type & ~0x80) ==
                XCB_SELECTION_NOTIFY) {
                xcb_selection_notify_event_t* notify =
                  (xcb_selection_notify_event_t*)selection_event;

                // Get the selection data
                xcb_get_property_cookie_t cookie = xcb_get_property(
                  m_handle.connection, 0, m_handle.window, selection,
                  targets, 0,
                  4096 // Maximum size to read
                );

                xcb_get_property_reply_t* reply =
                  xcb_get_property_reply(
                    m_handle.connection, cookie, nullptr);

                if(reply) {
                  Arr<Str> files;
                  char*    data = (char*)xcb_get_property_value(reply);
                  int length    = xcb_get_property_value_length(reply);

                  // Parse URI list (format: file:///path\r\n)
                  Str    uri_list(data, length);
                  size_t pos = 0;
                  while((pos = uri_list.find("\r\n")) != Str::npos) {
                    Str uri = uri_list.substr(0, pos);
                    if(uri.starts_with("file://")) {
                      // Convert URI to local path
                      Str path = uri.substr(7);
                      files.push_back(path);
                    }
                    uri_list = uri_list.substr(pos + 2);
                  }

                  // Send finished message
                  xcb_client_message_event_t finished;
                  memset(&finished, 0, sizeof(finished));
                  finished.response_type = XCB_CLIENT_MESSAGE;
                  finished.window = cm->data.data32[0]; // Source window
                  finished.type   = XCB_ATOM_XdndFinished;
                  finished.format = 32;
                  finished.data.data32[0] = m_handle.window;
                  finished.data.data32[1] = 1; // Success

                  xcb_send_event(
                    m_handle.connection, 0, cm->data.data32[0],
                    XCB_EVENT_MASK_NO_EVENT, (char*)&finished);

                  xcb_flush(m_handle.connection);

                  // Call the callback with collected files
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
        case XCB_EXPOSE:
          xcb_flush(m_handle.connection);
          break;
        default:
          break;
      }
      free(event);
    }

    if(!main()) { break; }
  }
}

uint32_t Window::GetWidth() const { return 0; }

uint32_t Window::GetHeight() const { return 0; }

uint32_t Window::GetContentWidth() const { return 0; }

uint32_t Window::GetContentHeight() const { return 0; }

bool Window::IsVisible() const { return false; }
///////////////////////////////////////////////////////////////////////
#endif
///////////////////////////////////////////////////////////////////////

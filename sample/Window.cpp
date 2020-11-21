#include "Window.hpp"

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
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
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

  xcb_map_window(m_handle.connection, m_handle.window);
  xcb_flush(m_handle.connection);
}

Window::~Window() { xcb_disconnect(m_handle.connection); }

void Window::Run(const std::function<bool()>& main)
{
  for(;;) {
    xcb_generic_event_t* event;
    while((event = xcb_poll_for_event(m_handle.connection))) {
      switch(event->response_type) {
        case XCB_EXPOSE:
          xcb_flush(m_handle.connection);
          break;
        case XCB_CLIENT_MESSAGE:
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

#include "Window.hpp"

#ifdef VD_OS_WINDOWS

  #include <shellapi.h>
  #include <windowsx.h>

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
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    } else {
      if(!main()) { break; }
    }
  }
}

void Window::SetTitle(const Str& title)
{
  SetWindowTextA(m_handle.hWnd, title.c_str());
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
    case WM_SIZE:
      if(wnd && wnd->OnResize && wParam != SIZE_MINIMIZED) {
        wnd->OnResize();
      }
      break;
    case WM_MOUSEMOVE:
      if(wnd && wnd->OnMouseMove) {
        const i32  x     = GET_X_LPARAM(lParam);
        const i32  y     = GET_Y_LPARAM(lParam);
        const auto delta = wnd->m_mouseDelta.Update(x, y);
        wnd->OnMouseMove(x, y, delta[0], delta[1]);
      }
      break;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
      if(wnd && wnd->OnMouseButton) {
        const bool down = message == WM_LBUTTONDOWN ||
                          message == WM_RBUTTONDOWN ||
                          message == WM_MBUTTONDOWN;
        i32 button = 0;
        if(message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)
          button = 0;
        else if(message == WM_RBUTTONDOWN || message == WM_RBUTTONUP)
          button = 1;
        else
          button = 2;
        wnd->OnMouseButton(
          GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), button, down);
      }
      break;
    case WM_MOUSEWHEEL:
      if(wnd && wnd->OnMouseWheel) {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hWnd, &pt);
        wnd->OnMouseWheel(
          pt.x, pt.y,
          static_cast<f32>(GET_WHEEL_DELTA_WPARAM(wParam)) /
            static_cast<f32>(WHEEL_DELTA));
      }
      break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
      if(wnd && wnd->OnKey) {
        wnd->OnKey(
          static_cast<u32>(wParam),
          message == WM_KEYDOWN || message == WM_SYSKEYDOWN);
      }
      break;
    case WM_CHAR:
      if(wnd && wnd->OnTextInput && wParam >= 32) {
        wnd->OnTextInput(static_cast<u32>(wParam));
      }
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
#endif

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdexcept>
#include "WaitableSwapchainApp.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"

const int WINDOW_WIDTH = 1280;
const int WINDOW_HEIGHT = 720;

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
  D3D12AppBase* pApp = reinterpret_cast<D3D12AppBase*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
  static POINT lastPoint;

  if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wp, lp))
    return TRUE;

  auto io = ImGui::GetIO();

  switch (msg)
  {
  case WM_PAINT:
    if (pApp)
    {
      pApp->Render();
    }
    return 0;

  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;

  case WM_SIZE:
    if (pApp)
    {
      RECT rect{};
      GetClientRect(hWnd, &rect);
      bool isMinimized = wp == SIZE_MINIMIZED;
      pApp->OnSizeChanged(rect.right - rect.left, rect.bottom - rect.top, isMinimized);
    }
    return 0;

  case WM_SYSKEYDOWN:
    if ((wp == VK_RETURN) && (lp & (1 << 29)))
    {
      if (pApp) {
        pApp->ToggleFullscreen();
      }
    }
    break;

  case WM_LBUTTONDOWN:
    // breakthru
  case WM_RBUTTONDOWN:
    // breakthru
  case WM_MBUTTONDOWN:
    if (pApp && !io.WantCaptureMouse)
    {
      UINT buttonType = 0;
      if (msg == WM_LBUTTONDOWN) { buttonType = 0; }
      if (msg == WM_RBUTTONDOWN) { buttonType = 1; }
      if (msg == WM_MBUTTONDOWN) { buttonType = 2; }
      pApp->OnMouseButtonDown(buttonType);

      GetCursorPos(&lastPoint);
      ScreenToClient(hWnd, &lastPoint);
      SetCapture(hWnd);
    }
    break;
  
  case WM_LBUTTONUP:
    // breakthru
  case WM_RBUTTONUP:
    // breakthru
  case WM_MBUTTONUP:
    if (pApp && !io.WantCaptureMouse)
    {
      UINT buttonType = 0;
      if (msg == WM_LBUTTONDOWN) { buttonType = 0; }
      if (msg == WM_MBUTTONDOWN) { buttonType = 1; }
      if (msg == WM_MBUTTONDOWN) { buttonType = 2; }

      ReleaseCapture();
      pApp->OnMouseButtonUp(buttonType);
    }
    break;

  case WM_MOUSEMOVE:
    if (pApp && !io.WantCaptureMouse)
    {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hWnd, &pt);
        int dx = pt.x - lastPoint.x;
        int dy = pt.y - lastPoint.y;
        
        pApp->OnMouseMove(msg, dx, dy);
        lastPoint = pt;
    }
    break;
  }
  return DefWindowProc(hWnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
  WaitableSwapchainApp theApp{};

  CoInitializeEx(NULL, COINIT_MULTITHREADED);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  WNDCLASSEX wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.lpszClassName = L"D3D12Book4";
  RegisterClassEx(&wc);

  DWORD dwStyle = WS_OVERLAPPEDWINDOW;// &~WS_SIZEBOX;
  RECT rect = { 0,0, WINDOW_WIDTH, WINDOW_HEIGHT };
  AdjustWindowRect(&rect, dwStyle, FALSE);

  auto hwnd = CreateWindow(wc.lpszClassName, L"D3D12Book4",
    dwStyle,
    CW_USEDEFAULT, CW_USEDEFAULT,
    rect.right - rect.left, rect.bottom - rect.top,
    nullptr,
    nullptr,
    hInstance,
    &theApp
  );

  ImGui_ImplWin32_Init(hwnd);


  try
  {
    const bool useWaitableSwapchain = true;
    theApp.Initialize(hwnd, DXGI_FORMAT_R8G8B8A8_UNORM, false, useWaitableSwapchain);

    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&theApp));
    ShowWindow(hwnd, nCmdShow);

    MSG msg{};
    while (msg.message != WM_QUIT)
    {
      if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
      {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
    }

    theApp.Terminate();
    return static_cast<int>(msg.wParam);
  }
  catch (std::runtime_error e)
  {
    OutputDebugStringA(e.what());
    OutputDebugStringA("\n");
  }
  return 0;
}

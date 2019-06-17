
#include <windows.h>
#include <gdiplus.h>

#include <iostream>

#include "defs.h"
#include "shared_picture.h"
#include "shared_memory.h"

#pragma comment(lib, "gdiplus.lib")

SharedPicture g_picture{ kDefaultWindowSize };
SharedMemory g_memory{ kSharedFileNameOnDisk, kMappedFileName, kMaxSizeOfMappedFile };
HWND g_hwnd{};

void LoadSharedPictureFromSharedMemory() {
  try {
    if (not g_memory.IsOpened()) {
      g_memory.Open();
    }
    if (not g_memory.IsMapped()) {
      g_memory.MapViewBuffer();
    }
    char * buffer{ const_cast<char *>(g_memory.GetViewBuffer()) };
    membuf sbuf(buffer, buffer + g_memory.GetMappedSize());
    std::istream in(&sbuf);
    VectorPicture v;
    in >> v;
    g_picture.SetVectorPicture(v);
    g_picture.Rasterize();
    std::cout << g_picture.vector_.current_point << std::endl;
  }
  catch (SharedMemory::errors::General& e) {
    std::cout << "shared memory failed, " << e.what() << std::endl;
  }
}

void DrawCurrentThreadWindow(HDC& hdc, PAINTSTRUCT& ps) {
  hdc = BeginPaint(g_hwnd, &ps);
  Gdiplus::Graphics graphics(hdc);
  g_picture.DrawRasterPicture(graphics);
  EndPaint(g_hwnd, &ps);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  HDC hdc;
  PAINTSTRUCT ps;
  switch (uMsg) {

  case WM_LBUTTONDOWN:
    LoadSharedPictureFromSharedMemory();
    InvalidateRect(g_hwnd, NULL, false);
    break;

  case WM_RBUTTONDOWN:
    g_picture.Clear();
    g_picture.Rasterize();
    InvalidateRect(g_hwnd, NULL, false);
    break;

  case WM_PAINT:
    DrawCurrentThreadWindow(hdc, ps);
    return 0;

  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;

  default:
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }
  return 0;
}

//Gdiplus::Bitmap b;



int main() {
  Gdiplus::GdiplusStartupInput gdiplusStartupInput;
  ULONG_PTR gdiplusToken;
  Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

  POINT position{ 0, 0 };
  g_hwnd = CreateWindowForThread(
    GetModuleHandle(NULL), WndProc, "another_process_class_name",
    "Another process", position, POINT{300, 300});

  MSG msg{};
  while (GetMessage(&msg, NULL, 0, 0)) {
    //if (PeekMessage(&msg, NULL, 0, 0, true)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
    //}
    //std::this_thread::yield();
  }
  Gdiplus::GdiplusShutdown(gdiplusToken);
}


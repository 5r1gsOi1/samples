
#include <windows.h>
#include <gdiplus.h>

#include <iostream>

#include "defs.h"
#include "shared_memory.h"

#pragma comment(lib, "gdiplus.lib")



LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  HDC hdc;
  PAINTSTRUCT ps;
  switch (uMsg) {

  case WM_LBUTTONDOWN:
    // load picture
    //ClearPicture();
    //UpdateBitmap();
    //InvalidateRectOfAllThreads(all_threads_hwnd, nullptr, false);
    break;

  case WM_RBUTTONDOWN:
    // clear picture
    //ClearPicture();
    //UpdateBitmap();
    //InvalidateRectOfAllThreads(all_threads_hwnd, nullptr, false);
    break;

  case WM_PAINT:
    //DrawCurrentThreadWindow(hdc, ps);
    return 0;

  case WM_DESTROY:
   // is_running = false;
    return 0;

  default:
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }
  return 0;
}

//Gdiplus::Bitmap b;



int main() {

  SharedMemory memory{ kSharedFileNameOnDisk, kMappedFileName, kMaxSizeOfMappedFile };
  try {
    memory.Open();
    memory.MapViewBuffer();
    auto buffer{ memory.GetViewBuffer() };
    int x{ 0 };
    memcpy(&x, (void*)buffer, sizeof(x));
    std::cout << x << std::endl;
  } catch (SharedMemory::errors::General& e) {
    std::cout << "shared memory failed, " << e.what() << std::endl;
  }



  POINT position{ 0, 0 };
  HWND hwnd = CreateWindowForThread(
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
}


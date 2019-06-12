
#pragma once

#include <windows.h>
#include <string>

inline const POINT kDefaultWindowSize{ 300, 300 };
constexpr int kMaxSizeOfMappedFile{ 1'000'000 };
const std::string kNameOfMappedFile{ "Local\\TestFileMappingObject1236" };

template <typename WNDPROC_function>
HWND CreateWindowForThread(HINSTANCE hInstance, WNDPROC_function WindowProc,
  const std::string& window_class_name,
  const std::string& title, const POINT& position,
  const POINT& size) {
  WNDCLASS wc{};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = hInstance;
  wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = window_class_name.c_str();
  RegisterClass(&wc);

  HWND hwnd = CreateWindowEx(0,  // Optional window styles.
    window_class_name.c_str(),  // Window class
    title.c_str(),              // Window text
    WS_OVERLAPPEDWINDOW | CS_OWNDC,        // Window style

    position.x, position.y, size.x, size.y,

    NULL,       // Parent window
    NULL,       // Menu
    hInstance,  // Instance handle
    NULL        // Additional application data
  );

  if (hwnd == NULL) {
    return 0;
  }

  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
  return hwnd;
}


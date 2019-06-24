#pragma once

#include "thread_pool.h"

using mytp::ThreadData;

#define PPCAT_NX(A, B) A ## B
#define PPCAT(A, B) PPCAT_NX(A, B)

#define NameOfWndProcForThread(N)   PPCAT(ThreadWndProc, N)

#define DefinitionOfWndProcForThread(N)                                           \
LRESULT CALLBACK NameOfWndProcForThread(N) (HWND hwnd, UINT uMsg, WPARAM wParam,  \
                                            LPARAM lParam) {                      \
  ThreadData* thread_data{g_thread_pool.GetThreadData(N)};                        \
  if (thread_data) {                                                              \
    return thread_data->WndProc(g_thread_pool.GetAllThreadsHwnd(),                \
                                hwnd, uMsg, wParam, lParam);                      \
  } else {                                                                        \
    return DefWindowProc(hwnd, uMsg, wParam, lParam);                             \
  }                                                                               \
}

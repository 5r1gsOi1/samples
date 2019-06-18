
#include <conio.h>
#include <objidl.h>
#include <windows.h>
#include <gdiplus.h>

#include <array>
#include <iostream>
#include <string>
#include <vector>

#include <atomic>
#include <mutex>
#include <thread>
#include <sstream>

#include "defs.h"
#include "shared_memory.h"
#include "shared_picture.h"

#pragma comment(lib, "gdiplus.lib")

#define WM_SHARED_POSITION_CHANGED  (WM_APP + 0x0001)
#define WM_CUSTOM_ACTIVATE          (WM_APP + 0x0002)

constexpr bool kDrawFromBitmap{ true };

class MultiThreadCompositionPosition {
public:
  POINT GetGlobalPosition() {
    std::lock_guard<std::mutex> lock{access_};
    return global_position_;
  }

  void SetGlobalPosition(const POINT& new_position) {
    std::lock_guard<std::mutex> lock(access_);
    global_position_ = new_position;
  }

private:
  std::mutex access_;
  POINT global_position_{};
};


using SharedPosition = MultiThreadCompositionPosition;

struct ThreadPoolInterface {
  virtual void NotifyAboutStartUp(std::size_t thread_index) = 0;
  virtual void NotifyAboutTermination(std::size_t thread_index) = 0;
};

struct ThreadCallback {
  ThreadCallback() = delete;
  ThreadCallback(ThreadPoolInterface * thread_pool_interface, const std::size_t thread_number) :
    thread_pool_interface_{ thread_pool_interface }, thread_number_{ thread_number } {}
  ~ThreadCallback() { thread_pool_interface_ = nullptr; }

  virtual void NotifyAboutStartUp() {
    if (thread_pool_interface_) {
      thread_pool_interface_->NotifyAboutStartUp(thread_number_);
    }
  }

  virtual void NotifyAboutTermination() {
    if (thread_pool_interface_) {
      thread_pool_interface_->NotifyAboutTermination(thread_number_);
    }
  }

  std::size_t MyThreadNumber() const {
    return thread_number_;
  }

protected:
  std::size_t thread_number_{};
  ThreadPoolInterface * thread_pool_interface_{};
};

class ThreadData {
public:
  ThreadData() = delete;
  ThreadData(ThreadData&) = delete;
  ThreadData(ThreadData&&) = default;
  ThreadData(
    ThreadCallback * pool_callback, 
    WNDPROC wnd_proc, 
    std::shared_ptr<SharedPicture> shared_picture,
    std::shared_ptr<SharedMemory> shared_memory,
    std::shared_ptr<SharedPosition> shared_position,
    const Gdiplus::Color& pen_color,
    const POINT window_size)
    : pool_callback_(pool_callback), wnd_proc_function(wnd_proc), shared_picture(shared_picture), 
    shared_memory(shared_memory), shared_position(shared_position), pen_color(pen_color), 
    window_size(window_size), is_running(true) {}

  ~ThreadData() = default;

  HWND GetHwnd() { return this->hwnd; }

  void Stop() {
    //is_running = false;
    SendMessage(hwnd, WM_DESTROY, 0, 0);
  }

  void InvalidateRectOfAllThreads(const std::vector<HWND>& all_threads_hwnd, const RECT *lpRect, bool bErase) {
    for (auto& hwnd : all_threads_hwnd) {
      InvalidateRect(hwnd, nullptr, false);
    }
  }

  void UpdateWindowPositionOfAllThreads(const std::vector<HWND>& all_threads_hwnd, const RECT& rect) {
    for (auto& hwnd : all_threads_hwnd) {
      SendMessage(hwnd, WM_SHARED_POSITION_CHANGED, this->window_position.x, this->window_position.y);
    }
  }

  void DrawCurrentThreadWindow(HDC& hdc, PAINTSTRUCT& ps) {
    hdc = BeginPaint(this->hwnd, &ps);
    Gdiplus::Graphics graphics(hdc);
    shared_picture->DrawRasterPicture(graphics);
    EndPaint(this->hwnd, &ps);
  }

  void SetGlobalPosition(const RECT * rect) {
    if (rect and this->shared_position) {
      POINT position{rect->left, rect->top};
      position.x -= this->window_position.x;
      position.y -= this->window_position.y;
      this->shared_position->SetGlobalPosition(position);
    }
  }

  void StorePictureToSharedMemory() {
    if (not shared_memory) {
      return;
    }
    try {
      if (not shared_memory->IsOpened()) {
        shared_memory->Create();
      }
      if (not shared_memory->IsMapped()) {
        shared_memory->MapViewBuffer();
      }
      char * buffer{ const_cast<char *>(shared_memory->GetViewBuffer()) };

      ostreambuf<char> ostreamBuffer(buffer, shared_memory->GetMappedSize());
      std::ostream os(&ostreamBuffer);
      auto json_string{shared_picture->vector_.SerializeToJson()};
      const std::size_t size{ json_string.size() };
      os.write(reinterpret_cast<const char*>(&size), sizeof(size));
      os << json_string;
      os.flush();
    }
    catch (SharedMemory::errors::General& e) {
      std::cout << "SharedMemory erorr" << e.what() << std::endl;
    }
  }

  LRESULT CALLBACK WndProc(const std::vector<HWND>& all_threads_hwnd, HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    HDC hdc;
    PAINTSTRUCT ps;
    switch (uMsg) {
    case WM_MBUTTONDOWN:
      StorePictureToSharedMemory();
      break;

    case WM_LBUTTONDOWN:
      shared_picture->AddLine(pen_color);
      break;

    case WM_RBUTTONDOWN:
      shared_picture->Clear();
      shared_picture->Rasterize();
      InvalidateRectOfAllThreads(all_threads_hwnd, nullptr, false);
      break;

    case WM_MOUSEMOVE:
      if (wParam & MK_LBUTTON) {
        int mouse_x = LOWORD(lParam), mouse_y = HIWORD(lParam);
        shared_picture->AddPoint(Gdiplus::Point{mouse_x, mouse_y});
        shared_picture->Rasterize();
        DrawCurrentThreadWindow(hdc, ps);
        InvalidateRectOfAllThreads(all_threads_hwnd, nullptr, false);
      }
      break;

    case WM_MOVING: {
      RECT * rect = reinterpret_cast<RECT*>(lParam);
      if (rect) {
        SetGlobalPosition(rect);
        UpdateWindowPositionOfAllThreads(all_threads_hwnd, *rect);
      }
      return 0;
    }
   
    case WM_SIZING:
      return 0;

    case WM_PAINT:
      DrawCurrentThreadWindow(hdc, ps);
      return 0;

    case WM_SHARED_POSITION_CHANGED: {
      auto position{this->shared_position->GetGlobalPosition()};
      SetWindowPos(this->hwnd, HWND_TOP, position.x + this->window_position.x,
        position.y + this->window_position.y, 0, 0,
        SWP_NOSIZE | SWP_DEFERERASE | SWP_NOSENDCHANGING);
      return 0;
    }

    case WM_DESTROY:
      is_running = false;
      return 0;

    default:
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
  }

  void SetWindowPosition(const POINT& position) {
    this->window_position = position;
  }

  POINT GetWindowSize() {
    return this->window_size;
  }

  void operator()() {
    std::size_t thread_number{};
    if (pool_callback_) {
      thread_number = pool_callback_->MyThreadNumber();
    }
    std::string class_name{};
    class_name += "thread_#" + std::to_string(thread_number) + "_class_name";
    std::string title{};
    title += "Thread #" + std::to_string(thread_number);
    
    POINT position{this->window_position};
    if (this->shared_position) {
      auto shared_position{ this->shared_position->GetGlobalPosition() };
      position.x += shared_position.x;
      position.y += shared_position.y;
    }
    hwnd = CreateWindowForThread(
      GetModuleHandle(NULL), wnd_proc_function, class_name.c_str(),
      title.c_str(), position, this->window_size);
    if (pool_callback_) {
      pool_callback_->NotifyAboutStartUp();
    }
    MSG msg{};
    while (is_running and GetMessage(&msg, NULL, 0, 0)) {
      //if (PeekMessage(&msg, NULL, 0, 0, true)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
      //}
      //std::this_thread::yield();
    }
    if (pool_callback_) {
      pool_callback_->NotifyAboutTermination();
    }
  }

protected:
  std::shared_ptr<SharedPicture> shared_picture;
  std::shared_ptr<SharedPosition> shared_position;
  std::shared_ptr<SharedMemory> shared_memory;

  ThreadCallback * pool_callback_;

  bool is_running{ false };

  HWND hwnd{ nullptr };
  POINT window_size{}, window_position{};
  Gdiplus::Color pen_color;
  WNDPROC wnd_proc_function;
};

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

POINT CalculateOverallShift(const int rows, const int columns,
  const POINT& screen_size, const POINT& windows_size, const int gap_between_windows) {
  POINT shift{};
  POINT total_composition_size{
    columns * windows_size.x + (columns + 1) * gap_between_windows,
  rows * windows_size.y + (rows + 1) * gap_between_windows };
  shift.x = static_cast<LONG>((screen_size.x - total_composition_size.x) / 2.);
  shift.y = static_cast<LONG>((screen_size.y - total_composition_size.y) / 2.);
  return shift;
}

std::tuple<RECT, std::vector<POINT>> GenerateWindowPositions(const POINT& windows_size,
  const int windows_count, const int gap_between_windows) {
  RECT rect{};
  std::vector<POINT> positions{};

  POINT screen_size{ GetSystemMetrics(SM_CXMAXIMIZED), GetSystemMetrics(SM_CYMAXIMIZED) };
  int rows = static_cast<int>(std::ceil(std::sqrt(windows_count)));
  while (rows * windows_size.y + (rows + 1) * gap_between_windows > screen_size.y) {
    --rows;
  }
  if (rows > 0) {
    int columns = (int)std::ceil((double)windows_count / rows);
    POINT total_composition_size{
        columns * windows_size.x + (columns + 1) * gap_between_windows,
        rows * windows_size.y + (rows + 1) * gap_between_windows };
    if (total_composition_size.x < screen_size.x and total_composition_size.y < screen_size.y) {
      POINT overall_shift{
          static_cast<long>((screen_size.x - total_composition_size.x) / 2.),
          static_cast<long>((screen_size.y - total_composition_size.y) / 2.) };

      rect = RECT{overall_shift.x, overall_shift.y, 
                  overall_shift.x + total_composition_size.x, 
                  overall_shift.y + total_composition_size.x, };

      int remaining_windows{ windows_count };
      for (int i{}; i < columns; ++i) {
        int shift{};
        if (remaining_windows < rows) {
          shift = static_cast<int>(((rows - remaining_windows) * windows_size.y + ((rows - remaining_windows) * gap_between_windows)) / 2.);
        }
        for (int j{}; j < rows; ++j) {
          int x { i * windows_size.x + (i + 1) * gap_between_windows };
          int y { j * windows_size.y + (j + 1) * gap_between_windows + shift };
          positions.push_back(POINT{ x, y });
          --remaining_windows;
        }
      }
    }
  }
  return std::make_tuple(rect, positions);
}

struct ThreadControl {
  ThreadControl(ThreadData * data, ThreadCallback * callback)
    : data(data), callback(callback) {}
  ~ThreadControl() {
    delete data;
    delete callback;
    data = nullptr;
    callback = nullptr;
  }

  ThreadData * data;
  ThreadCallback * callback;
  bool is_running_{false};
};

class MyThreadPool : public ThreadPoolInterface {
public:
  MyThreadPool() = default;
  MyThreadPool(const POINT& windows_size) : windows_size_(windows_size) {
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    shared_position_ = std::make_shared<SharedPosition>();
  }

  virtual void AddNewThread(WNDPROC wnd_proc, std::shared_ptr<SharedPicture> shared_picture, 
                            std::shared_ptr<SharedMemory> shared_memory,
                            const Gdiplus::Color& pen_color) {
    if (not is_running_) {
      std::size_t new_thread_number{threads_controls_.size()};
      auto callback{new ThreadCallback{this, new_thread_number}};
      auto data{new ThreadData{callback, wnd_proc, shared_picture, shared_memory, 
        shared_position_, pen_color, this->windows_size_}};
      auto control{std::make_unique<ThreadControl>(data, callback)};
      threads_controls_.emplace_back(std::move(control));
    }
  }

  virtual void StartThreads() {
    if (not is_running_) {
      if (not threads_controls_.empty()) {
        auto [rect, windows_positions] {GenerateWindowPositions(this->windows_size_, threads_controls_.size(), 10)};
        this->shared_position_->SetGlobalPosition(POINT{rect.left, rect.top});
        if (threads_controls_.size() == windows_positions.size()) {
          for (std::size_t i{}; i < threads_controls_.size(); ++i) {
            if (threads_controls_.at(i)->data) {
              threads_controls_.at(i)->data->SetWindowPosition(windows_positions.at(i));
            }
          }
        }
        for (auto& tc : threads_controls_) {
          threads_.emplace_back(std::ref(*tc->data));
        }
      }
      using namespace std::chrono_literals;
      this->is_running_ = true;
      while (this->is_running_ and ThreadsAreRunning()) {
        std::this_thread::sleep_for(1ms);
      }
      std::cout << "Main thread terminated" << std::endl;
    }
  }

  ThreadData* GetThreadData(const size_t thread_index) {
    std::lock_guard<std::mutex> lock{access_mutex_};
    if (thread_index < threads_controls_.size() and threads_controls_.at(thread_index)) {
      return threads_controls_.at(thread_index)->data;
    } else {
      return nullptr;
    }
  }

  std::vector<HWND> GetAllThreadsHwnd() {
    std::lock_guard<std::mutex> lock{access_mutex_};
    std::vector<HWND> all_threads_hwnd{};
    all_threads_hwnd.reserve(threads_controls_.size());
    for (auto& thread : threads_controls_) {
      if (thread->is_running_) {
        all_threads_hwnd.push_back(thread->data->GetHwnd());
      }
    }
    return all_threads_hwnd;
  }

  void JoinAllThreads() {
    for (auto& thread : threads_) {
      thread.join();
    }
  }

  void TerminateAllThreads() {
    for (auto& tc : threads_controls_) {
      if (tc and tc->data) {
        tc->data->Stop();
      }
    }
  }

  void ShutDown() {
    Gdiplus::GdiplusShutdown(gdiplusToken);
  }

  ~MyThreadPool() {
    TerminateAllThreads();
    ShutDown();
    JoinAllThreads();
  }

  bool ThreadsAreRunning() {
    return number_of_running_threads_ > 0 or (std::size_t)number_of_started_threads_ < threads_controls_.size();
  }

  virtual void NotifyAboutStartUp(std::size_t thread_number) override {
    std::lock_guard<std::mutex> lock(access_mutex_);
    this->threads_controls_.at(thread_number)->is_running_ = true;
    ++number_of_started_threads_;
    ++number_of_running_threads_;
    std::cout << "Thread #" << thread_number << " started" << std::endl;
  }

  virtual void NotifyAboutTermination(std::size_t thread_number) override {
    std::lock_guard<std::mutex> lock(access_mutex_);
    this->threads_controls_.at(thread_number)->is_running_ = false;
    --number_of_running_threads_;
    std::cout << "Thread #" << thread_number << " terminated" << std::endl;
  }

protected:
  std::mutex access_mutex_;
  std::vector<std::unique_ptr<ThreadControl>> threads_controls_;
  std::vector<std::thread> threads_;
  bool is_running_{ false };

  Gdiplus::GdiplusStartupInput gdiplusStartupInput;
  ULONG_PTR gdiplusToken;

  int number_of_running_threads_{};
  int number_of_started_threads_{};

  POINT windows_size_;
  std::shared_ptr<SharedPosition> shared_position_;
};

MyThreadPool g_thread_pool{ kDefaultWindowSize };

DefinitionOfWndProcForThread(0)
DefinitionOfWndProcForThread(1)
DefinitionOfWndProcForThread(2)
DefinitionOfWndProcForThread(3)

BOOL WINAPI CtrlHandler(DWORD fdwCtrlType) {
  switch (fdwCtrlType) {
  case CTRL_C_EVENT:
  case CTRL_BREAK_EVENT:
  case CTRL_CLOSE_EVENT:
    g_thread_pool.TerminateAllThreads();
    g_thread_pool.ShutDown();
    //g_thread_pool.JoinAllThreads();
    return TRUE;

  default:
    return FALSE;
  }
}

void RunThreads() {
  SetConsoleCtrlHandler(CtrlHandler, TRUE);
  auto shared_picture = std::make_shared<SharedPicture>(kPointsCount, kLinesCount, kDefaultWindowSize);
  auto shared_memory = std::make_shared<SharedMemory>(kSharedFileNameOnDisk, kMappedFileName, kMaxSizeOfMappedFile);
  g_thread_pool.AddNewThread(NameOfWndProcForThread(0), shared_picture, shared_memory, Gdiplus::Color{ 255,255,0,0 });
  g_thread_pool.AddNewThread(NameOfWndProcForThread(1), shared_picture, shared_memory, Gdiplus::Color{ 255,0,193,255 });
  g_thread_pool.AddNewThread(NameOfWndProcForThread(2), shared_picture, shared_memory, Gdiplus::Color{ 255,0,155,0 });
  g_thread_pool.AddNewThread(NameOfWndProcForThread(3), shared_picture, shared_memory, Gdiplus::Color{ 255,255,180,0 });
  g_thread_pool.StartThreads();
}

int main() {
  RunThreads();
  _getch();
}

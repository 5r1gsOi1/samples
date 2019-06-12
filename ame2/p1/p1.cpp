
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

#include "defs.h"

#pragma comment(lib, "gdiplus.lib")

#define WM_SHARED_POSITION_CHANGED (WM_APP + 0x0001)

constexpr bool kDrawFromBitmap{ true };

template <size_t total_number_of_lines, size_t number_of_points_in_line>
class MultiThreadPicture {
public:
  typedef std::size_t ThreadIndex;
  struct Line {
    Line() = default;
    std::size_t start{}, length{};
    Gdiplus::Color color;
    ThreadIndex creator{};
  };
  struct Picture {
    std::array<Gdiplus::Point, number_of_points_in_line> points{};
    std::array<Line, total_number_of_lines> lines{};
    std::size_t current_point{}, start_line{}, end_line{};
  };

  struct DrawerThread {
    bool screen_needs_to_be_cleared{ false };
  };

  Gdiplus::Bitmap * LockBitmapAndGetIt() {
    bitmap_mutex_.lock();
    return bitmap_;
  }

  void ReleaseBitmap() {
    bitmap_mutex_.unlock();
  }

  MultiThreadPicture() {
    if constexpr (kDrawFromBitmap) {
      bitmap_ = new Gdiplus::Bitmap(kDefaultWindowSize.x, kDefaultWindowSize.y, PixelFormat32bppRGB);
      Gdiplus::Graphics graphics(bitmap_);
      graphics.Clear(Gdiplus::Color(255, 255, 255, 255));
    }
  }

  void CreateBitmap(const POINT* size) {
    bitmap_mutex_.lock();
    delete bitmap_;
    bitmap_ = new Gdiplus::Bitmap(size.x, size.y, PixelFormat32bppRGB);
    bitmap_mutex_.unlock();
  }

  Picture GetPicture() {
    LockAccess();
    Picture picture_copy{ picture };
    ClearAccess();
    return picture_copy;
  }

  void ClearPicture(const ThreadIndex thread_index) {
    LockAccess();
    for (auto& line : picture.lines) {
      line.length = 0;
    }
    ClearAccess();
  }

  ThreadIndex AddThread(const Gdiplus::Color& color) {
    LockAccess();
    drawer_threads.push_back(DrawerThread{});
    ClearAccess();
    return drawer_threads.size() - 1;
  }

  void RemoveLineThatStartsAt(const std::size_t start_index) {
    std::cout << "SEARCHING : " << start_index << std::endl;
    auto real_index{start_index};
    if (start_index >= picture.points.size()) {
      real_index = 0;
    }
    for (auto & line : picture.lines) {
      std::cout << "   comparing to " << line.start << "[" << line.length  << "]" << std::endl;
      if (line.length != 0 and line.start == real_index) {
        std::cout << "FOUND : " << line.start << std::endl;
        line.start = 0;
        line.length = 0;
        break;
      }
    }
  }

  void DecrementLineFromLeftSide(const std::size_t line_index) {
    auto & line = picture.lines.at(line_index);
    if (line.length > 0) {
      --line.length;
      ++line.start;
      if (line.start >= picture.points.size()) {
        line.start = 0;
      }
    }
  }

  void AddPoint(const ThreadIndex thread_index, const Gdiplus::Point& point) {
    LockAccess();
    auto& points = picture.points;
    auto& current_point = picture.current_point;
    auto& current_line = picture.lines.at(picture.end_line);

    //std::cout << "next line = " << picture.start_line << std::endl;
    std::cout << "current point = " << current_point << std::endl;

    if (current_point == picture.lines.at(picture.start_line).start) {
      std::cout << "  MATCH!, " << picture.start_line << std::endl;
      DecrementLineFromLeftSide(picture.start_line);
      if (picture.lines.at(picture.start_line).length == 0 and 
          picture.start_line != picture.end_line) {
        ++picture.start_line;
        if (picture.start_line >= picture.lines.size()) {
          picture.start_line = 0;
        }
      }
    }

    points.at(current_point) = point;
    if (current_line.length < points.size()) {
      ++current_line.length;
    }

    ++current_point;
    if (current_point >= points.size()) {
      current_point = 0;
    }
    ClearAccess();
  }

  void AddLine(const ThreadIndex thread_index, const Gdiplus::Color& color) {
    LockAccess();

    ++picture.end_line;
    if (picture.end_line >= picture.lines.size()) {
      picture.end_line = 0;
    }
    if (picture.end_line == picture.start_line) {
      ++picture.start_line;
      if (picture.start_line >= picture.lines.size()) {
        picture.start_line = 0;
      }
    }

    auto & line {picture.lines.at(picture.end_line)};
    line.length = 0;
    line.start = picture.current_point;
    line.color = color;
    line.creator = thread_index;

    ClearAccess();
  }

  bool ScreenNeedsToBeCleared(const ThreadIndex thread_index) {
    LockAccess();
    if (drawer_threads.at(thread_index).screen_needs_to_be_cleared) {
      drawer_threads.at(thread_index).screen_needs_to_be_cleared = false;
      ClearAccess();
      return true;
    }
    else {
      ClearAccess();
      return false;
    }
  }

private:
  void LockAccess() {
    while (access_flag.test_and_set()) {};
  }

  void ClearAccess() {
    access_flag.clear();
  }

  Picture picture;
  std::vector<DrawerThread> drawer_threads;
  std::atomic_flag access_flag{};

  Gdiplus::Bitmap * bitmap_{};
  std::mutex bitmap_mutex_;
};

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

using SharedPicture = MultiThreadPicture<20, 1000>;
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

protected:
  std::size_t thread_number_{};
  ThreadPoolInterface * thread_pool_interface_{};
};

class ThreadData {
public:
  ThreadData() = delete;
  ThreadData(ThreadData&) = delete;
  ThreadData(ThreadData&&) = default;
  ThreadData(ThreadCallback * pool_callback, WNDPROC wnd_proc, std::shared_ptr<SharedPicture> shared_picture,
    const Gdiplus::Color& pen_color, std::shared_ptr<SharedPosition> shared_position, const POINT window_size)
    : pool_callback_(pool_callback), wnd_proc_function(wnd_proc), shared_picture(shared_picture), 
    pen_color(pen_color), shared_position(shared_position), window_size(window_size) {
    index_in_shared_picture = shared_picture->AddThread(pen_color);
    is_running = true;
  }

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

  void UpdateBitmap() {
    if constexpr (kDrawFromBitmap) {
      auto * bitmap = shared_picture->LockBitmapAndGetIt();
      Gdiplus::Graphics graphics(bitmap);
      DrawPicture(graphics);
      shared_picture->ReleaseBitmap();
    }
  }

  void DrawCurrentThreadWindow(HDC& hdc, PAINTSTRUCT& ps) {
    hdc = BeginPaint(this->hwnd, &ps);
    Draw(hdc);
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

  LRESULT CALLBACK WndProc(const std::vector<HWND>& all_threads_hwnd, HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    HDC hdc;
    PAINTSTRUCT ps;
    switch (uMsg) {
    case WM_LBUTTONDOWN:
      StartDrawingLine();
      break;

    case WM_LBUTTONUP:
      StopDrawingLine();
      break;

    case WM_RBUTTONDOWN:
      ClearPicture();
      UpdateBitmap();
      InvalidateRectOfAllThreads(all_threads_hwnd, nullptr, false);
      break;

    case WM_MOUSEMOVE:
      if (wParam & MK_LBUTTON) {
        int mouse_x = LOWORD(lParam), mouse_y = HIWORD(lParam);
        AddPoint(mouse_x, mouse_y);
        UpdateBitmap();
        DrawCurrentThreadWindow(hdc, ps);
        InvalidateRectOfAllThreads(all_threads_hwnd, nullptr, false);
      }
      else {
        StopDrawingLine();
      }
      break;

    //case WM_ERASEBKGND:
    //  return 0;

    case WM_MOVING: {
      RECT * rect = reinterpret_cast<RECT*>(lParam);
      if (rect) {
        SetGlobalPosition(rect);
        UpdateWindowPositionOfAllThreads(all_threads_hwnd, *rect);
      }
      return 0;
    }

    case WM_SIZING:
      RequestScreenClear();
      return 0;

    /*case WM_NCPAINT:
      //SetWindowPosition();
      //return 0;
      return DefWindowProc(hwnd, uMsg, wParam, lParam);*/

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
    std::string class_name{};
    class_name += "thread_#" + std::to_string(index_in_shared_picture) + "_class_name";
    std::string title{};
    title += "Thread #" + std::to_string(index_in_shared_picture);
    
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

  void StartDrawingLine() {
    shared_picture->AddLine(index_in_shared_picture, pen_color);
  }

  void StopDrawingLine() {  }

  void ClearPicture() {
    shared_picture->ClearPicture(index_in_shared_picture);
  }

  void AddPoint(const int x, const int y) { AddPoint(Gdiplus::Point{ x, y }); }

  void AddPoint(const Gdiplus::Point& point) {
    shared_picture->AddPoint(index_in_shared_picture, point);
  }

  bool ScreenClearRequestedLocally() {
    if (screen_clear_requested_locally) {
      screen_clear_requested_locally = false;
      return true;
    }
    else {
      return false;
    }
  }

  void RequestScreenClear() {
    screen_clear_requested_locally = true;
  }

  void DrawFromBitmap(Gdiplus::Graphics& graphics) {
    auto * bitmap = shared_picture->LockBitmapAndGetIt();
    Gdiplus::Rect sizeR(0, 0, bitmap->GetWidth(), bitmap->GetHeight());
    graphics.DrawImage(bitmap, sizeR, 0, 0,
      (int)bitmap->GetWidth(),
      (int)bitmap->GetHeight(),
      Gdiplus::UnitPixel);
    shared_picture->ReleaseBitmap();
  }
  
  void DrawPicture(Gdiplus::Graphics& graphics) {
    if (ScreenClearRequestedLocally() or
      shared_picture->ScreenNeedsToBeCleared(index_in_shared_picture)) {
      graphics.Clear(Gdiplus::Color(255, 255, 255, 255));
    }
    graphics.Clear(Gdiplus::Color(255, 255, 255, 255));

    auto picture{ shared_picture->GetPicture() };

    graphics.SetSmoothingMode(Gdiplus::SmoothingMode::SmoothingModeNone);
    //graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
    //graphics.Flush(Gdiplus::FlushIntention::FlushIntentionSync);
    
    for (auto & line : picture.lines) {
      if (line.length != 0) {
        Gdiplus::Pen pen(line.color, 4.f);
        if (line.length <= picture.points.size() - line.start) {
          graphics.DrawLines(&pen, picture.points.data() + line.start, line.length);
        } else {
          std::size_t right_part{picture.points.size() - line.start};
          graphics.DrawLines(&pen, picture.points.data() + line.start, right_part);
          graphics.DrawLine(&pen, picture.points.back(), picture.points.front());
          graphics.DrawLines(&pen, picture.points.data(), line.length - right_part);
        }
      }
    }


    /*
    for (std::size_t i{ picture.line_index + 1 }; i < picture.lines.size(); ++i) {
      Gdiplus::Pen pen(picture.lines.at(i).color, 4.f);
      graphics.DrawLines(&pen, picture.lines.at(i).points.data(), picture.lines.at(i).index);
    }
    for (std::size_t i{}; i <= picture.line_index; ++i) {
      Gdiplus::Pen pen(picture.lines.at(i).color, 4.f);
      graphics.DrawLines(&pen, picture.lines.at(i).points.data(), picture.lines.at(i).index);
    }//*/
  }

  void Draw(HDC hdc) {
    Gdiplus::Graphics graphics(hdc);
    if constexpr (kDrawFromBitmap) {
      DrawFromBitmap(graphics);
    } else {
      DrawPicture(graphics);
    }
  }

protected:
  std::shared_ptr<SharedPicture> shared_picture;
  std::shared_ptr<SharedPosition> shared_position;
  SharedPicture::ThreadIndex index_in_shared_picture{};

  ThreadCallback * pool_callback_;

  bool is_running{ false };
  bool screen_clear_requested_locally{ false };


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
                            const Gdiplus::Color& pen_color) {
    if (not is_running_) {
      std::size_t new_thread_number{threads_controls_.size()};
      auto callback{new ThreadCallback{this, new_thread_number}};
      auto data{new ThreadData{callback, wnd_proc, shared_picture, pen_color, 
        shared_position_, this->windows_size_}};
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
  auto shared_picture = std::make_shared<SharedPicture>();
  g_thread_pool.AddNewThread(NameOfWndProcForThread(0), shared_picture, Gdiplus::Color{ 255,255,0,0 });
  g_thread_pool.AddNewThread(NameOfWndProcForThread(1), shared_picture, Gdiplus::Color{ 255,0,193,255 });
  g_thread_pool.AddNewThread(NameOfWndProcForThread(2), shared_picture, Gdiplus::Color{ 255,0,155,0 });
  g_thread_pool.AddNewThread(NameOfWndProcForThread(3), shared_picture, Gdiplus::Color{ 255,255,180,0 });
  g_thread_pool.StartThreads();
}




int main() {

  const std::string file_name{"shared_file.txt"};
  HANDLE file = CreateFile(file_name.c_str(), GENERIC_READ | GENERIC_WRITE, 
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_SEQUENTIAL_SCAN, 
                           NULL);

  HANDLE mapped_file = CreateFileMapping(file, NULL, PAGE_READWRITE, 0, 
                                         kMaxSizeOfMappedFile, kNameOfMappedFile.c_str());


  if (mapped_file == NULL) {
    std::cout << "CreateFileMapping() failed, " << GetLastError() << std::endl;
    _getch();
    return 1;
  }

  LPCTSTR buffer = (LPTSTR)MapViewOfFile(mapped_file, FILE_MAP_ALL_ACCESS,
                                         0, 0, kMaxSizeOfMappedFile);
  if (buffer == NULL) {
    std::cout << "MapViewFile() failed" << std::endl;
    CloseHandle(mapped_file);
    _getch();
    return 1;
  }

  UnmapViewOfFile(buffer);
  CloseHandle(mapped_file);
  CloseHandle(file);


  char * current_directory = new char[256];
  GetCurrentDirectory(256, current_directory);
  std::cout << "current dir = " << current_directory << std::endl;


  RunThreads();

  _getch();

}

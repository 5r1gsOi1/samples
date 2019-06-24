
#pragma once

#include <thread>
#include <stdexcept>

#include "spinlock.h"

namespace mytp {
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


  class ThreadDataBase {
  public:
    struct errors {
      struct General : public std::runtime_error {};
      struct DefaultThreadFunctionIsCalled : public General {};
    };

    ThreadDataBase() = delete;
    ThreadDataBase(ThreadCallback * pool_callback)
      : pool_callback_(pool_callback) {}

    ThreadDataBase(ThreadDataBase&) = delete;
    ThreadDataBase(ThreadDataBase&&) = default;

    ~ThreadDataBase() = default;

    void Stop() {
      is_running = false;
    }

    virtual void ThreadFunction() {
      while (is_running) { std::this_thread::yield(); }
    }

    void operator()() {
      if (pool_callback_) {
        thread_number_ = pool_callback_->MyThreadNumber();
      }
      if (pool_callback_) {
        pool_callback_->NotifyAboutStartUp();
      }
      is_running = true;
      ThreadFunction();
      if (pool_callback_) {
        pool_callback_->NotifyAboutTermination();
      }
    }

  protected:
    ThreadCallback * pool_callback_{};
    bool is_running{ false };
    std::size_t thread_number_{};
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
        std::size_t new_thread_number{ threads_controls_.size() };
        auto callback{ new ThreadCallback{this, new_thread_number} };
        auto data{ new ThreadData{callback, wnd_proc, shared_picture, shared_memory,
          shared_position_, pen_color, this->windows_size_} };
        auto control{ std::make_unique<ThreadControl>(data, callback) };
        threads_controls_.emplace_back(std::move(control));
      }
    }

    virtual void StartThreads() {
      if (not is_running_) {
        if (not threads_controls_.empty()) {
          auto [rect, windows_positions] {GenerateWindowPositions(this->windows_size_, threads_controls_.size(), 10)};
          this->shared_position_->SetGlobalPosition(POINT{ rect.left, rect.top });
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
      std::lock_guard<std::mutex> lock{ access_mutex_ };
      if (thread_index < threads_controls_.size() and threads_controls_.at(thread_index)) {
        return threads_controls_.at(thread_index)->data;
      }
      else {
        return nullptr;
      }
    }

    std::vector<HWND> GetAllThreadsHwnd() {
      std::lock_guard<std::mutex> lock{ access_mutex_ };
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

}



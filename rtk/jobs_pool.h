
#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

template <class Job>
class JobsPool {
 public:
  JobsPool() = delete;
  JobsPool(const int number_of_workers)
      : number_of_workers_{number_of_workers} {
    start();
  }
  ~JobsPool() {
    FinishAvailableJobs();
    stop();
  }

  void AddJob(const Job& new_job) {
    if (not is_finishing_) {
      {
        std::scoped_lock lock{jobs_access_};
        jobs_.push_back(new_job);
      }
      wake_up_signal_.notify_one();
    }
  }

  void AddJob(Job&& new_job) {
    if (not is_finishing_) {
      {
        std::scoped_lock lock{jobs_access_};
        jobs_.push_back(std::move(new_job));
      }
      wake_up_signal_.notify_one();
    }
  }

  void start() {
    if (not this->is_running_ and not this->is_finishing_) {
      this->is_running_ = true;
      for (int i{}; i < number_of_workers_; ++i) {
        workers_.emplace_back(&JobsPool::WorkerMain, this, i);
      }
    }
  }

  void AbandonJobsAndStop() { stop(); }

  void FinishAvailableJobs() {
    if (this->is_running_) {
      this->is_finishing_ = true;
      wake_up_signal_.notify_all();
      while (true) {
        std::scoped_lock lock{jobs_access_};
        if (jobs_.empty()) {
          break;
        }
        std::this_thread::yield();
      }
    }
  }

  void stop() {
    if (this->is_running_) {
      this->is_running_ = false;
      wake_up_signal_.notify_all();
      for (auto& w : workers_) {
        w.join();
      }
    }
  }

 private:
  std::optional<Job> PickAJob() {
    std::scoped_lock lock{jobs_access_};
    if (not jobs_.empty()) {
      auto job{std::move(jobs_.back())};
      jobs_.pop_back();
      return job;
    } else {
      return std::nullopt;
    }
  }

  void WorkerMain(const int thread_number) {
    while (this->is_running_) {
      auto job{PickAJob()};
      if (job.has_value()) {
        job.value().perform(thread_number);
      } else if (not this->is_finishing_) {
        std::unique_lock lock{workers_mutex_};
        wake_up_signal_.wait(lock);
      } else {
        break;
      }
    }
  }

  std::vector<std::thread> workers_{};
  int number_of_workers_{};
  std::deque<Job> jobs_{};
  std::mutex workers_mutex_{}, jobs_access_{};
  std::condition_variable wake_up_signal_{};

  bool is_running_{false}, is_finishing_{false};
  bool jobs_are_abandoned_{false};
};

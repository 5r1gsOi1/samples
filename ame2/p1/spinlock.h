
#pragma once

#include <thread>
#include <atomic>

class SpinLock {
public:
  void lock() {
    while (flag_.test_and_set(std::memory_order_acquire)) {
      std::this_thread::yield();
    };
  }

  void unlock() {
    flag_.clear(std::memory_order_release);
  }

private:
  std::atomic_flag flag_{ ATOMIC_FLAG_INIT };
};

#pragma once

#include <atomic>
#include <thread>

namespace fsm::detail {

class reentrancy_tracker {
  public:
    reentrancy_tracker() = default;

    [[nodiscard]] bool is_reentrant_call() const noexcept {
        return dispatching_thread_.load(std::memory_order_acquire) == std::this_thread::get_id();
    }

    [[nodiscard]] int depth() const noexcept {
        return is_reentrant_call() ? dispatch_depth_.load(std::memory_order_acquire) : 0;
    }

    struct depth_guard {
        reentrancy_tracker& tracker;

        explicit depth_guard(reentrancy_tracker& t) : tracker(t) {
            tracker.dispatching_thread_.store(std::this_thread::get_id(), std::memory_order_release);
            tracker.dispatch_depth_.fetch_add(1, std::memory_order_acq_rel);
        }

        ~depth_guard() {
            if (tracker.dispatch_depth_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                tracker.dispatching_thread_.store(std::thread::id{}, std::memory_order_release);
            }
        }
    };

  private:
    std::atomic<std::thread::id> dispatching_thread_{};
    std::atomic<int> dispatch_depth_{0};
};

}  // namespace fsm::detail

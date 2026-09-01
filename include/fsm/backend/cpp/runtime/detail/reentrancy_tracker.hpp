#pragma once

#include <atomic>
#include <thread>

namespace fsm::detail {

class reentrancy_tracker {
  public:
    reentrancy_tracker() = default;

    [[nodiscard]] bool is_reentrant_call() const noexcept {
        return dispatching_thread_.load(std::memory_order_relaxed) == std::this_thread::get_id();
    }

    [[nodiscard]] int depth() const noexcept {
        return dispatch_depth_;
    }

    struct depth_guard {
        reentrancy_tracker& tracker;

        explicit depth_guard(reentrancy_tracker& t) : tracker(t) {
            tracker.dispatching_thread_.store(std::this_thread::get_id(), std::memory_order_relaxed);
            ++tracker.dispatch_depth_;
        }

        ~depth_guard() {
            if (--tracker.dispatch_depth_ == 0) {
                tracker.dispatching_thread_.store(std::thread::id{}, std::memory_order_relaxed);
            }
        }
    };

  private:
    std::atomic<std::thread::id> dispatching_thread_{};
    int dispatch_depth_{0};
};

}  // namespace fsm::detail

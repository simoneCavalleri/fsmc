#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <utility>

#include "fsm/backend/cpp/runtime/async_event_queue.hpp"

namespace fsm::detail {

/**
 * @brief Manages the active-object event queue and background worker thread lifecycle.
 */
template <typename Task, typename FsmRef>
class worker_thread_controller {
  public:
    worker_thread_controller() = default;

    ~worker_thread_controller() { stop_worker(); }

    worker_thread_controller(const worker_thread_controller&) = delete;
    worker_thread_controller& operator=(const worker_thread_controller&) = delete;
    worker_thread_controller(worker_thread_controller&&) = delete;
    worker_thread_controller& operator=(worker_thread_controller&&) = delete;

    void start_worker(FsmRef& fsm) {
        bool expected = false;
        if (worker_running_.compare_exchange_strong(expected, true)) {
            queue_.start();
            worker_thread_ = std::thread([this, &fsm]() { worker_loop(fsm); });
        }
    }

    void stop_worker() {
        bool expected = true;
        if (worker_running_.compare_exchange_strong(expected, false)) {
            queue_.stop();
            if (worker_thread_.joinable()) {
                if (std::this_thread::get_id() != worker_thread_.get_id()) {
                    worker_thread_.join();
                } else {
                    worker_thread_.detach();
                }
            }
        }
    }

    [[nodiscard]] bool is_worker_running() const noexcept { return worker_running_.load(std::memory_order_relaxed); }

    [[nodiscard]] bool is_calling_from_worker_thread() const noexcept {
        return worker_thread_.get_id() == std::this_thread::get_id();
    }

    void clear_queue() { queue_.clear(); }

    [[nodiscard]] std::size_t pending_events_count() const noexcept { return queue_.size(); }

    [[nodiscard]] bool is_queue_empty() const noexcept { return queue_.empty(); }

    void push(Task task) { queue_.push(std::move(task)); }

    template <typename Rep, typename Period>
    void push_timed(std::chrono::time_point<std::chrono::steady_clock, std::chrono::duration<Rep, Period>> deadline,
                    Task task) {
        queue_.push_timed(deadline, std::move(task));
    }

    bool process_one(FsmRef& fsm) {
        Task task;
        if (queue_.try_pop(task)) {
            task(fsm);
            return true;
        }
        return false;
    }

    std::size_t run_until_empty(FsmRef& fsm) {
        std::size_t processed = 0;
        while (process_one(fsm)) {
            ++processed;
        }
        return processed;
    }

    void wait_until_idle() {
        while (!is_queue_empty()) {
            std::this_thread::yield();
        }
    }

  private:
    void worker_loop(FsmRef& fsm) {
        while (worker_running_.load(std::memory_order_relaxed)) {
            Task task;
            if (queue_.pop_wait(task)) {
                task(fsm);
            }
        }
    }

    async_event_queue<Task> queue_{};
    std::atomic<bool> worker_running_{false};
    std::thread worker_thread_{};
};

}  // namespace fsm::detail

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
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
        std::scoped_lock lock(lifecycle_mutex_);
        if (!worker_running_.load(std::memory_order_acquire)) {
            if (worker_thread_.joinable()) {
                worker_thread_.join();
            }
            queue_.start();
            worker_running_.store(true, std::memory_order_release);
            worker_thread_ = std::thread([this, &fsm]() {
                worker_thread_id_.store(std::this_thread::get_id(), std::memory_order_release);
                worker_loop(fsm);
            });
            worker_thread_id_.store(worker_thread_.get_id(), std::memory_order_release);
        }
    }

    void stop_worker() {
        std::unique_lock lock(lifecycle_mutex_);
        if (worker_running_.exchange(false, std::memory_order_acq_rel)) {
            queue_.stop();
            if (worker_thread_.joinable()) {
                if (std::this_thread::get_id() != worker_thread_id_.load(std::memory_order_acquire)) {
                    worker_thread_.join();
                } else {
                    worker_thread_.detach();
                }
            }
            worker_thread_id_.store(std::thread::id{}, std::memory_order_release);
        }
    }

    [[nodiscard]] bool is_worker_running() const noexcept { return worker_running_.load(std::memory_order_acquire); }

    [[nodiscard]] bool is_calling_from_worker_thread() const noexcept {
        return worker_thread_id_.load(std::memory_order_acquire) == std::this_thread::get_id();
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
            task_finish_guard finish_guard{queue_};
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

    void wait_until_idle() { queue_.wait_until_idle(); }

  private:
    struct task_finish_guard {
        async_event_queue<Task>& queue;
        ~task_finish_guard() { queue.finish_task(); }
    };

    void worker_loop(FsmRef& fsm) {
        while (worker_running_.load(std::memory_order_acquire)) {
            Task task;
            if (queue_.pop_wait(task)) {
                task_finish_guard finish_guard{queue_};
                task(fsm);
            }
        }
    }

    mutable std::mutex lifecycle_mutex_{};
    async_event_queue<Task> queue_{};
    std::atomic<bool> worker_running_{false};
    std::atomic<std::thread::id> worker_thread_id_{};
    std::thread worker_thread_{};
};

}  // namespace fsm::detail

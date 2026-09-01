#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <utility>
#include <vector>

#include "fsm/backend/cpp/runtime/async_types.hpp"

namespace fsm {

template <typename HandlerType>
class async_event_queue {
  public:
    using timed_event_type = timed_event<HandlerType>;

    async_event_queue() = default;

    void push(HandlerType task) {
        {
            std::scoped_lock lock(mutex_);
            event_queue_.push(std::move(task));
        }
        cv_.notify_one();
    }

    void push_timed(std::chrono::steady_clock::time_point deadline, HandlerType task) {
        {
            std::scoped_lock lock(mutex_);
            timed_queue_.push(timed_event_type{deadline, std::move(task)});
        }
        cv_.notify_one();
    }

    [[nodiscard]] std::size_t size() const {
        std::scoped_lock lock(mutex_);
        return event_queue_.size() + timed_queue_.size();
    }

    [[nodiscard]] bool empty() const {
        std::scoped_lock lock(mutex_);
        return event_queue_.empty() && timed_queue_.empty();
    }

    void clear() {
        std::scoped_lock lock(mutex_);
        std::queue<HandlerType> empty_q;
        std::swap(event_queue_, empty_q);
        std::priority_queue<timed_event_type, std::vector<timed_event_type>, std::greater<>> empty_timed;
        std::swap(timed_queue_, empty_timed);
    }

    // Single-event pop for manual polling or worker loop
    bool try_pop(HandlerType& out_task) {
        std::scoped_lock lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        if (!timed_queue_.empty() && now >= timed_queue_.top().deadline) {
            out_task = std::move(const_cast<timed_event_type&>(timed_queue_.top()).task);
            timed_queue_.pop();
            return true;
        }
        if (!event_queue_.empty()) {
            out_task = std::move(event_queue_.front());
            event_queue_.pop();
            return true;
        }
        return false;
    }

    // Drain all ready tasks (for process_all or stop_worker)
    std::vector<HandlerType> drain_ready_batch() {
        std::vector<HandlerType> batch;
        std::scoped_lock lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        while (!timed_queue_.empty() && now >= timed_queue_.top().deadline) {
            batch.push_back(std::move(const_cast<timed_event_type&>(timed_queue_.top()).task));
            timed_queue_.pop();
        }
        while (!event_queue_.empty()) {
            batch.push_back(std::move(event_queue_.front()));
            event_queue_.pop();
        }
        return batch;
    }

    bool pop_wait(HandlerType& out_task) {
        std::unique_lock lock(mutex_);
        while (true) {
            const auto now = std::chrono::steady_clock::now();
            if (!timed_queue_.empty() && now >= timed_queue_.top().deadline) {
                out_task = std::move(const_cast<timed_event_type&>(timed_queue_.top()).task);
                timed_queue_.pop();
                return true;
            }
            if (!event_queue_.empty()) {
                out_task = std::move(event_queue_.front());
                event_queue_.pop();
                return true;
            }
            if (!running_) {
                return false;
            }
            if (!timed_queue_.empty()) {
                cv_.wait_until(lock, timed_queue_.top().deadline);
            } else {
                cv_.wait(lock);
            }
        }
    }

    void start() {
        {
            std::scoped_lock lock(mutex_);
            running_ = true;
        }
    }

    void stop() {
        {
            std::scoped_lock lock(mutex_);
            running_ = false;
        }
        cv_.notify_all();
    }

    std::mutex& mutex() noexcept { return mutex_; }
    std::condition_variable& cv() noexcept { return cv_; }
    std::queue<HandlerType>& event_queue() noexcept { return event_queue_; }
    std::priority_queue<timed_event_type, std::vector<timed_event_type>, std::greater<>>& timed_queue() noexcept {
        return timed_queue_;
    }

  private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<HandlerType> event_queue_;
    std::priority_queue<timed_event_type, std::vector<timed_event_type>, std::greater<>> timed_queue_;
    bool running_{true};
};

}  // namespace fsm

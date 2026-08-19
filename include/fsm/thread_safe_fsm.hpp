#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "fsm.hpp"

namespace fsm {

// Thread-safe wrapper for FSM with synchronized dispatch, asynchronous event
// queue, and Context Injection
template <typename Table, typename Context = no_context, typename InitialState = typename Table::initial_state>
class thread_safe_fsm {
  public:
    using fsm_type = fsm<Table, Context, InitialState>;
    using context_type = Context;
    using event_handler = std::function<void(fsm_type&)>;

    // Static compile-time introspection
    static constexpr std::size_t state_count = fsm_type::state_count;
    static constexpr std::size_t transition_count = fsm_type::transition_count;
    static constexpr std::size_t event_count = fsm_type::event_count;

    template <typename State>
    static constexpr bool has_state = fsm_type::template has_state<State>;

    template <typename Event>
    static constexpr bool has_event = fsm_type::template has_event<Event>;

    // Constructors
    thread_safe_fsm() = default;

    explicit thread_safe_fsm(Context& ctx, Table table = Table{}) : fsm_(ctx, std::move(table)) {}

    explicit thread_safe_fsm(Context& ctx, InitialState initial, Table table = Table{})
        : fsm_(ctx, std::move(initial), std::move(table)) {}

    explicit thread_safe_fsm(Table table) : fsm_(std::move(table)) {}

    // Destructor: ensures background worker (if running) is cleanly stopped and
    // joined
    ~thread_safe_fsm() { stop_worker(); }

    // Non-copyable
    thread_safe_fsm(const thread_safe_fsm&) = delete;
    thread_safe_fsm& operator=(const thread_safe_fsm&) = delete;

    // Move operations
    thread_safe_fsm(thread_safe_fsm&&) = delete;
    thread_safe_fsm& operator=(thread_safe_fsm&&) = delete;

    // ========================================================================
    // Context & Observer Management
    // ========================================================================
    void set_context(Context& ctx) {
        std::scoped_lock lock(mutex_);
        fsm_.set_context(ctx);
    }

    void set_observer(typename fsm_type::observer_type observer) {
        std::scoped_lock lock(mutex_);
        fsm_.set_observer(std::move(observer));
    }

    // ========================================================================
    // Synchronous Dispatch (blocking with recursive mutex)
    // ========================================================================
    template <typename Event>
    bool send(const Event& event) {
        std::scoped_lock lock(mutex_);
        return fsm_.dispatch(event);
    }

    // ========================================================================
    // Asynchronous Queue (Thread-Safe Event Injection & Self-Posting)
    // ========================================================================
    template <typename Event>
    void post(Event event) {
        auto task = [evt = std::move(event)](fsm_type& machine) {
            machine.dispatch(evt);
        };
        {
            std::scoped_lock lock(mutex_);
            event_queue_.push(std::move(task));
        }
        cv_.notify_one();
    }

    // Process a single event from the queue in current thread. Returns true if an
    // event was processed.
    bool process_one() {
        event_handler task;
        {
            std::scoped_lock lock(mutex_);
            if (event_queue_.empty()) {
                return false;
            }
            task = std::move(event_queue_.front());
            event_queue_.pop();
            task(fsm_);
        }
        return true;
    }

    // Process all pending events in the queue in the current thread.
    std::size_t process_all() {
        std::size_t count = 0;
        while (process_one()) {
            ++count;
        }
        return count;
    }

    // Returns current number of pending events in queue
    [[nodiscard]] std::size_t pending_events() const {
        std::scoped_lock lock(mutex_);
        return event_queue_.size();
    }

    // Returns true if event queue is empty
    [[nodiscard]] bool is_queue_empty() const {
        std::scoped_lock lock(mutex_);
        return event_queue_.empty();
    }

    // Deferred events count under mutex lock
    [[nodiscard]] std::size_t deferred_count() const {
        std::scoped_lock lock(mutex_);
        return fsm_.deferred_count();
    }

    // Clear deferred events under mutex lock
    void clear_deferred_events() {
        std::scoped_lock lock(mutex_);
        fsm_.clear_deferred_events();
    }

    // ========================================================================
    // Background Worker Thread Management
    // ========================================================================
    void start_worker() {
        std::scoped_lock lock(mutex_);
        if (worker_running_) {
            return;
        }
        worker_running_ = true;
        worker_thread_ = std::thread([this]() { this->worker_loop(); });
    }

    void stop_worker() {
        {
            std::scoped_lock lock(mutex_);
            if (!worker_running_) {
                return;
            }
            worker_running_ = false;
        }
        cv_.notify_all();

        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    [[nodiscard]] bool is_worker_running() const { return worker_running_.load(); }

    // ========================================================================
    // Thread-safe State Inspection
    // ========================================================================
    template <typename State>
    [[nodiscard]] bool is_in_state() const {
        std::scoped_lock lock(mutex_);
        return fsm_.template is_in_state<State>();
    }

    // Thread-safe query of current state name
    [[nodiscard]] std::string current_state_name() const {
        std::scoped_lock lock(mutex_);
        return std::string(fsm_.current_state_name());
    }

    // Execute custom callback with current state under mutex lock
    template <typename Callable>
    auto with_state(Callable&& callable) const {
        std::scoped_lock lock(mutex_);
        return std::visit(std::forward<Callable>(callable), fsm_.get_current_state_variant());
    }

    // Execute custom callback directly with underlying FSM under mutex lock
    template <typename Callable>
    auto with_fsm(Callable&& callable) {
        std::scoped_lock lock(mutex_);
        return std::forward<Callable>(callable)(fsm_);
    }

    template <typename Event, typename Rep, typename Period>
    void post_delayed(Event event, std::chrono::duration<Rep, Period> delay) {
        auto deadline = std::chrono::steady_clock::now() + delay;
        auto task = [evt = std::move(event)](fsm_type& machine) {
            machine.dispatch(evt);
        };
        {
            std::scoped_lock lock(mutex_);
            timed_queue_.push(timed_event{deadline, std::move(task)});
        }
        cv_.notify_one();
    }

  private:
    struct timed_event {
        std::chrono::steady_clock::time_point deadline;
        event_handler task;

        bool operator>(const timed_event& other) const noexcept { return deadline > other.deadline; }
    };

    void worker_loop() {
        while (true) {
            event_handler task;
            {
                std::unique_lock<std::recursive_mutex> lock(mutex_);

                while (worker_running_ && event_queue_.empty() && timed_queue_.empty()) {
                    cv_.wait(lock);
                }

                while (worker_running_ && event_queue_.empty() && !timed_queue_.empty()) {
                    const auto now = std::chrono::steady_clock::now();
                    if (now >= timed_queue_.top().deadline) {
                        break;
                    }
                    cv_.wait_until(lock, timed_queue_.top().deadline);
                }

                if (!worker_running_ && event_queue_.empty() && timed_queue_.empty()) {
                    break;
                }

                const auto now = std::chrono::steady_clock::now();
                if (!timed_queue_.empty() && now >= timed_queue_.top().deadline) {
                    task = timed_queue_.top().task;
                    timed_queue_.pop();
                } else if (!event_queue_.empty()) {
                    task = std::move(event_queue_.front());
                    event_queue_.pop();
                }
            }

            if (task) {
                std::scoped_lock lock(mutex_);
                task(fsm_);
            }
        }
    }

    mutable std::recursive_mutex mutex_;
    std::condition_variable_any cv_;
    std::queue<event_handler> event_queue_;
    std::priority_queue<timed_event, std::vector<timed_event>, std::greater<>> timed_queue_;
    fsm_type fsm_;

    std::atomic<bool> worker_running_{false};
    std::thread worker_thread_;
};

}  // namespace fsm

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "fsm/runtime/cpp/async_event_queue.hpp"
#include "fsm/runtime/cpp/async_types.hpp"
#include "fsm/runtime/cpp/fsm.hpp"
#include "fsm/runtime/cpp/type_traits.hpp"

namespace fsm {

/**
 * @brief Thread-safe Finite State Machine wrapper supporting both synchronous and asynchronous dispatch.
 *
 * @details
 * `thread_safe_fsm` supports two primary operational models:
 *
 * 1. **Background Worker Model (Asynchronous)**:
 *    - Events pushed via `post()`, `post(evt, cb)`, and `post_async()` automatically start the background
 *      worker thread if not already active, ensuring `post_async().get()` never hangs.
 *    - `stop_worker()` signals termination, joins the worker thread safely, and systematically drains
 *      all pending and cascading events.
 *    - `request_stop()` allows asynchronous, non-blocking stop requests (e.g. from within callbacks/actions).
 *
 * 2. **Manual Polling / Game Loop Model (Synchronous / Single-Threaded Event Loop)**:
 *    - Push events via `enqueue()`.
 *    - Process them deterministically on the calling thread when desired using `process_one()` or `process_all()`.
 *
 * 3. **Synchronous Immediate Dispatch**:
 *    - `send()` is always available for immediate, synchronous dispatch from any thread.
 *
 * 4. **Thread-Safe Context Access**:
 *    - Use `with_context([](Context& ctx) { ... })` for synchronized, lock-protected access to the shared Context.
 */
template <typename Table, typename Context = no_context, typename InitialState = typename Table::initial_state>
class thread_safe_fsm {
  public:
    using fsm_type = fsm<Table, Context, InitialState, dynamic_observer>;
    using context_type = Context;
    using event_handler = std::function<void(fsm_type&)>;
    using unhandled_handler = ::fsm::unhandled_handler;
    using guard_rejected_handler = ::fsm::guard_rejected_handler;
    using deferred_handler = ::fsm::deferred_handler;
    using dispatch_failure_handler = ::fsm::dispatch_failure_handler;
    using exception_handler = ::fsm::exception_handler;

    // Static compile-time introspection
    static constexpr std::size_t state_count = fsm_type::state_count;
    static constexpr std::size_t transition_count = fsm_type::transition_count;
    static constexpr std::size_t event_count = fsm_type::event_count;

    template <typename State>
    static constexpr bool has_state = fsm_type::template has_state<State>;

    template <typename Event>
    static constexpr bool has_event = fsm_type::template has_event<Event>;

    // Default constructor (enabled only when Context is no_context)
    template <typename C = Context, typename = std::enable_if_t<std::is_same_v<C, no_context>>>
    thread_safe_fsm() {}

    explicit thread_safe_fsm(Context& ctx, Table table = Table{}) : fsm_(ctx, std::move(table)) {}

    explicit thread_safe_fsm(Context& ctx, InitialState initial, Table table = Table{})
        : fsm_(ctx, std::move(initial), std::move(table)) {}

    /**
     * @brief Destructor: ensures background worker (if running) is cleanly stopped and joined,
     * drains all remaining queue tasks before destruction, and purges queues.
     */
    ~thread_safe_fsm() {
        stop_worker();
        clear_queue();
    }

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
        std::scoped_lock lock(dispatch_mutex_);
        fsm_.set_context(ctx);
    }

    template <typename Callable>
    auto with_context(Callable&& callable) {
        std::scoped_lock lock(dispatch_mutex_);
        return std::forward<Callable>(callable)(fsm_.context());
    }

    template <typename Callable>
    auto with_context(Callable&& callable) const {
        std::scoped_lock lock(dispatch_mutex_);
        return std::forward<Callable>(callable)(fsm_.context());
    }

    [[nodiscard]] Context& context() noexcept { return fsm_.context(); }
    [[nodiscard]] const Context& context() const noexcept { return fsm_.context(); }

    [[nodiscard]] Context* get_context() noexcept { return fsm_.get_context(); }
    [[nodiscard]] const Context* get_context() const noexcept { return fsm_.get_context(); }

    void set_observer(std::function<void(const transition_info&)> observer) {
        std::scoped_lock lock(dispatch_mutex_);
        user_observer_ = std::move(observer);
        if (user_observer_) {
            fsm_.set_observer([this](const transition_info& info) { notification_buffer_.push_back(info); });
        } else {
            fsm_.set_observer(nullptr);
        }
    }

    void set_unhandled_handler(unhandled_handler handler) {
        std::scoped_lock lock(dispatch_mutex_);
        unhandled_handler_ = std::move(handler);
    }

    void set_guard_rejected_handler(guard_rejected_handler handler) {
        std::scoped_lock lock(dispatch_mutex_);
        guard_rejected_handler_ = std::move(handler);
    }

    void set_deferred_handler(deferred_handler handler) {
        std::scoped_lock lock(dispatch_mutex_);
        deferred_handler_ = std::move(handler);
    }

    void set_dispatch_failure_handler(dispatch_failure_handler handler) {
        std::scoped_lock lock(dispatch_mutex_);
        failure_handler_ = std::move(handler);
    }

    void set_exception_handler(exception_handler handler) {
        std::scoped_lock lock(dispatch_mutex_);
        exception_handler_ = std::move(handler);
    }

    [[nodiscard]] std::exception_ptr last_exception() const {
        std::scoped_lock lock(dispatch_mutex_);
        return last_exception_;
    }

    void clear_last_exception() {
        std::scoped_lock lock(dispatch_mutex_);
        last_exception_ = nullptr;
    }

    // ========================================================================
    // Synchronous Dispatch
    // ========================================================================
    template <typename Event>
    dispatch_result send(const Event& event) {
        dispatch_snapshot snap;
        try {
            snap = execute_dispatch_under_lock(event);
        } catch (...) {
            auto ex = std::current_exception();
            handle_exception_outside_lock(ex, get_exception_handler_copy());
            std::rethrow_exception(ex);
        }
        invoke_notifications_outside_lock(event, snap);
        return snap.result;
    }

    // ========================================================================
    // Asynchronous Queue
    // ========================================================================
    template <typename Event>
    void post(Event event) {
        if (!worker_running_.load() && !is_calling_from_worker_thread()) {
            start_worker();
        }
        enqueue(std::move(event));
    }

    template <typename Event, typename Callback>
    void post(Event event, Callback&& on_complete) {
        if (!worker_running_.load() && !is_calling_from_worker_thread()) {
            start_worker();
        }
        auto task = [this, evt = std::move(event), cb = std::forward<Callback>(on_complete)](fsm_type&) mutable {
            dispatch_snapshot snap;
            bool success = false;
            try {
                snap = execute_dispatch_under_lock(evt);
                invoke_notifications_outside_lock(evt, snap);
                success = true;
            } catch (...) {
                handle_exception_outside_lock(std::current_exception(), get_exception_handler_copy());
            }
            if (success) {
                try {
                    cb(snap.result);
                } catch (...) {
                    handle_exception_outside_lock(std::current_exception(), get_exception_handler_copy());
                }
            }
        };
        queue_.push(std::move(task));
    }

    template <typename Event>
    std::future<dispatch_result> post_async(Event event) {
        if (!worker_running_.load() && !is_calling_from_worker_thread()) {
            start_worker();
        }
        auto promise = std::make_shared<std::promise<dispatch_result>>();
        auto future = promise->get_future();
        auto task = [this, evt = std::move(event), p = promise](fsm_type&) {
            try {
                auto snap = execute_dispatch_under_lock(evt);
                invoke_notifications_outside_lock(evt, snap);
                p->set_value(snap.result);
            } catch (...) {
                auto ex = std::current_exception();
                handle_exception_outside_lock(ex, get_exception_handler_copy());
                p->set_exception(ex);
            }
        };
        queue_.push(std::move(task));
        return future;
    }

    template <typename Event>
    void enqueue(Event event) {
        auto task = [this, evt = std::move(event)](fsm_type&) {
            try {
                auto snap = execute_dispatch_under_lock(evt);
                invoke_notifications_outside_lock(evt, snap);
            } catch (...) {
                handle_exception_outside_lock(std::current_exception(), get_exception_handler_copy());
            }
        };
        {
            std::scoped_lock lock(queue_.mutex());
            if (is_stopping_.load(std::memory_order_acquire) && !is_calling_from_worker_thread() &&
                !is_calling_from_stopping_thread()) {
                return;
            }
            queue_.event_queue().push(std::move(task));
        }
        queue_.cv().notify_one();
    }

    // ========================================================================
    // Manual Polling (Single-Consumer Loop)
    // ========================================================================
    bool process_one() {
        if (worker_running_.load(std::memory_order_acquire)) {
            return false;
        }
        bool expected = false;
        if (!is_polling_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return false;
        }
        struct PollingGuard {
            std::atomic<bool>& flag;
            ~PollingGuard() { flag.store(false, std::memory_order_release); }
        } guard{is_polling_};

        event_handler task;
        if (!queue_.try_pop(task)) {
            return false;
        }
        if (task) {
            try {
                task(fsm_);
            } catch (...) {
                handle_exception_outside_lock(std::current_exception(), get_exception_handler_copy());
                return false;
            }
        }
        return true;
    }

    std::size_t process_all() {
        if (worker_running_.load(std::memory_order_acquire)) {
            return 0;
        }
        bool expected = false;
        if (!is_polling_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return 0;
        }
        struct PollingGuard {
            std::atomic<bool>& flag;
            ~PollingGuard() { flag.store(false, std::memory_order_release); }
        } guard{is_polling_};

        std::size_t total = 0;
        while (true) {
            auto batch = queue_.drain_ready_batch();
            if (batch.empty()) {
                break;
            }
            for (auto& task : batch) {
                if (task) {
                    try {
                        task(fsm_);
                    } catch (...) {
                        handle_exception_outside_lock(std::current_exception(), get_exception_handler_copy());
                    }
                }
            }
            total += batch.size();
        }
        return total;
    }

    [[nodiscard]] std::size_t pending_events() const { return queue_.size(); }
    [[nodiscard]] bool is_queue_empty() const { return queue_.empty(); }
    void clear_queue() { queue_.clear(); }

    [[nodiscard]] std::size_t deferred_count() const {
        std::scoped_lock lock(dispatch_mutex_);
        return fsm_.deferred_count();
    }

    void clear_deferred_events() {
        std::scoped_lock lock(dispatch_mutex_);
        fsm_.clear_deferred_events();
    }

    [[nodiscard]] bool is_calling_from_worker_thread() const noexcept {
        const auto id = worker_thread_id_.load(std::memory_order_acquire);
        return id != std::thread::id{} && std::this_thread::get_id() == id;
    }

    [[nodiscard]] bool is_calling_from_stopping_thread() const noexcept {
        const auto id = stopping_thread_id_.load(std::memory_order_acquire);
        return id != std::thread::id{} && std::this_thread::get_id() == id;
    }

    // ========================================================================
    // Background Worker Thread Management
    // ========================================================================
    void start_worker() {
        if (is_calling_from_worker_thread()) {
            return;
        }
        std::scoped_lock lock(lifecycle_mutex_);
        {
            std::scoped_lock q_lock(queue_.mutex());
            if (worker_running_ || is_stopping_) {
                return;
            }
            if (worker_thread_.joinable()) {
                worker_thread_.join();
            }
            worker_running_ = true;
        }
        worker_thread_ = std::thread([this]() { this->worker_loop(); });
    }

    void request_stop() noexcept {
        {
            std::scoped_lock q_lock(queue_.mutex());
            if (!worker_running_) {
                return;
            }
            worker_running_ = false;
        }
        queue_.cv().notify_all();
    }

    void stop_worker() {
        if (is_calling_from_worker_thread()) {
            request_stop();
            return;
        }

        std::scoped_lock lock(lifecycle_mutex_);
        stopping_thread_id_.store(std::this_thread::get_id(), std::memory_order_release);
        {
            std::scoped_lock q_lock(queue_.mutex());
            worker_running_ = false;
            is_stopping_ = true;
        }
        queue_.cv().notify_all();

        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }

        // Systematically drain all pending and chained events until queue is empty
        process_all();

        {
            std::scoped_lock q_lock(queue_.mutex());
            is_stopping_ = false;
            stopping_thread_id_.store(std::thread::id{}, std::memory_order_release);
        }
    }

    [[nodiscard]] bool is_worker_running() const { return worker_running_.load(); }

    // ========================================================================
    // Thread-safe State Inspection
    // ========================================================================
    template <typename State>
    [[nodiscard]] bool is_in_state() const {
        std::scoped_lock lock(dispatch_mutex_);
        return fsm_.template is_in_state<State>();
    }

    [[nodiscard]] std::string current_state_name() const {
        std::scoped_lock lock(dispatch_mutex_);
        return std::string(fsm_.current_state_name());
    }

    template <typename Callable>
    auto with_state(Callable&& callable) const {
        std::scoped_lock lock(dispatch_mutex_);
        return std::visit(std::forward<Callable>(callable), fsm_.get_current_state_variant());
    }

    template <typename Callable>
    auto with_fsm(Callable&& callable) {
        std::scoped_lock lock(dispatch_mutex_);
        return std::forward<Callable>(callable)(fsm_);
    }

    template <typename Event, typename Rep, typename Period>
    void post_delayed(Event event, std::chrono::duration<Rep, Period> delay) {
        if (!worker_running_.load() && !is_calling_from_worker_thread()) {
            start_worker();
        }
        auto deadline = std::chrono::steady_clock::now() + delay;
        auto task = [this, evt = std::move(event)](fsm_type&) {
            try {
                auto snap = execute_dispatch_under_lock(evt);
                invoke_notifications_outside_lock(evt, snap);
            } catch (...) {
                handle_exception_outside_lock(std::current_exception(), get_exception_handler_copy());
            }
        };
        queue_.push_timed(deadline, std::move(task));
    }

  private:
    template <typename Event>
    dispatch_snapshot execute_dispatch_under_lock(const Event& evt) {
        dispatch_snapshot snap;
        std::scoped_lock lock(dispatch_mutex_);
        notification_buffer_.clear();
        snap.result = fsm_.dispatch(evt);
        snap.state_name = std::string(fsm_.current_state_name());
        snap.notifications = std::move(notification_buffer_);
        snap.unhandled_h = unhandled_handler_;
        snap.guard_rejected_h = guard_rejected_handler_;
        snap.deferred_h = deferred_handler_;
        snap.failure_h = failure_handler_;
        snap.exception_h = exception_handler_;
        snap.observer_h = user_observer_;
        return snap;
    }

    template <typename Event>
    void invoke_notifications_outside_lock(const Event& evt, const dispatch_snapshot& snap) {
        if (snap.observer_h) {
            for (const auto& info : snap.notifications) {
                try {
                    snap.observer_h(info);
                } catch (...) {
                    handle_exception_outside_lock(std::current_exception(), snap.exception_h);
                }
            }
        }
        if (snap.result.is_unhandled() && snap.unhandled_h) {
            try {
                snap.unhandled_h(get_event_name(evt), snap.state_name);
            } catch (...) {
                handle_exception_outside_lock(std::current_exception(), snap.exception_h);
            }
        } else if (snap.result.is_guard_rejected() && snap.guard_rejected_h) {
            try {
                snap.guard_rejected_h(get_event_name(evt), snap.state_name);
            } catch (...) {
                handle_exception_outside_lock(std::current_exception(), snap.exception_h);
            }
        } else if (snap.result.is_deferred() && snap.deferred_h) {
            try {
                snap.deferred_h(get_event_name(evt), snap.state_name);
            } catch (...) {
                handle_exception_outside_lock(std::current_exception(), snap.exception_h);
            }
        }
        if (!snap.result.is_ok() && snap.failure_h) {
            try {
                snap.failure_h(get_event_name(evt), snap.state_name, snap.result.status);
            } catch (...) {
                handle_exception_outside_lock(std::current_exception(), snap.exception_h);
            }
        }
    }

    void handle_exception_outside_lock(std::exception_ptr ex, const exception_handler& local_h) {
        {
            std::scoped_lock lock(dispatch_mutex_);
            last_exception_ = ex;
        }
        if (local_h) {
            try {
                local_h(ex);
            } catch (...) {
            }
        }
    }

    exception_handler get_exception_handler_copy() {
        std::scoped_lock lock(dispatch_mutex_);
        return exception_handler_;
    }

    void worker_loop() {
        worker_thread_id_.store(std::this_thread::get_id(), std::memory_order_release);
        while (true) {
            event_handler task;
            {
                std::unique_lock<std::mutex> lock(queue_.mutex());

                while (worker_running_ && queue_.event_queue().empty() && queue_.timed_queue().empty()) {
                    queue_.cv().wait(lock);
                }

                while (worker_running_ && queue_.event_queue().empty() && !queue_.timed_queue().empty()) {
                    const auto now = std::chrono::steady_clock::now();
                    if (now >= queue_.timed_queue().top().deadline) {
                        break;
                    }
                    queue_.cv().wait_until(lock, queue_.timed_queue().top().deadline);
                }

                if (!worker_running_ && queue_.event_queue().empty() && queue_.timed_queue().empty()) {
                    break;
                }

                const auto now = std::chrono::steady_clock::now();
                if (!queue_.timed_queue().empty() && now >= queue_.timed_queue().top().deadline) {
                    task = queue_.timed_queue().top().task;
                    queue_.timed_queue().pop();
                } else if (!queue_.event_queue().empty()) {
                    task = std::move(queue_.event_queue().front());
                    queue_.event_queue().pop();
                }
            }

            if (task) {
                try {
                    task(fsm_);
                } catch (...) {
                    handle_exception_outside_lock(std::current_exception(), get_exception_handler_copy());
                }
            }
        }
        worker_thread_id_.store(std::thread::id{}, std::memory_order_release);
    }

    mutable async_event_queue<event_handler> queue_;
    mutable std::recursive_mutex lifecycle_mutex_;
    mutable std::recursive_mutex dispatch_mutex_;
    fsm_type fsm_;
    std::function<void(const transition_info&)> user_observer_{};
    std::vector<transition_info> notification_buffer_{};
    unhandled_handler unhandled_handler_{};
    guard_rejected_handler guard_rejected_handler_{};
    deferred_handler deferred_handler_{};
    dispatch_failure_handler failure_handler_{};
    exception_handler exception_handler_{};
    std::exception_ptr last_exception_{nullptr};

    std::atomic<bool> worker_running_{false};
    std::atomic<bool> is_stopping_{false};
    std::atomic<bool> is_polling_{false};
    std::atomic<std::thread::id> worker_thread_id_{};
    std::atomic<std::thread::id> stopping_thread_id_{};
    std::thread worker_thread_;
};

}  // namespace fsm

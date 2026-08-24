#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "fsm/fsm.hpp"
#include "fsm/type_traits.hpp"

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
    using unhandled_handler = std::function<void(std::string_view /*event*/, std::string_view /*state*/)>;
    using guard_rejected_handler = std::function<void(std::string_view /*event*/, std::string_view /*state*/)>;
    using deferred_handler = std::function<void(std::string_view /*event*/, std::string_view /*state*/)>;
    using dispatch_failure_handler =
        std::function<void(std::string_view /*event*/, std::string_view /*state*/, dispatch_status /*status*/)>;
    using exception_handler = std::function<void(std::exception_ptr)>;

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
     *
     * @note Lifecycle Policy: `thread_safe_fsm` must be destroyed from an external managing thread.
     * If an asynchronous shutdown from within an action or observer callback is needed, invoke `request_stop()`,
     * and let the external owner perform destruction once the worker has terminated.
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

    /**
     * @brief Executes a user callback with exclusive locked access to the Context.
     *
     * Recommended method for thread-safe modification or querying of Context data while
     * the FSM or worker thread is active.
     */
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

    /**
     * @warning (Unsynchronized Access) Direct Context access bypasses state machine synchronization locks.
     * Use `with_context([](Context& ctx) { ... })` for thread-safe access when worker or FSM is active.
     */
    [[nodiscard]] Context& context() noexcept { return fsm_.context(); }
    [[nodiscard]] const Context& context() const noexcept { return fsm_.context(); }

    [[nodiscard]] Context* get_context() noexcept { return fsm_.get_context(); }
    [[nodiscard]] const Context* get_context() const noexcept { return fsm_.get_context(); }

    /**
     * @brief Registers an observer callback.
     * @note Handlers are updated atomically under lock. Any in-flight dispatch retains its existing snapshot;
     * the new observer takes effect for all subsequent event dispatches.
     */
    void set_observer(std::function<void(const transition_info&)> observer) {
        std::scoped_lock lock(dispatch_mutex_);
        user_observer_ = std::move(observer);
        if (user_observer_) {
            fsm_.set_observer([this](const transition_info& info) { notification_buffer_.push_back(info); });
        } else {
            fsm_.set_observer(nullptr);
        }
    }

    /** @note Applied atomically; active in-flight dispatch uses its initial snapshot; applies to subsequent dispatches.
     */
    void set_unhandled_handler(unhandled_handler handler) {
        std::scoped_lock lock(dispatch_mutex_);
        unhandled_handler_ = std::move(handler);
    }

    /** @note Applied atomically; active in-flight dispatch uses its initial snapshot; applies to subsequent dispatches.
     */
    void set_guard_rejected_handler(guard_rejected_handler handler) {
        std::scoped_lock lock(dispatch_mutex_);
        guard_rejected_handler_ = std::move(handler);
    }

    /** @note Applied atomically; active in-flight dispatch uses its initial snapshot; applies to subsequent dispatches.
     */
    void set_deferred_handler(deferred_handler handler) {
        std::scoped_lock lock(dispatch_mutex_);
        deferred_handler_ = std::move(handler);
    }

    /** @note Applied atomically; active in-flight dispatch uses its initial snapshot; applies to subsequent dispatches.
     */
    void set_dispatch_failure_handler(dispatch_failure_handler handler) {
        std::scoped_lock lock(dispatch_mutex_);
        failure_handler_ = std::move(handler);
    }

    /** @note Applied atomically; active in-flight dispatch uses its initial snapshot; applies to subsequent dispatches.
     */
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
    // Synchronous Dispatch (Deadlock-Free Reentrant Lock)
    // ========================================================================
    /**
     * @brief Synchronously dispatches an event to the state machine.
     *
     * The internal state mutation is executed atomically under `dispatch_mutex_`.
     * All observers and handlers are invoked outside the lock to prevent deadlock
     * and minimize lock contention.
     *
     * If an action, hook, or observer throws an exception, it is recorded in
     * `last_exception()`, forwarded to `exception_handler` (if set), and rethrown
     * to the caller.
     */
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
    // Asynchronous Queue (Thread-Safe Event Injection & Self-Posting)
    // ========================================================================
    /**
     * @brief Asynchronously enqueues an event (fire-and-forget).
     *
     * Automatically ensures the background worker thread is running to process the event.
     *
     * @note Exception Handling Policy: In fire-and-forget `post()`, if an action or observer throws,
     * the exception is caught to keep the worker thread running, recorded in `last_exception()`,
     * and forwarded to `set_exception_handler(handler)`. Callers interested in per-call exceptions
     * should use `post_async()`.
     */
    template <typename Event>
    void post(Event event) {
        if (!worker_running_.load() && !is_calling_from_worker_thread()) {
            start_worker();
        }
        enqueue(std::move(event));
    }

    /**
     * @brief Asynchronously enqueues an event with a completion callback.
     *
     * Automatically ensures the background worker thread is running.
     * The callback is invoked outside the lock after dispatch and observer notifications complete.
     */
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
        {
            std::scoped_lock lock(queue_mutex_);
            event_queue_.push(std::move(task));
        }
        cv_.notify_one();
    }

    /**
     * @brief Asynchronously enqueues an event and returns a std::future for result tracking.
     *
     * Automatically ensures the background worker thread is running so the future never hangs.
     *
     * @note Exception Handling Policy: If an action or observer throws during dispatch, the exception
     * is set on the promise (`std::promise::set_exception`) and rethrown when calling `future.get()`,
     * as well as recorded in `last_exception()` and notified to `set_exception_handler()`.
     */
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
        {
            std::scoped_lock lock(queue_mutex_);
            event_queue_.push(std::move(task));
        }
        cv_.notify_one();
        return future;
    }

    /**
     * @brief Enqueues an event without automatically starting the background worker thread.
     *
     * Useful for Manual Polling Mode where events are queued and manually processed via `process_one()` or
     * `process_all()`. Rejects new external events if the state machine is currently undergoing shutdown.
     */
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
            std::scoped_lock lock(queue_mutex_);
            if (is_stopping_.load(std::memory_order_acquire) && !is_calling_from_worker_thread() &&
                !is_calling_from_stopping_thread()) {
                return;
            }
            event_queue_.push(std::move(task));
        }
        cv_.notify_one();
    }

    /**
     * @brief Processes a single pending event in the current thread (Manual Polling Mode).
     *
     * @note Single-Consumer Polling Contract: process_one() and process_all() must be called from a single consumer
     * thread (e.g. main/game loop). Concurrent consumer invocations are rejected. Rejects (returns false) if the
     * background worker is currently active to prevent concurrency conflicts.
     *
     * @return true if an event was processed, false if the queue was empty, worker is active, or polling is contested.
     */
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
        {
            std::scoped_lock lock(queue_mutex_);
            const auto now = std::chrono::steady_clock::now();
            if (!timed_queue_.empty() && now >= timed_queue_.top().deadline) {
                task = timed_queue_.top().task;
                timed_queue_.pop();
            } else if (!event_queue_.empty()) {
                task = std::move(event_queue_.front());
                event_queue_.pop();
            } else {
                return false;
            }
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

    /**
     * @brief Processes all pending events in the queue in the current thread (Manual Polling Mode).
     *
     * Drains all pending events as well as any cascading events queued during dispatch.
     *
     * @note Single-Consumer Polling Contract: process_one() and process_all() must be called from a single consumer
     * thread. Rejects (returns 0) if the background worker is active or another thread is actively polling.
     *
     * @return The number of processed events.
     */
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
            std::vector<event_handler> batch;
            {
                std::scoped_lock lock(queue_mutex_);
                const auto now = std::chrono::steady_clock::now();
                while (!timed_queue_.empty() && now >= timed_queue_.top().deadline) {
                    batch.push_back(timed_queue_.top().task);
                    timed_queue_.pop();
                }
                while (!event_queue_.empty()) {
                    batch.push_back(std::move(event_queue_.front()));
                    event_queue_.pop();
                }
            }

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

    // Returns current number of pending events in queue
    [[nodiscard]] std::size_t pending_events() const {
        std::scoped_lock lock(queue_mutex_);
        return event_queue_.size() + timed_queue_.size();
    }

    // Returns true if event queue is empty
    [[nodiscard]] bool is_queue_empty() const {
        std::scoped_lock lock(queue_mutex_);
        return event_queue_.empty() && timed_queue_.empty();
    }

    // Clear all pending events in the queue
    void clear_queue() {
        std::scoped_lock lock(queue_mutex_);
        std::queue<event_handler> empty_q;
        std::swap(event_queue_, empty_q);
        std::priority_queue<timed_event, std::vector<timed_event>, std::greater<>> empty_timed;
        std::swap(timed_queue_, empty_timed);
    }

    // Deferred events count under dispatch lock
    [[nodiscard]] std::size_t deferred_count() const {
        std::scoped_lock lock(dispatch_mutex_);
        return fsm_.deferred_count();
    }

    // Clear deferred events under dispatch lock
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
            std::scoped_lock q_lock(queue_mutex_);
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

    /**
     * @brief Requests the worker to stop gracefully without joining.
     * Safe to call from any thread, including from actions/observers within the worker thread itself.
     */
    void request_stop() noexcept {
        {
            std::scoped_lock q_lock(queue_mutex_);
            if (!worker_running_) {
                return;
            }
            worker_running_ = false;
        }
        cv_.notify_all();
    }

    /**
     * @brief Stops the background worker thread, waits for it to finish (join), and drains remaining events.
     *
     * @note If called from within the worker thread itself, gracefully delegates to `request_stop()`
     * to avoid self-join deadlocks or use-after-free.
     */
    void stop_worker() {
        if (is_calling_from_worker_thread()) {
            request_stop();
            return;
        }

        std::scoped_lock lock(lifecycle_mutex_);
        stopping_thread_id_.store(std::this_thread::get_id(), std::memory_order_release);
        {
            std::scoped_lock q_lock(queue_mutex_);
            worker_running_ = false;
            is_stopping_ = true;
        }
        cv_.notify_all();

        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }

        // Systematically drain all pending and chained events until queue is empty
        process_all();

        {
            std::scoped_lock q_lock(queue_mutex_);
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

    // Thread-safe query of current state name
    [[nodiscard]] std::string current_state_name() const {
        std::scoped_lock lock(dispatch_mutex_);
        return std::string(fsm_.current_state_name());
    }

    // Execute custom callback with current state under dispatch lock
    template <typename Callable>
    auto with_state(Callable&& callable) const {
        std::scoped_lock lock(dispatch_mutex_);
        return std::visit(std::forward<Callable>(callable), fsm_.get_current_state_variant());
    }

    // Execute custom callback directly with underlying FSM under dispatch lock
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
        {
            std::scoped_lock lock(queue_mutex_);
            timed_queue_.push(timed_event{deadline, std::move(task)});
        }
        cv_.notify_one();
    }

  private:
    struct dispatch_snapshot {
        dispatch_result result{dispatch_status::unhandled};
        std::string state_name{};
        std::vector<transition_info> notifications{};
        unhandled_handler unhandled_h{};
        guard_rejected_handler guard_rejected_h{};
        deferred_handler deferred_h{};
        dispatch_failure_handler failure_h{};
        exception_handler exception_h{};
        std::function<void(const transition_info&)> observer_h{};
    };

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

    struct timed_event {
        std::chrono::steady_clock::time_point deadline;
        event_handler task;

        bool operator>(const timed_event& other) const noexcept { return deadline > other.deadline; }
    };

    void worker_loop() {
        worker_thread_id_.store(std::this_thread::get_id(), std::memory_order_release);
        while (true) {
            event_handler task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);

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
                try {
                    task(fsm_);
                } catch (...) {
                    handle_exception_outside_lock(std::current_exception(), get_exception_handler_copy());
                }
            }
        }
        worker_thread_id_.store(std::thread::id{}, std::memory_order_release);
    }

    mutable std::mutex queue_mutex_;
    mutable std::recursive_mutex lifecycle_mutex_;
    mutable std::recursive_mutex dispatch_mutex_;
    std::condition_variable cv_;
    std::queue<event_handler> event_queue_;
    std::priority_queue<timed_event, std::vector<timed_event>, std::greater<>> timed_queue_;
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

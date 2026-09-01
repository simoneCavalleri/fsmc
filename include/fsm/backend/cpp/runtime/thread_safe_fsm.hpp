#pragma once

#include <atomic>
#include <chrono>
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

#include "fsm/backend/cpp/runtime/async_event_queue.hpp"
#include "fsm/backend/cpp/runtime/async_types.hpp"
#include "fsm/backend/cpp/runtime/detail/notification_dispatcher.hpp"
#include "fsm/backend/cpp/runtime/detail/reentrancy_tracker.hpp"
#include "fsm/backend/cpp/runtime/fsm.hpp"
#include "fsm/backend/cpp/runtime/traits/dispatch_result.hpp"
#include "fsm/backend/cpp/runtime/traits/observer_traits.hpp"
#include "fsm/backend/cpp/runtime/traits/reflection.hpp"
#include "fsm/backend/cpp/runtime/type_traits.hpp"

namespace fsm {

/**
 * @brief Thread-Safe Concurrent Active-Object / Event-Queue Wrapper around `fsm::fsm`.
 *
 * Provides a concurrent wrapper around `fsm::fsm` supporting:
 * 1. **Lock-Free / Thread-Safe Event Queueing**:
 *    - `post()` / `enqueue()` enqueues events asynchronously in O(1) time.
 * 2. **Optional Background Worker**:
 *    - `start_worker()` launches a dedicated event loop thread.
 * 3. **Synchronous Immediate Dispatch**:
 *    - `send()` is available for immediate, synchronous dispatch from any thread.
 *
 * ### Not zero-heap
 * Unlike `fsm<>` and `spsc_fsm<>`, this wrapper's queueing API (`post()`,
 * `enqueue()`, `post_async()`, `post_delayed()`) type-erases each queued
 * event into a `std::function`, which allocates on the heap for any capture
 * larger than the implementation's small-buffer optimization. If you need a
 * genuinely zero-heap asynchronous FSM (e.g. for an ISR producer), use
 * `spsc_fsm` instead, whose `enqueue()` writes directly into a lock-free
 * fixed-capacity ring buffer.
 *
 * ### Reentrancy
 * Calling `send()` (or anything that ultimately calls `execute_dispatch_under_lock`)
 * *from within* an Action or Guard that is itself running as part of an
 * in-flight dispatch, **on the same thread**, is safely deferred:
 * the reentrant call is queued instead of executing inline, returning
 * `dispatch_status::deferred`. Once the outermost dispatch finishes,
 * `send()` automatically drains the queue so the reentrant event is processed
 * promptly without invalidating stack state references.
 */
template <typename Table, typename InPorts = no_ports, typename OutPorts = no_ports, typename Registers = no_registers,
          typename Services = no_services, typename InitialState = typename Table::initial_state,
          std::size_t DeferredCapacity = 16>
class thread_safe_fsm {
  public:
    using fsm_type =
        fsm<Table, InPorts, OutPorts, Registers, Services, InitialState, dynamic_observer, DeferredCapacity>;
    using in_ports_type = InPorts;
    using out_ports_type = OutPorts;
    using registers_type = Registers;
    using services_type = Services;
    using initial_state_type = InitialState;
    using event_handler = std::function<void(fsm_type&)>;
    using unhandled_handler = ::fsm::unhandled_handler;
    using guard_rejected_handler = ::fsm::guard_rejected_handler;
    using deferred_handler = ::fsm::deferred_handler;
    using dispatch_failure_handler = ::fsm::dispatch_failure_handler;
    using exception_handler = ::fsm::exception_handler;

    thread_safe_fsm() {
        fsm_.set_observer([this](const transition_info& info) { notification_buffer_.push_back(info); });
    }

    explicit thread_safe_fsm(services_type& srv) : fsm_(srv) {
        fsm_.set_observer([this](const transition_info& info) { notification_buffer_.push_back(info); });
    }

    explicit thread_safe_fsm(registers_type reg) : fsm_(std::move(reg)) {
        fsm_.set_observer([this](const transition_info& info) { notification_buffer_.push_back(info); });
    }

    thread_safe_fsm(registers_type reg, services_type& srv) : fsm_(std::move(reg), srv) {
        fsm_.set_observer([this](const transition_info& info) { notification_buffer_.push_back(info); });
    }

    ~thread_safe_fsm() {
        stop_worker();
        process_all();
    }

    thread_safe_fsm(const thread_safe_fsm&) = delete;
    thread_safe_fsm& operator=(const thread_safe_fsm&) = delete;
    thread_safe_fsm(thread_safe_fsm&&) = delete;
    thread_safe_fsm& operator=(thread_safe_fsm&&) = delete;

    // Diagnostic Callbacks
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

    template <typename Callback>
    void set_observer(Callback&& observer) {
        std::scoped_lock lock(dispatch_mutex_);
        user_observer_ = std::forward<Callback>(observer);
    }

    void clear_observer() {
        std::scoped_lock lock(dispatch_mutex_);
        user_observer_ = nullptr;
    }

    [[nodiscard]] std::exception_ptr last_exception() const {
        if (reentrancy_.is_reentrant_call()) {
            return last_exception_;
        }
        std::scoped_lock lock(dispatch_mutex_);
        return last_exception_;
    }

    void clear_last_exception() {
        if (reentrancy_.is_reentrant_call()) {
            last_exception_ = nullptr;
            return;
        }
        std::scoped_lock lock(dispatch_mutex_);
        last_exception_ = nullptr;
    }

    // State & Register Access
    [[nodiscard]] registers_type& registers() noexcept { return fsm_.registers(); }

    [[nodiscard]] const registers_type& registers() const noexcept { return fsm_.registers(); }

    [[nodiscard]] registers_type snapshot_registers() const {
        if (reentrancy_.is_reentrant_call()) {
            return fsm_.registers();
        }
        std::scoped_lock lock(dispatch_mutex_);
        return fsm_.registers();
    }

    void update_registers(registers_type reg) {
        if (reentrancy_.is_reentrant_call()) {
            fsm_.registers() = std::move(reg);
            return;
        }
        std::scoped_lock lock(dispatch_mutex_);
        fsm_.registers() = std::move(reg);
    }

    template <typename Callable>
    auto with_registers(Callable&& fn) const {
        if (reentrancy_.is_reentrant_call()) {
            return fn(std::as_const(fsm_.registers()));
        }
        std::scoped_lock lock(dispatch_mutex_);
        return fn(std::as_const(fsm_.registers()));
    }

    template <typename Callable>
    auto with_registers(Callable&& fn) {
        if (reentrancy_.is_reentrant_call()) {
            return fn(fsm_.registers());
        }
        std::scoped_lock lock(dispatch_mutex_);
        return fn(fsm_.registers());
    }

    template <typename State>
    [[nodiscard]] bool is_in_state() const noexcept {
        if (reentrancy_.is_reentrant_call()) {
            return fsm_.template is_in_state<State>();
        }
        std::scoped_lock lock(dispatch_mutex_);
        return fsm_.template is_in_state<State>();
    }

    template <typename State>
    [[nodiscard]] bool is_in() const noexcept {
        return is_in_state<State>();
    }

    [[nodiscard]] std::string_view current_state_name() const {
        if (reentrancy_.is_reentrant_call()) {
            return fsm_.current_state_name();
        }
        std::scoped_lock lock(dispatch_mutex_);
        return fsm_.current_state_name();
    }

    [[nodiscard]] std::size_t deferred_count() const noexcept {
        if (reentrancy_.is_reentrant_call()) {
            return fsm_.deferred_count();
        }
        std::scoped_lock lock(dispatch_mutex_);
        return fsm_.deferred_count();
    }

    void clear_deferred_events() noexcept {
        if (reentrancy_.is_reentrant_call()) {
            fsm_.clear_deferred_events();
            return;
        }
        std::scoped_lock lock(dispatch_mutex_);
        fsm_.clear_deferred_events();
    }

    template <typename Callable>
    auto with_state(Callable&& fn) const {
        if (reentrancy_.is_reentrant_call()) {
            return fsm_.with_state(std::forward<Callable>(fn));
        }
        std::scoped_lock lock(dispatch_mutex_);
        return fsm_.with_state(std::forward<Callable>(fn));
    }

    template <typename Callable>
    auto with_state(Callable&& fn) {
        if (reentrancy_.is_reentrant_call()) {
            return fsm_.with_state(std::forward<Callable>(fn));
        }
        std::scoped_lock lock(dispatch_mutex_);
        return fsm_.with_state(std::forward<Callable>(fn));
    }

    [[nodiscard]] std::size_t current_state_index() const {
        if (reentrancy_.is_reentrant_call()) {
            return fsm_.get_current_state_variant().index();
        }
        std::scoped_lock lock(dispatch_mutex_);
        return fsm_.get_current_state_variant().index();
    }

    // ========================================================================
    // Synchronous Dispatch
    // ========================================================================

    template <typename Event>
    dispatch_result send(const Event& event) {
        auto snap = execute_dispatch_under_lock(event);
        detail::invoke_notifications_outside_lock(event, snap, last_exception_, dispatch_mutex_);
        drain_reentrant_queue_if_outermost();
        return snap.result;
    }

    template <typename Event>
    dispatch_result send(const Event& event, const in_ports_type& in, out_ports_type& out) {
        if (reentrancy_.is_reentrant_call()) {
            return queue_reentrant_event(event);
        }
        detail::dispatch_snapshot snap;
        {
            std::scoped_lock lock(dispatch_mutex_);
            detail::reentrancy_tracker::depth_guard depth_guard(reentrancy_);
            notification_buffer_.clear();
            snap.result = fsm_.dispatch(event, in, out);
            snap.state_name = std::string(fsm_.current_state_name());
            snap.notifications = std::move(notification_buffer_);
            snap.unhandled_h = unhandled_handler_;
            snap.guard_rejected_h = guard_rejected_handler_;
            snap.deferred_h = deferred_handler_;
            snap.failure_h = failure_handler_;
            snap.exception_h = exception_handler_;
            snap.observer_h = user_observer_;
        }
        detail::invoke_notifications_outside_lock(event, snap, last_exception_, dispatch_mutex_);
        drain_reentrant_queue_if_outermost();
        return snap.result;
    }

    // ========================================================================
    // Sampled Synchronous Control Loop Step
    // ========================================================================

    step_result step(const in_ports_type& in, out_ports_type& out, services_type& srv) {
        std::scoped_lock lock(dispatch_mutex_);
        return fsm_.step(in, out, srv);
    }

    template <typename DurationRep>
    step_result step(DurationRep dt, const in_ports_type& in, out_ports_type& out, services_type& srv) {
        std::scoped_lock lock(dispatch_mutex_);
        return fsm_.step(dt, in, out, srv);
    }

    step_result step(const in_ports_type& in, out_ports_type& out) {
        std::scoped_lock lock(dispatch_mutex_);
        return fsm_.step(in, out);
    }

    template <typename DurationRep>
    step_result step(DurationRep dt, const in_ports_type& in, out_ports_type& out) {
        std::scoped_lock lock(dispatch_mutex_);
        return fsm_.step(dt, in, out);
    }

    step_result step() {
        std::scoped_lock lock(dispatch_mutex_);
        return fsm_.step();
    }

    template <typename DurationRep>
    step_result step(DurationRep dt) {
        std::scoped_lock lock(dispatch_mutex_);
        return fsm_.step(dt);
    }

    // ========================================================================
    // Asynchronous Queueing
    // ========================================================================

    template <typename Event>
    void post(Event event) {
        enqueue(std::move(event));
    }

    template <typename Event, typename Callback>
    void post(Event event, Callback&& callback) {
        if (!worker_running_.load() && !is_calling_from_worker_thread()) {
            start_worker();
        }
        auto task = [this, evt = std::move(event), cb = std::forward<Callback>(callback)](fsm_type&) mutable {
            try {
                auto snap = execute_dispatch_under_lock(evt);
                detail::invoke_notifications_outside_lock(evt, snap, last_exception_, dispatch_mutex_);
                cb(snap.result);
            } catch (...) {
                detail::handle_exception_outside_lock(std::current_exception(), get_exception_handler_copy(),
                                                      last_exception_, dispatch_mutex_);
            }
        };
        queue_.push(std::move(task));
    }

    template <typename Event>
    [[nodiscard]] std::future<dispatch_result> post_async(Event event) {
        if (!worker_running_.load() && !is_calling_from_worker_thread()) {
            start_worker();
        }
        auto promise = std::make_shared<std::promise<dispatch_result>>();
        auto future = promise->get_future();
        auto task = [this, evt = std::move(event), p = promise](fsm_type&) mutable {
            try {
                auto snap = execute_dispatch_under_lock(evt);
                detail::invoke_notifications_outside_lock(evt, snap, last_exception_, dispatch_mutex_);
                p->set_value(snap.result);
            } catch (...) {
                p->set_exception(std::current_exception());
                detail::handle_exception_outside_lock(std::current_exception(), get_exception_handler_copy(),
                                                      last_exception_, dispatch_mutex_);
            }
        };
        queue_.push(std::move(task));
        return future;
    }

    template <typename Event, typename Rep, typename Period>
    void post_delayed(Event event, std::chrono::duration<Rep, Period> delay, bool cancel_if_state_changes = false) {
        if (!worker_running_.load() && !is_calling_from_worker_thread()) {
            start_worker();
        }
        auto deadline = std::chrono::steady_clock::now() + delay;
        const auto scheduled_state = cancel_if_state_changes ? current_state_index() : static_cast<std::size_t>(-1);
        auto task = [this, evt = std::move(event), scheduled_state, cancel_if_state_changes](fsm_type&) {
            try {
                if (cancel_if_state_changes && current_state_index() != scheduled_state) {
                    return;  // Invalidate stale timeout
                }
                auto snap = execute_dispatch_under_lock(evt);
                detail::invoke_notifications_outside_lock(evt, snap, last_exception_, dispatch_mutex_);
            } catch (...) {
                detail::handle_exception_outside_lock(std::current_exception(), get_exception_handler_copy(),
                                                      last_exception_, dispatch_mutex_);
            }
        };
        queue_.push_timed(deadline, std::move(task));
    }

    template <typename Event, typename Rep, typename Period>
    void post_state_timeout(Event event, std::chrono::duration<Rep, Period> delay) {
        post_delayed(std::move(event), delay, /*cancel_if_state_changes=*/true);
    }

    template <typename Event>
    void enqueue(Event event) {
        auto task = [this, evt = std::move(event)](fsm_type&) {
            try {
                auto snap = execute_dispatch_under_lock(evt);
                detail::invoke_notifications_outside_lock(evt, snap, last_exception_, dispatch_mutex_);
            } catch (...) {
                detail::handle_exception_outside_lock(std::current_exception(), get_exception_handler_copy(),
                                                      last_exception_, dispatch_mutex_);
            }
        };
        queue_.push(std::move(task));
    }

    // ========================================================================
    // Worker Thread Control
    // ========================================================================

    void start_worker() {
        bool expected = false;
        if (worker_running_.compare_exchange_strong(expected, true)) {
            queue_.start();
            worker_thread_ = std::thread([this]() { worker_loop(); });
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
            process_all();
        }
    }

    [[nodiscard]] bool is_worker_running() const noexcept { return worker_running_.load(); }

    void clear_queue() { queue_.clear(); }
    [[nodiscard]] std::size_t pending_events_count() const noexcept { return queue_.size(); }
    [[nodiscard]] std::size_t pending_events() const noexcept { return queue_.size(); }
    [[nodiscard]] bool is_queue_empty() const noexcept { return queue_.empty(); }

    bool process_one() {
        event_handler task;
        if (queue_.try_pop(task)) {
            task(fsm_);
            return true;
        }
        return false;
    }

    std::size_t run_until_empty() {
        std::size_t processed = 0;
        while (process_one()) {
            ++processed;
        }
        return processed;
    }

    std::size_t process_all() { return run_until_empty(); }

    void wait_until_idle() {
        while (!is_queue_empty()) {
            std::this_thread::yield();
        }
    }

  private:
    template <typename Event>
    dispatch_result queue_reentrant_event(const Event& event) {
        enqueue(event);
        return dispatch_result{dispatch_status::deferred};
    }

    void drain_reentrant_queue_if_outermost() {
        if (reentrancy_.depth() == 0 && !worker_running_.load(std::memory_order_relaxed) &&
            !is_calling_from_worker_thread()) {
            process_all();
        }
    }

    template <typename Event>
    detail::dispatch_snapshot execute_dispatch_under_lock(const Event& evt) {
        if (reentrancy_.is_reentrant_call()) {
            detail::dispatch_snapshot snap;
            snap.result = queue_reentrant_event(evt);
            snap.state_name = std::string(fsm_.current_state_name());
            snap.deferred_h = deferred_handler_;
            snap.exception_h = exception_handler_;
            return snap;
        }

        detail::dispatch_snapshot snap;
        std::scoped_lock lock(dispatch_mutex_);
        detail::reentrancy_tracker::depth_guard depth_guard(reentrancy_);
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

    exception_handler get_exception_handler_copy() {
        std::scoped_lock lock(dispatch_mutex_);
        return exception_handler_;
    }

    bool is_calling_from_worker_thread() const noexcept {
        return worker_thread_.get_id() == std::this_thread::get_id();
    }

    void worker_loop() {
        while (worker_running_.load()) {
            event_handler task;
            if (queue_.pop_wait(task)) {
                task(fsm_);
            }
        }
    }

    mutable std::mutex dispatch_mutex_;
    fsm_type fsm_;
    async_event_queue<event_handler> queue_;
    std::atomic<bool> worker_running_{false};
    std::thread worker_thread_;
    detail::reentrancy_tracker reentrancy_{};

    std::function<void(const transition_info&)> user_observer_{nullptr};
    std::vector<transition_info> notification_buffer_;
    unhandled_handler unhandled_handler_{nullptr};
    guard_rejected_handler guard_rejected_handler_{nullptr};
    deferred_handler deferred_handler_{nullptr};
    dispatch_failure_handler failure_handler_{nullptr};
    exception_handler exception_handler_{nullptr};
    std::exception_ptr last_exception_{nullptr};
};

}  // namespace fsm

#pragma once

#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/backend/cpp/runtime/async_types.hpp"
#include "fsm/backend/cpp/runtime/detail/diagnostic_handlers.hpp"
#include "fsm/backend/cpp/runtime/detail/notification_dispatcher.hpp"
#include "fsm/backend/cpp/runtime/detail/reentrancy_tracker.hpp"
#include "fsm/backend/cpp/runtime/detail/worker_thread_controller.hpp"
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

    thread_safe_fsm() { init_observer_(); }

    explicit thread_safe_fsm(services_type& srv) : fsm_(srv) { init_observer_(); }

    explicit thread_safe_fsm(registers_type reg) : fsm_(std::move(reg)) { init_observer_(); }

    thread_safe_fsm(registers_type reg, services_type& srv) : fsm_(std::move(reg), srv) { init_observer_(); }

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
        with_lock([&] { diagnostics_.set_unhandled_handler(std::move(handler)); });
    }

    void set_guard_rejected_handler(guard_rejected_handler handler) {
        with_lock([&] { diagnostics_.set_guard_rejected_handler(std::move(handler)); });
    }

    void set_deferred_handler(deferred_handler handler) {
        with_lock([&] { diagnostics_.set_deferred_handler(std::move(handler)); });
    }

    void set_dispatch_failure_handler(dispatch_failure_handler handler) {
        with_lock([&] { diagnostics_.set_dispatch_failure_handler(std::move(handler)); });
    }

    void set_exception_handler(exception_handler handler) {
        with_lock([&] { diagnostics_.set_exception_handler(std::move(handler)); });
    }

    template <typename Callback>
    void set_observer(Callback&& observer) {
        with_lock([&] { diagnostics_.set_user_observer(std::forward<Callback>(observer)); });
    }

    void clear_observer() {
        with_lock([&] { diagnostics_.clear_user_observer(); });
    }

    [[nodiscard]] std::exception_ptr last_exception() const {
        return with_lock([&] { return diagnostics_.last_exception(); });
    }

    void clear_last_exception() {
        with_lock([&] { diagnostics_.clear_last_exception(); });
    }

    [[nodiscard]] std::uint64_t state_residence_time() const {
        return with_lock([&] { return fsm_.state_residence_time(); });
    }

    [[nodiscard]] bool has_invariant_violation() const {
        return with_lock([&] { return fsm_.has_invariant_violation(); });
    }

    [[nodiscard]] bool is_invariant_satisfied() const { return !has_invariant_violation(); }

    [[nodiscard]] std::optional<invariant_violation_info> last_invariant_violation() const {
        return with_lock([&] { return fsm_.last_invariant_violation(); });
    }

    void set_invariant_violation_handler(std::function<void(const invariant_violation_info&)> handler) {
        with_lock([&] { fsm_.set_invariant_violation_handler(std::move(handler)); });
    }

    void set_invariant_violation_handler(void (*handler)(const invariant_violation_info&)) noexcept {
        with_lock([&] { fsm_.set_invariant_violation_handler(handler); });
    }

    template <typename Callable,
              std::enable_if_t<std::is_convertible_v<Callable, void (*)(const invariant_violation_info&)> &&
                                   !std::is_same_v<std::decay_t<Callable>, void (*)(const invariant_violation_info&)>,
                               int> = 0>
    void set_invariant_violation_handler(Callable&& handler) noexcept {
        set_invariant_violation_handler(
            static_cast<void (*)(const invariant_violation_info&)>(std::forward<Callable>(handler)));
    }

    // State & Register Access
    [[nodiscard]] registers_type snapshot_registers() const {
        return with_lock([&] { return fsm_.registers(); });
    }

    void update_registers(registers_type reg) {
        with_lock([&] { fsm_.registers() = std::move(reg); });
    }

    template <typename Callable>
    auto with_registers(Callable&& fn) const {
        return with_lock([&] { return std::forward<Callable>(fn)(std::as_const(fsm_.registers())); });
    }

    template <typename Callable>
    auto with_registers(Callable&& fn) {
        return with_lock([&] { return std::forward<Callable>(fn)(fsm_.registers()); });
    }

    template <typename State>
    [[nodiscard]] bool is_in_state() const noexcept {
        return with_lock([&] { return fsm_.template is_in_state<State>(); });
    }

    template <typename State>
    [[nodiscard]] bool is_in() const noexcept {
        return with_lock([&] { return fsm_.template is_in<State>(); });
    }

    [[nodiscard]] bool is_in(std::string_view target_name) const noexcept {
        return with_lock([&] { return fsm_.is_in(target_name); });
    }

    void reset() {
        with_lock([&] { fsm_.reset(); });
    }

    void reset(registers_type reg) {
        with_lock([&] { fsm_.reset(std::move(reg)); });
    }

    void clear_history() noexcept {
        with_lock([&] { fsm_.clear_history(); });
    }

    [[nodiscard]] std::string_view get_history(std::string_view parent) const {
        return with_lock([&] { return fsm_.get_history(parent); });
    }

    template <typename ParentState>
    [[nodiscard]] std::string_view get_history() const {
        return get_history(get_state_name_static<ParentState>());
    }

    [[nodiscard]] std::string_view current_state_name() const {
        return with_lock([&] { return fsm_.current_state_name(); });
    }

    [[nodiscard]] std::size_t deferred_count() const noexcept {
        return with_lock([&] { return fsm_.deferred_count(); });
    }

    void clear_deferred_events() noexcept {
        with_lock([&] { fsm_.clear_deferred_events(); });
    }

    [[nodiscard]] auto& timer_manager() noexcept { return fsm_.timer_manager(); }
    [[nodiscard]] const auto& timer_manager() const noexcept { return fsm_.timer_manager(); }

    template <typename Fn>
    auto with_timer_manager(Fn&& fn) {
        return with_lock([&] { return std::forward<Fn>(fn)(fsm_.timer_manager()); });
    }

    template <typename Fn>
    auto with_timer_manager(Fn&& fn) const {
        return with_lock([&] { return std::forward<Fn>(fn)(fsm_.timer_manager()); });
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

    /**
     * @brief Synchronously dispatches an event under mutex protection.
     *
     * This is the canonical thread-safe synchronous dispatch method for `thread_safe_fsm`.
     * If called reentrantly from an action or notification callback on the same thread,
     * the event is queued safely and drained when the outermost dispatch completes.
     */
    template <typename Event>
    dispatch_result send(const Event& event) {
        if (reentrancy_.is_reentrant_call()) {
            return queue_reentrant_event(event);
        }
        auto snap = execute_dispatch_under_lock(event);
        notify_after_dispatch(event, snap);
        drain_reentrant_queue_if_outermost();
        return snap.result;
    }

    /**
     * @brief Synchronously dispatches an event with partitioned I/O ports under mutex protection.
     */
    template <typename Event>
    dispatch_result send(const Event& event, const in_ports_type& in, out_ports_type& out) {
        if (reentrancy_.is_reentrant_call()) {
            worker_.push([this, event, in](fsm_type&) {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
                try {
                    out_ports_type dummy_out{};
                    auto snap = execute_dispatch_ports_under_lock(event, in, dummy_out);
                    notify_after_dispatch(event, snap);
                } catch (...) {
                    detail::handle_exception_outside_lock(std::current_exception(), get_exception_handler_safe(),
                                                          diagnostics_.last_exception_ref(), dispatch_mutex_);
                }
#else
                out_ports_type dummy_out{};
                auto snap = execute_dispatch_ports_under_lock(event, in, dummy_out);
                notify_after_dispatch(event, snap);
#endif
            });
            return dispatch_result{dispatch_status::deferred, std::nullopt};
        }
        auto snap = execute_dispatch_ports_under_lock(event, in, out);
        notify_after_dispatch(event, snap);
        drain_reentrant_queue_if_outermost();
        return snap.result;
    }

    /**
     * @brief Interface compatibility alias for `send()`.
     *
     * Enables generic or template code written against `fsm` to invoke `dispatch()` interchangeably
     * on either `fsm` or `thread_safe_fsm`.
     */
    template <typename Event, typename... Args>
    dispatch_result dispatch(const Event& event, Args&&... args) {
        return send(event, std::forward<Args>(args)...);
    }

    // ========================================================================
    // Sampled Synchronous Control Loop Step
    // ========================================================================

    step_result step(const in_ports_type& in, out_ports_type& out, services_type& srv) {
        return execute_synchronized_mutation([this, &in, &out, &srv]() { return fsm_.step(in, out, srv); });
    }

    template <typename DurationRep>
    step_result step(DurationRep dt, const in_ports_type& in, out_ports_type& out, services_type& srv) {
        return execute_synchronized_mutation([this, dt, &in, &out, &srv]() { return fsm_.step(dt, in, out, srv); });
    }

    step_result step(const in_ports_type& in, out_ports_type& out) {
        return execute_synchronized_mutation([this, &in, &out]() { return fsm_.step(in, out); });
    }

    template <typename DurationRep>
    step_result step(DurationRep dt, const in_ports_type& in, out_ports_type& out) {
        return execute_synchronized_mutation([this, dt, &in, &out]() { return fsm_.step(dt, in, out); });
    }

    step_result step() {
        return execute_synchronized_mutation([this]() { return fsm_.step(); });
    }

    template <typename DurationRep>
    step_result step(DurationRep dt) {
        return execute_synchronized_mutation([this, dt]() { return fsm_.step(dt); });
    }

    std::size_t tick(std::uint64_t delta_ms) {
        return execute_synchronized_mutation([this, delta_ms]() { return fsm_.tick(delta_ms); });
    }

    template <typename Callback>
    std::size_t tick(std::uint64_t delta_ms, Callback on_expired) {
        return execute_synchronized_mutation(
            [this, delta_ms, &on_expired]() { return fsm_.tick(delta_ms, on_expired); });
    }

    template <typename Rep, typename Period>
    std::size_t tick(std::chrono::duration<Rep, Period> dt) {
        return execute_synchronized_mutation([this, dt]() { return fsm_.tick(dt); });
    }

    template <typename Rep, typename Period, typename Callback>
    std::size_t tick(std::chrono::duration<Rep, Period> dt, Callback on_expired) {
        return execute_synchronized_mutation([this, dt, &on_expired]() { return fsm_.tick(dt, on_expired); });
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
        if (!worker_.is_worker_running() && !worker_.is_calling_from_worker_thread()) {
            start_worker();
        }
        auto task = [this, evt = std::move(event), cb = std::forward<Callback>(callback)](fsm_type&) mutable {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
            try {
                auto snap = execute_dispatch_under_lock(evt);
                notify_after_dispatch(evt, snap);
                cb(snap.result);
            } catch (...) {
                detail::handle_exception_outside_lock(std::current_exception(), get_exception_handler_safe(),
                                                      diagnostics_.last_exception_ref(), dispatch_mutex_);
            }
#else
            auto snap = execute_dispatch_under_lock(evt);
            notify_after_dispatch(evt, snap);
            cb(snap.result);
#endif
        };
        worker_.push(std::move(task));
    }

    template <typename Event>
    [[nodiscard]] std::future<dispatch_result> post_async(Event event) {
        if (!worker_.is_worker_running() && !worker_.is_calling_from_worker_thread()) {
            start_worker();
        }
        auto promise = std::make_shared<std::promise<dispatch_result>>();
        auto future = promise->get_future();
        auto task = [this, evt = std::move(event), p = promise](fsm_type&) mutable {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
            try {
                auto snap = execute_dispatch_under_lock(evt);
                notify_after_dispatch(evt, snap);
                p->set_value(snap.result);
            } catch (...) {
                p->set_exception(std::current_exception());
                detail::handle_exception_outside_lock(std::current_exception(), get_exception_handler_safe(),
                                                      diagnostics_.last_exception_ref(), dispatch_mutex_);
            }
#else
            auto snap = execute_dispatch_under_lock(evt);
            notify_after_dispatch(evt, snap);
            p->set_value(snap.result);
#endif
        };
        worker_.push(std::move(task));
        return future;
    }

    template <typename Event, typename Rep, typename Period>
    void post_delayed(Event event, std::chrono::duration<Rep, Period> delay, bool cancel_if_state_changes = false) {
        if (!worker_.is_worker_running() && !worker_.is_calling_from_worker_thread()) {
            start_worker();
        }
        auto deadline = std::chrono::steady_clock::now() + delay;
        const std::uint64_t scheduled_epoch = transition_epoch_.load(std::memory_order_acquire);
        auto task = [this, evt = std::move(event), scheduled_epoch, cancel_if_state_changes](fsm_type&) {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
            try {
                auto snap = execute_dispatch_under_lock(evt, cancel_if_state_changes, scheduled_epoch);
                if (snap.has_value()) {
                    notify_after_dispatch(evt, *snap);
                }
            } catch (...) {
                detail::handle_exception_outside_lock(std::current_exception(), get_exception_handler_safe(),
                                                      diagnostics_.last_exception_ref(), dispatch_mutex_);
            }
#else
            auto snap = execute_dispatch_under_lock(evt, cancel_if_state_changes, scheduled_epoch);
            if (snap.has_value()) {
                notify_after_dispatch(evt, *snap);
            }
#endif
        };
        worker_.push_timed(deadline, std::move(task));
    }

    template <typename Event, typename Rep, typename Period>
    void post_state_timeout(Event event, std::chrono::duration<Rep, Period> delay) {
        post_delayed(std::move(event), delay, /*cancel_if_state_changes=*/true);
    }

    template <typename Event>
    void enqueue(Event event) {
        auto task = [this, evt = std::move(event)](fsm_type&) {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
            try {
                auto snap = execute_dispatch_under_lock(evt);
                notify_after_dispatch(evt, snap);
            } catch (...) {
                detail::handle_exception_outside_lock(std::current_exception(), get_exception_handler_safe(),
                                                      diagnostics_.last_exception_ref(), dispatch_mutex_);
            }
#else
            auto snap = execute_dispatch_under_lock(evt);
            notify_after_dispatch(evt, snap);
#endif
        };
        worker_.push(std::move(task));
    }

    // ========================================================================
    // Worker Thread Control
    // ========================================================================

    void start_worker() { worker_.start_worker(fsm_); }

    void stop_worker() {
        worker_.stop_worker();
        process_all();
    }

    [[nodiscard]] bool is_worker_running() const noexcept { return worker_.is_worker_running(); }

    void clear_queue() { worker_.clear_queue(); }
    [[nodiscard]] std::size_t pending_events_count() const noexcept { return worker_.pending_events_count(); }
    [[nodiscard]] std::size_t pending_events() const noexcept { return worker_.pending_events_count(); }
    [[nodiscard]] bool is_queue_empty() const noexcept { return worker_.is_queue_empty(); }

    bool process_one() { return worker_.process_one(fsm_); }
    std::size_t run_until_empty() { return worker_.run_until_empty(fsm_); }
    std::size_t process_all() { return run_until_empty(); }
    void wait_until_idle() { worker_.wait_until_idle(); }

  private:
    void init_observer_() {
        fsm_.set_observer([this](const transition_info& info) {
            if (diagnostics_.has_any_callback()) {
                diagnostics_.notification_buffer().push_back(info);
            }
        });
    }

    template <typename Fn>
    decltype(auto) with_lock(Fn&& fn) const {
        if (reentrancy_.is_reentrant_call()) {
            return std::forward<Fn>(fn)();
        }
        std::scoped_lock lock(dispatch_mutex_);
        return std::forward<Fn>(fn)();
    }

    [[nodiscard]] exception_handler get_exception_handler_safe() const {
        return with_lock([&] { return diagnostics_.get_exception_handler_copy(); });
    }

    template <typename Fn>
    auto execute_synchronized_mutation(Fn&& fn) {
        if (reentrancy_.is_reentrant_call()) {
            detail::reentrancy_tracker::depth_guard depth_guard(reentrancy_);
            const std::size_t prev_state = fsm_.get_current_state_variant().index();
            auto res = fn();
            if (fsm_.get_current_state_variant().index() != prev_state) {
                transition_epoch_.fetch_add(1, std::memory_order_release);
            }
            return res;
        }
        using Result = decltype(fn());
        Result res{};
        {
            std::scoped_lock lock(dispatch_mutex_);
            detail::reentrancy_tracker::depth_guard depth_guard(reentrancy_);
            const std::size_t prev_state = fsm_.get_current_state_variant().index();
            res = fn();
            if (fsm_.get_current_state_variant().index() != prev_state) {
                transition_epoch_.fetch_add(1, std::memory_order_release);
            }
        }
        drain_reentrant_queue_if_outermost();
        return res;
    }

    template <typename Event>
    dispatch_result queue_reentrant_event(const Event& event) {
        enqueue(event);
        return dispatch_result{dispatch_status::deferred};
    }

    void drain_reentrant_queue_if_outermost() {
        if (reentrancy_.depth() == 0 && !worker_.is_worker_running() && !worker_.is_calling_from_worker_thread()) {
            process_all();
        }
    }

    detail::dispatch_snapshot capture_dispatch_snapshot(dispatch_result result) {
        if (!diagnostics_.has_any_callback() && diagnostics_.notification_buffer().empty()) {
            detail::dispatch_snapshot snap{};
            snap.result = result;
            return snap;
        }
        detail::dispatch_snapshot snap{};
        snap.result = result;
        snap.state_name = std::string(fsm_.current_state_name());
        snap.notifications = std::move(diagnostics_.notification_buffer());
        diagnostics_.populate_snapshot_handlers(snap);
        return snap;
    }

    template <typename Event>
    void notify_after_dispatch(const Event& evt, const detail::dispatch_snapshot& snap) {
        if (!snap.observer_h && !snap.unhandled_h && !snap.guard_rejected_h && !snap.deferred_h && !snap.failure_h &&
            !snap.exception_h && snap.notifications.empty()) {
            return;
        }
        detail::invoke_notifications_outside_lock(evt, snap, diagnostics_.last_exception_ref(), dispatch_mutex_);
    }

    template <typename Event>
    detail::dispatch_snapshot execute_dispatch_under_lock(const Event& evt) {
        if (reentrancy_.is_reentrant_call()) {
            return capture_dispatch_snapshot(queue_reentrant_event(evt));
        }

        std::scoped_lock lock(dispatch_mutex_);
        detail::reentrancy_tracker::depth_guard depth_guard(reentrancy_);
        diagnostics_.notification_buffer().clear();
        const std::size_t prev_state = fsm_.get_current_state_variant().index();
        auto res = fsm_.dispatch(evt);
        if (res.is_success() || fsm_.get_current_state_variant().index() != prev_state) {
            transition_epoch_.fetch_add(1, std::memory_order_release);
        }
        return capture_dispatch_snapshot(res);
    }

    template <typename Event>
    std::optional<detail::dispatch_snapshot> execute_dispatch_under_lock(const Event& evt, bool cancel_if_state_changes,
                                                                         std::uint64_t expected_epoch) {
        std::scoped_lock lock(dispatch_mutex_);
        if (cancel_if_state_changes && transition_epoch_.load(std::memory_order_acquire) != expected_epoch) {
            return std::nullopt;  // Invalidate stale timeout atomically under dispatch_mutex_ (no ABA/TOCTOU)
        }
        detail::reentrancy_tracker::depth_guard depth_guard(reentrancy_);
        diagnostics_.notification_buffer().clear();
        const std::size_t prev_state = fsm_.get_current_state_variant().index();
        auto res = fsm_.dispatch(evt);
        if (res.is_success() || fsm_.get_current_state_variant().index() != prev_state) {
            transition_epoch_.fetch_add(1, std::memory_order_release);
        }
        return capture_dispatch_snapshot(res);
    }

    template <typename Event>
    detail::dispatch_snapshot execute_dispatch_ports_under_lock(const Event& evt, const in_ports_type& in,
                                                                out_ports_type& out) {
        std::scoped_lock lock(dispatch_mutex_);
        detail::reentrancy_tracker::depth_guard depth_guard(reentrancy_);
        diagnostics_.notification_buffer().clear();
        const std::size_t prev_state = fsm_.get_current_state_variant().index();
        auto res = fsm_.dispatch(evt, in, out);
        if (res.is_success() || fsm_.get_current_state_variant().index() != prev_state) {
            transition_epoch_.fetch_add(1, std::memory_order_release);
        }
        return capture_dispatch_snapshot(res);
    }

    mutable std::mutex dispatch_mutex_;
    fsm_type fsm_;
    detail::worker_thread_controller<event_handler, fsm_type> worker_{};
    detail::reentrancy_tracker reentrancy_{};
    detail::diagnostic_handler_manager diagnostics_{};
    std::atomic<std::uint64_t> transition_epoch_{0};
};

}  // namespace fsm

#include "fsm/backend/cpp/runtime/detail/thread_safe_policy_adapter.hpp"

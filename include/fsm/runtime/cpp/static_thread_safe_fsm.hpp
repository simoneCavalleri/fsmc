#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

#include "fsm/runtime/cpp/fsm.hpp"
#include "fsm/runtime/cpp/static_ring_buffer.hpp"

namespace fsm {

// Zero-allocation, statically bounded thread-safe FSM wrapper designed for hard real-time and embedded systems.
// Does NOT use std::function, heap allocations, std::vector, or std::future.
template <typename Table, typename Context = no_context, std::size_t QueueCapacity = 64,
          OverflowPolicy Policy = OverflowPolicy::DropIncoming, typename InitialState = typename Table::initial_state,
          typename Observer = no_observer>
class static_thread_safe_fsm {
  public:
    using fsm_type = fsm<Table, Context, InitialState, Observer>;
    using event_variant = typename Table::event_variant;
    using context_type = Context;

    static constexpr std::size_t state_count = fsm_type::state_count;
    static constexpr std::size_t transition_count = fsm_type::transition_count;
    static constexpr std::size_t event_count = fsm_type::event_count;
    static constexpr std::size_t queue_capacity = QueueCapacity;
    static constexpr OverflowPolicy overflow_policy = Policy;

    template <typename State>
    static constexpr bool has_state = fsm_type::template has_state<State>;

    template <typename Event>
    static constexpr bool has_event = fsm_type::template has_event<Event>;

    template <typename C = Context, typename = std::enable_if_t<std::is_same_v<C, no_context>>>
    static_thread_safe_fsm() {}

    explicit static_thread_safe_fsm(Context& ctx, Table table = Table{}) : fsm_(ctx, std::move(table)) {}

    explicit static_thread_safe_fsm(Context& ctx, InitialState initial, Table table = Table{})
        : fsm_(ctx, std::move(initial), std::move(table)) {}

    ~static_thread_safe_fsm() { stop_worker(); }

    // Non-copyable, non-movable
    static_thread_safe_fsm(const static_thread_safe_fsm&) = delete;
    static_thread_safe_fsm& operator=(const static_thread_safe_fsm&) = delete;
    static_thread_safe_fsm(static_thread_safe_fsm&&) = delete;
    static_thread_safe_fsm& operator=(static_thread_safe_fsm&&) = delete;

    template <typename Callable>
    auto with_context(Callable&& callable) {
        std::scoped_lock lock(mutex_);
        return std::forward<Callable>(callable)(fsm_.context());
    }

    template <typename Callable>
    auto with_context(Callable&& callable) const {
        std::scoped_lock lock(mutex_);
        return std::forward<Callable>(callable)(fsm_.context());
    }

    [[nodiscard]] Context snapshot_context() const {
        std::scoped_lock lock(mutex_);
        return fsm_.context();
    }

    /**
     * @warning Direct, un-synchronized access to Context.
     * In multithreaded environments where a background worker or multiple threads
     * dispatch events concurrently, prefer using @ref with_context() or @ref snapshot_context()
     * to avoid race conditions.
     */
    [[nodiscard]] Context& context() noexcept { return fsm_.context(); }
    [[nodiscard]] const Context& context() const noexcept { return fsm_.context(); }

    [[nodiscard]] Context* get_context() noexcept { return fsm_.get_context(); }
    [[nodiscard]] const Context* get_context() const noexcept { return fsm_.get_context(); }

    template <typename Event>
    dispatch_result send(const Event& event) {
        std::scoped_lock lock(mutex_);
        return fsm_.dispatch(event);
    }

    // Zero-allocation queue submission
    template <typename Event>
    bool post(Event&& event, OverflowPolicy policy = Policy) {
        if (!worker_running_.load() && !is_calling_from_worker_thread()) {
            start_worker();
        }
        {
            std::scoped_lock lock(mutex_);
            if (!queue_.push(event_variant(std::forward<Event>(event)), policy)) {
                return false;
            }
        }
        cv_.notify_one();
        return true;
    }

    template <typename Event>
    bool enqueue(Event&& event, OverflowPolicy policy = Policy) {
        std::scoped_lock lock(mutex_);
        return queue_.push(event_variant(std::forward<Event>(event)), policy);
    }

    bool process_one() {
        event_variant evt;
        {
            std::scoped_lock lock(mutex_);
            auto popped = queue_.pop();
            if (!popped) {
                return false;
            }
            evt = std::move(*popped);
        }
        std::visit([this](const auto& e) { fsm_.dispatch(e); }, evt);
        return true;
    }

    std::size_t process_all() {
        std::size_t processed = 0;
        while (process_one()) {
            ++processed;
        }
        return processed;
    }

    [[nodiscard]] std::size_t pending_events() const {
        std::scoped_lock lock(mutex_);
        return queue_.size();
    }

    [[nodiscard]] bool is_queue_empty() const {
        std::scoped_lock lock(mutex_);
        return queue_.empty();
    }

    [[nodiscard]] bool is_queue_full() const {
        std::scoped_lock lock(mutex_);
        return queue_.full();
    }

    void start_worker() {
        std::scoped_lock lock(mutex_);
        if (worker_running_) {
            return;
        }
        worker_running_ = true;
        worker_thread_ = std::thread([this]() { worker_loop(); });
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

    template <typename State>
    [[nodiscard]] bool is_in_state() const {
        std::scoped_lock lock(mutex_);
        return fsm_.template is_in_state<State>();
    }

    [[nodiscard]] std::string_view current_state_name() const {
        std::scoped_lock lock(mutex_);
        return fsm_.current_state_name();
    }

  private:
    [[nodiscard]] bool is_calling_from_worker_thread() const noexcept {
        return worker_thread_.get_id() == std::this_thread::get_id();
    }

    void worker_loop() {
        while (true) {
            event_variant evt;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() { return !worker_running_ || !queue_.empty(); });

                if (!worker_running_ && queue_.empty()) {
                    break;
                }

                auto popped = queue_.pop();
                if (popped) {
                    evt = std::move(*popped);
                } else {
                    continue;
                }
            }

            std::visit([this](const auto& e) { fsm_.dispatch(e); }, evt);
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    fsm_type fsm_;
    static_ring_buffer<event_variant, QueueCapacity> queue_;
    std::atomic<bool> worker_running_{false};
    std::thread worker_thread_;
};

}  // namespace fsm

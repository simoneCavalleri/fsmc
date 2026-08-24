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

#include "fsm.hpp"
#include "static_ring_buffer.hpp"

namespace fsm {

// Zero-allocation, statically bounded thread-safe FSM wrapper designed for hard real-time and embedded systems.
// Does NOT use std::function, heap allocations, std::vector, or std::future.
template <typename Table, typename Context = no_context, std::size_t QueueCapacity = 64,
          typename InitialState = typename Table::initial_state, typename Observer = no_observer>
class static_thread_safe_fsm {
  public:
    using fsm_type = fsm<Table, Context, InitialState, Observer>;
    using event_variant = typename Table::event_variant;
    using context_type = Context;

    static constexpr std::size_t state_count = fsm_type::state_count;
    static constexpr std::size_t transition_count = fsm_type::transition_count;
    static constexpr std::size_t event_count = fsm_type::event_count;
    static constexpr std::size_t queue_capacity = QueueCapacity;

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

    [[nodiscard]] Context& context() noexcept { return fsm_.context(); }
    [[nodiscard]] const Context& context() const noexcept { return fsm_.context(); }

    [[nodiscard]] Context* get_context() noexcept { return fsm_.get_context(); }
    [[nodiscard]] const Context* get_context() const noexcept { return fsm_.get_context(); }

    template <typename Event>
    dispatch_result send(const Event& event) {
        std::scoped_lock lock(mutex_);
        return fsm_.dispatch(event);
    }

    // Zero-allocation queue submission (returns false if ring buffer is full)
    template <typename Event>
    bool post(Event&& event) {
        {
            std::scoped_lock lock(queue_mutex_);
            if (!queue_.push(event_variant{std::forward<Event>(event)})) {
                return false;
            }
        }
        cv_.notify_one();
        return true;
    }

    // Process a single event from the static ring buffer in current thread
    bool process_one() {
        std::optional<event_variant> evt_opt;
        {
            std::scoped_lock lock(queue_mutex_);
            evt_opt = queue_.pop();
        }
        if (evt_opt.has_value()) {
            std::scoped_lock lock(mutex_);
            std::visit([this](const auto& evt) { fsm_.dispatch(evt); }, *evt_opt);
            return true;
        }
        return false;
    }

    // Process all pending events in the ring buffer synchronously
    std::size_t process_all() {
        std::size_t processed = 0;
        while (process_one()) {
            ++processed;
        }
        return processed;
    }

    // Starts background worker thread
    void start_worker() {
        if (worker_.joinable())
            return;
        running_ = true;
        worker_ = std::thread([this]() {
            while (running_.load()) {
                std::optional<event_variant> evt_opt;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    cv_.wait(lock, [&] { return !queue_.empty() || !running_.load(); });
                    if (!running_.load() && queue_.empty())
                        break;
                    evt_opt = queue_.pop();
                }
                if (evt_opt.has_value()) {
                    std::scoped_lock lock(mutex_);
                    std::visit([this](const auto& evt) { fsm_.dispatch(evt); }, *evt_opt);
                }
            }
        });
    }

    // Stops and joins background worker thread cleanly
    void stop_worker() {
        running_ = false;
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    template <typename State>
    [[nodiscard]] bool is_in_state() const {
        std::scoped_lock lock(mutex_);
        return fsm_.template is_in_state<State>();
    }

    [[nodiscard]] std::string_view current_state_name() const {
        std::scoped_lock lock(mutex_);
        return fsm_.current_state_name();
    }

    [[nodiscard]] bool is_queue_empty() const {
        std::scoped_lock lock(queue_mutex_);
        return queue_.empty();
    }

    [[nodiscard]] bool is_queue_full() const {
        std::scoped_lock lock(queue_mutex_);
        return queue_.full();
    }

    [[nodiscard]] std::size_t pending_events() const {
        std::scoped_lock lock(queue_mutex_);
        return queue_.size();
    }

  private:
    fsm_type fsm_;
    mutable std::recursive_mutex mutex_;
    mutable std::mutex queue_mutex_;
    std::condition_variable cv_;
    static_ring_buffer<event_variant, QueueCapacity> queue_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace fsm

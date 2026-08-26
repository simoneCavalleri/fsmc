#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

#include "fsm/runtime/cpp/fsm.hpp"
#include "fsm/runtime/cpp/spsc_ring_buffer.hpp"

namespace fsm {

/**
 * @brief Lock-Free, Zero-Allocation Single-Producer Single-Consumer (SPSC) FSM Wrapper.
 *
 * Designed for Hard Real-Time, Embedded Systems, and Interrupt Service Routines (ISR).
 * - **Producer (ISR / Sensor Task)**: Calls `enqueue()` in Wait-Free O(1) time without acquiring locks or allocating
 * memory.
 * - **Consumer (Control Task / Worker)**: Calls `process_one()` or `run_until_empty()` to drain the queue and advance
 * the FSM sequentially.
 * - **Readers (Any Thread)**: Inspect `state_index()`, `state_name()`, and `snapshot_context()` via acquire-release
 * semantics and seqlock synchronization.
 *
 * @tparam Table Transition table type defining states, events, and transitions.
 * @tparam Context Shared EFSM context struct (default: no_context).
 * @tparam QueueCapacity Power-of-two capacity for the internal SPSC ring buffer (default: 64).
 * @tparam InitialState Initial state type (default: Table::initial_state).
 */
template <typename Table, typename Context = no_context, std::size_t QueueCapacity = 64,
          typename InitialState = typename Table::initial_state>
class spsc_fsm {
    static_assert((QueueCapacity > 1) && ((QueueCapacity & (QueueCapacity - 1)) == 0),
                  "spsc_fsm QueueCapacity must be a power of two");

  public:
    using fsm_type = fsm<Table, Context, InitialState>;
    using event_variant = typename Table::event_variant;
    using context_type = Context;
    using initial_state_type = InitialState;

    static constexpr std::size_t state_count = fsm_type::state_count;
    static constexpr std::size_t transition_count = fsm_type::transition_count;
    static constexpr std::size_t event_count = fsm_type::event_count;
    static constexpr std::size_t queue_capacity = QueueCapacity;

    template <typename State>
    static constexpr bool has_state = fsm_type::template has_state<State>;

    template <typename Event>
    static constexpr bool has_event = fsm_type::template has_event<Event>;

    // Default constructor (enabled only when Context is no_context)
    template <typename C = Context, typename = std::enable_if_t<std::is_same_v<C, no_context>>>
    spsc_fsm() : fsm_(), context_(nullptr) {
        state_index_.store(fsm_.get_current_state_variant().index(), std::memory_order_relaxed);
    }

    // Constructor with Context reference
    explicit spsc_fsm(Context& ctx, Table table = Table{}) : fsm_(ctx, std::move(table)), context_(&ctx) {
        state_index_.store(fsm_.get_current_state_variant().index(), std::memory_order_relaxed);
    }

    // Constructor with Context reference and specific initial state
    explicit spsc_fsm(Context& ctx, InitialState initial, Table table = Table{})
        : fsm_(ctx, std::move(initial), std::move(table)), context_(&ctx) {
        state_index_.store(fsm_.get_current_state_variant().index(), std::memory_order_relaxed);
    }

    // Non-copyable, non-movable
    spsc_fsm(const spsc_fsm&) = delete;
    spsc_fsm& operator=(const spsc_fsm&) = delete;
    spsc_fsm(spsc_fsm&&) = delete;
    spsc_fsm& operator=(spsc_fsm&&) = delete;

    // ========================================================================
    // Producer API (Single Producer / ISR Thread: Wait-Free O(1))
    // ========================================================================

    /**
     * @brief Enqueues an event into the lock-free SPSC buffer.
     * Safe to call from Interrupt Service Routines (ISR) and hard real-time tasks.
     *
     * @param event The typed event instance.
     * @return true if successfully enqueued, false if queue is full.
     */
    template <typename Event>
    bool enqueue(Event&& event) noexcept {
        return queue_.push(event_variant{std::forward<Event>(event)});
    }

    /**
     * @brief Enqueues an event variant directly into the lock-free SPSC buffer.
     */
    bool enqueue(const event_variant& event) noexcept { return queue_.push(event); }

    // ========================================================================
    // Consumer API (Single Consumer / Dedicated Worker Thread)
    // ========================================================================

    /**
     * @brief Pops and dispatches a single event from the queue.
     * Must be called exclusively by the designated consumer/worker thread.
     *
     * @return true if an event was processed, false if the queue was empty.
     */
    bool process_one() {
        auto item = queue_.pop();
        if (!item.has_value()) {
            return false;
        }

        // Begin seqlock writer sequence
        seq_.fetch_add(1, std::memory_order_release);

        std::visit([this](const auto& evt) { (void)this->fsm_.dispatch_direct(evt); }, *item);
        state_index_.store(fsm_.get_current_state_variant().index(), std::memory_order_release);

        // End seqlock writer sequence
        seq_.fetch_add(1, std::memory_order_release);
        return true;
    }

    /**
     * @brief Drains all currently queued events in sequence.
     *
     * @return std::size_t Number of events processed.
     */
    std::size_t run_until_empty() {
        std::size_t count = 0;
        while (process_one()) {
            ++count;
        }
        return count;
    }

    // ========================================================================
    // Read & Introspection API (Thread-Safe from Any Reader Thread)
    // ========================================================================

    /**
     * @brief Atomically queries the active state variant index.
     */
    [[nodiscard]] std::size_t state_index() const noexcept { return state_index_.load(std::memory_order_acquire); }

    /**
     * @brief Queries current active state name as string_view.
     */
    [[nodiscard]] std::string_view state_name() const noexcept { return fsm_.current_state_name(); }

    /**
     * @brief Checks if the FSM is currently in the specified State.
     */
    template <typename State>
    [[nodiscard]] bool is_in() const noexcept {
        return fsm_.template is_in<State>();
    }

    /**
     * @brief Checks if the FSM is currently in the specified State (alias).
     */
    template <typename State>
    [[nodiscard]] bool is_in_state() const noexcept {
        return fsm_.template is_in_state<State>();
    }

    /**
     * @brief Takes a consistent atomic snapshot of Context using seqlock synchronization.
     * Zero locks, zero blocking of the producer or consumer thread.
     */
    [[nodiscard]] Context snapshot_context() const noexcept {
        if constexpr (std::is_same_v<Context, no_context>) {
            return no_context{};
        } else {
            if (context_ == nullptr) {
                return Context{};
            }
            Context result;
            std::uint32_t s0 = 0;
            do {
                s0 = seq_.load(std::memory_order_acquire);
                while (s0 & 1U) {
                    std::this_thread::yield();
                    s0 = seq_.load(std::memory_order_acquire);
                }
                result = *context_;
                std::atomic_thread_fence(std::memory_order_acquire);
            } while (seq_.load(std::memory_order_acquire) != s0);
            return result;
        }
    }

    /**
     * @brief Executes a read-only callable against a consistent snapshot of the Context.
     */
    template <typename Callable>
    auto with_context(Callable&& fn) const {
        auto ctx_copy = snapshot_context();
        return fn(ctx_copy);
    }

    /**
     * @brief Returns current approximate number of pending queued events.
     */
    [[nodiscard]] std::size_t queue_size() const noexcept { return queue_.size(); }

    /**
     * @brief Returns true if the event queue is currently empty.
     */
    [[nodiscard]] bool queue_empty() const noexcept { return queue_.empty(); }

    /**
     * @brief Returns true if the event queue is currently full.
     */
    [[nodiscard]] bool queue_full() const noexcept { return queue_.full(); }

  private:
    fsm_type fsm_;
    spsc_ring_buffer<event_variant, QueueCapacity> queue_;
    std::atomic<std::size_t> state_index_{0};
    mutable std::atomic<std::uint32_t> seq_{0};
    Context* context_{nullptr};
};

}  // namespace fsm

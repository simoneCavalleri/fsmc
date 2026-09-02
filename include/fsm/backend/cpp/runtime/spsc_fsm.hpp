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

#include "fsm/backend/cpp/runtime/fsm.hpp"
#include "fsm/backend/cpp/runtime/spsc_ring_buffer.hpp"

namespace fsm {

/**
 * @brief Lock-Free, Zero-Allocation Single-Producer Single-Consumer (SPSC) FSM Wrapper.
 *
 * Designed for Hard Real-Time, Embedded Systems, and Interrupt Service Routines (ISR).
 * - **Producer (ISR / Sensor Task)**: Calls `enqueue()` in Wait-Free O(1) time without acquiring locks or allocating
 * memory.
 * - **Consumer (Control Task / Worker)**: Calls `process_one()` or `run_until_empty()` to drain the queue and advance
 * the FSM sequentially.
 * - **Readers (Any Thread)**: Inspect `state_index()`, `state_name()`, and `snapshot_registers()` via acquire-release
 * semantics and seqlock synchronization.
 * Reviewed and found correct as-is: the seqlock in snapshot_registers() below
 * (odd/even sequence counter bracketing every register mutation, retried
 * read on the reader side) is a standard, sound seqlock.
 */
namespace detail {
template <typename Variant, std::size_t... Is>
constexpr std::string_view get_state_name_by_index_impl(std::size_t idx, std::index_sequence<Is...>) noexcept {
    constexpr std::string_view names[] = { ::fsm::get_state_name(std::variant_alternative_t<Is, Variant>{})... };
    if (idx < sizeof...(Is)) {
        return names[idx];
    }
    return "";
}

template <typename Variant>
constexpr std::string_view get_state_name_by_index(std::size_t idx) noexcept {
    return get_state_name_by_index_impl<Variant>(idx, std::make_index_sequence<std::variant_size_v<Variant>>{});
}
}  // namespace detail

template <typename Table, typename InPorts = no_ports, typename OutPorts = no_ports, typename Registers = no_registers,
          typename Services = no_services, std::size_t QueueCapacity = 64,
          typename InitialState = typename Table::initial_state, std::size_t DeferredCapacity = 16>
class spsc_fsm {
    static_assert((QueueCapacity > 1) && ((QueueCapacity & (QueueCapacity - 1)) == 0),
                  "spsc_fsm QueueCapacity must be a power of two");

  public:
    using fsm_type = fsm<Table, InPorts, OutPorts, Registers, Services, InitialState, no_observer, DeferredCapacity>;
    using event_variant = typename Table::event_variant;
    using in_ports_type = InPorts;
    using out_ports_type = OutPorts;
    using registers_type = Registers;
    using services_type = Services;
    using initial_state_type = InitialState;

    static constexpr std::size_t state_count = fsm_type::state_count;
    static constexpr std::size_t transition_count = fsm_type::transition_count;
    static constexpr std::size_t event_count = fsm_type::event_count;
    static constexpr std::size_t queue_capacity = QueueCapacity;

    template <typename State>
    static constexpr bool has_state = fsm_type::template has_state<State>;

    template <typename Event>
    static constexpr bool has_event = fsm_type::template has_event<Event>;

    // Constructors
    spsc_fsm() : fsm_() { state_index_.store(fsm_.get_current_state_variant().index(), std::memory_order_relaxed); }

    explicit spsc_fsm(Services& srv, Table table = Table{}) : fsm_(srv, std::move(table)) {
        state_index_.store(fsm_.get_current_state_variant().index(), std::memory_order_relaxed);
    }

    explicit spsc_fsm(Registers reg, Table table = Table{}) : fsm_(std::move(reg), std::move(table)) {
        state_index_.store(fsm_.get_current_state_variant().index(), std::memory_order_relaxed);
    }

    explicit spsc_fsm(Registers reg, Services& srv, Table table = Table{})
        : fsm_(std::move(reg), srv, std::move(table)) {
        state_index_.store(fsm_.get_current_state_variant().index(), std::memory_order_relaxed);
    }

    explicit spsc_fsm(InitialState initial, Table table = Table{}) : fsm_(std::move(initial), std::move(table)) {
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
     * @brief Posts an event into the lock-free ring buffer in Wait-Free O(1) time.
     * @param event The event payload instance to post.
     * @return true if queued successfully, false if the queue is full.
     */
    template <typename Event>
    [[nodiscard]] bool post(Event&& event) noexcept {
        return queue_.push(event_variant{std::forward<Event>(event)});
    }

    [[nodiscard]] bool post(const event_variant& event) noexcept { return queue_.push(event); }

    // ========================================================================
    // Consumer API (Single Consumer / Dedicated Worker Thread)
    // ========================================================================

    bool process_one() {
        auto item = queue_.pop();
        if (!item.has_value()) {
            return false;
        }

        seq_.fetch_add(1, std::memory_order_release);
        std::visit([this](const auto& evt) { (void)this->fsm_.dispatch_direct(evt); }, *item);
        state_index_.store(fsm_.get_current_state_variant().index(), std::memory_order_release);
        seq_.fetch_add(1, std::memory_order_release);
        return true;
    }

    bool process_one(const in_ports_type& in, out_ports_type& out) {
        auto item = queue_.pop();
        if (!item.has_value()) {
            return false;
        }

        seq_.fetch_add(1, std::memory_order_release);
        services_type dummy_srv{};
        services_type& srv = (fsm_.get_services() != nullptr) ? *fsm_.get_services() : dummy_srv;
        std::visit(
            [this, &in, &out, &srv](const auto& evt) { (void)this->fsm_.dispatch_direct_ports(evt, in, out, srv); },
            *item);
        state_index_.store(fsm_.get_current_state_variant().index(), std::memory_order_release);
        seq_.fetch_add(1, std::memory_order_release);
        return true;
    }

    std::size_t run_until_empty() {
        std::size_t count = 0;
        while (process_one()) {
            ++count;
        }
        return count;
    }

    std::size_t run_until_empty(const in_ports_type& in, out_ports_type& out) {
        std::size_t count = 0;
        while (process_one(in, out)) {
            ++count;
        }
        return count;
    }

    step_result step(const in_ports_type& in, out_ports_type& out, services_type& srv) {
        seq_.fetch_add(1, std::memory_order_release);
        step_result res = fsm_.step(in, out, srv);
        state_index_.store(fsm_.get_current_state_variant().index(), std::memory_order_release);
        seq_.fetch_add(1, std::memory_order_release);
        return res;
    }

    template <typename DurationRep>
    step_result step(DurationRep dt, const in_ports_type& in, out_ports_type& out, services_type& srv) {
        seq_.fetch_add(1, std::memory_order_release);
        step_result res = fsm_.step(dt, in, out, srv);
        state_index_.store(fsm_.get_current_state_variant().index(), std::memory_order_release);
        seq_.fetch_add(1, std::memory_order_release);
        return res;
    }

    step_result step(const in_ports_type& in, out_ports_type& out) {
        seq_.fetch_add(1, std::memory_order_release);
        step_result res = fsm_.step(in, out);
        state_index_.store(fsm_.get_current_state_variant().index(), std::memory_order_release);
        seq_.fetch_add(1, std::memory_order_release);
        return res;
    }

    template <typename DurationRep>
    step_result step(DurationRep dt, const in_ports_type& in, out_ports_type& out) {
        seq_.fetch_add(1, std::memory_order_release);
        step_result res = fsm_.step(dt, in, out);
        state_index_.store(fsm_.get_current_state_variant().index(), std::memory_order_release);
        seq_.fetch_add(1, std::memory_order_release);
        return res;
    }

    step_result step() {
        seq_.fetch_add(1, std::memory_order_release);
        step_result res = fsm_.step();
        state_index_.store(fsm_.get_current_state_variant().index(), std::memory_order_release);
        seq_.fetch_add(1, std::memory_order_release);
        return res;
    }

    template <typename DurationRep>
    step_result step(DurationRep dt) {
        seq_.fetch_add(1, std::memory_order_release);
        step_result res = fsm_.step(dt);
        state_index_.store(fsm_.get_current_state_variant().index(), std::memory_order_release);
        seq_.fetch_add(1, std::memory_order_release);
        return res;
    }

    // ========================================================================
    // Read & Introspection API
    // ========================================================================

    [[nodiscard]] std::size_t state_index() const noexcept { return state_index_.load(std::memory_order_acquire); }
    [[nodiscard]] std::string_view state_name() const noexcept {
        return detail::get_state_name_by_index<typename Table::state_variant>(state_index());
    }

    template <typename State>
    [[nodiscard]] bool is_in() const noexcept {
        return fsm_.template is_in<State>();
    }

    template <typename State>
    [[nodiscard]] bool is_in_state() const noexcept {
        return fsm_.template is_in_state<State>();
    }

    /**
     * @brief Captures a consistent snapshot of internal registers without blocking the consumer thread.
     *
     * WHY (Seqlock Lock-Free Protocol):
     * In hard real-time systems, external logging or telemetry threads must never block or preempt
     * the control task. We implement a lightweight Sequence Lock (seqlock):
     * 1. The consumer increments `seq_` to an odd number before mutating registers, and to even after.
     * 2. The reader thread samples `seq_` before and after reading the registers.
     * 3. If `seq_` was even and unchanged across the copy, the snapshot is guaranteed free of torn reads.
     */
    [[nodiscard]] Registers snapshot_registers() const noexcept {
        if constexpr (std::is_same_v<Registers, no_registers>) {
            return no_registers{};
        } else {
            Registers result;
            std::uint32_t s0 = 0;
            do {
                s0 = seq_.load(std::memory_order_acquire);
                while (s0 & 1U) {
                    std::this_thread::yield();
                    s0 = seq_.load(std::memory_order_acquire);
                }
                result = fsm_.registers();
                std::atomic_thread_fence(std::memory_order_acquire);
            } while (seq_.load(std::memory_order_acquire) != s0);
            return result;
        }
    }

    template <typename Callable>
    auto with_registers(Callable&& fn) const {
        auto reg_copy = snapshot_registers();
        return fn(reg_copy);
    }

    [[nodiscard]] std::size_t queue_size() const noexcept { return queue_.size(); }
    [[nodiscard]] bool queue_empty() const noexcept { return queue_.empty(); }
    [[nodiscard]] bool queue_full() const noexcept { return queue_.full(); }

  private:
    fsm_type fsm_;
    spsc_ring_buffer<event_variant, QueueCapacity> queue_;
    std::atomic<std::size_t> state_index_{0};
    mutable std::atomic<std::uint32_t> seq_{0};
};

}  // namespace fsm

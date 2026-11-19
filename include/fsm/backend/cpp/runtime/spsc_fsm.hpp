#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#if !defined(FSMC_FREESTANDING)
#include <thread>
#endif
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
 * - **Producer (ISR / Sensor Task)**: Calls `post()` in Wait-Free O(1) time without acquiring locks or allocating
 * memory.
 * - **Consumer (Control Task / Worker)**: Calls `process_one()`, `run_until_empty()`, `step()`, `tick()`,
 *   and consumer-only introspection (`get_history()`, `timer_manager()`).
 * - **Readers (Any Thread)**: Inspect `state_index()`, `state_name()`, `is_in()`, `state_residence_time()`,
 *   `has_invariant_violation()`, and `snapshot_registers()` via race-free atomic word snapshots (zero UB under C++20/TSan).
 */
namespace detail {

template <typename T>
class atomic_word_snapshot {
    static_assert(std::is_trivially_copyable_v<T>, "atomic_word_snapshot requires trivially copyable T");
    static constexpr std::size_t kWords = (sizeof(T) + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t);

  public:
    atomic_word_snapshot() noexcept = default;

    void publish(const T& value) noexcept {
        std::array<std::uint64_t, kWords> raw{};
        std::memcpy(raw.data(), static_cast<const void*>(&value), sizeof(T));
        const std::uint64_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_seq_cst);
        for (std::size_t i = 0; i < kWords; ++i) {
            words_[i].store(raw[i], std::memory_order_release);
        }
        seq_.store(s + 2, std::memory_order_release);
    }

    [[nodiscard]] T load() const noexcept {
        std::array<std::uint64_t, kWords> raw{};
        for (;;) {
            const std::uint64_t s0 = seq_.load(std::memory_order_acquire);
            if ((s0 & 1ULL) != 0ULL) {
#if !defined(FSMC_FREESTANDING)
                std::this_thread::yield();
#endif
                continue;
            }
            for (std::size_t i = 0; i < kWords; ++i) {
                raw[i] = words_[i].load(std::memory_order_acquire);
            }
            const std::uint64_t s1 = seq_.load(std::memory_order_seq_cst);
            if (s0 == s1) {
                break;
            }
        }
        T out{};
        std::memcpy(static_cast<void*>(&out), raw.data(), sizeof(T));
        return out;
    }

  private:
    std::atomic<std::uint64_t> seq_{0};
    std::array<std::atomic<std::uint64_t>, kWords> words_{};
};

template <>
class atomic_word_snapshot<no_registers> {
  public:
    constexpr void publish(const no_registers&) noexcept {}
    [[nodiscard]] constexpr no_registers load() const noexcept { return no_registers{}; }
};
template <typename Variant, std::size_t... Is>
constexpr std::string_view get_state_name_by_index_impl(std::size_t idx, std::index_sequence<Is...>) noexcept {
    constexpr std::string_view names[] = {::fsm::get_state_name(std::variant_alternative_t<Is, Variant>{})...};
    if (idx < sizeof...(Is)) {
        return names[idx];
    }
    return "";
}

template <typename Variant>
constexpr std::string_view get_state_name_by_index(std::size_t idx) noexcept {
    return get_state_name_by_index_impl<Variant>(idx, std::make_index_sequence<std::variant_size_v<Variant>>{});
}

template <typename Variant, typename TargetState, std::size_t... Is>
constexpr bool is_substate_by_variant_index_impl(std::size_t idx, std::index_sequence<Is...>) noexcept {
    constexpr bool matches[] = {is_substate_of_v<std::variant_alternative_t<Is, Variant>, TargetState>...};
    if (idx < sizeof...(Is)) {
        return matches[idx];
    }
    return false;
}

template <typename Variant, typename TargetState>
constexpr bool is_substate_by_variant_index(std::size_t idx) noexcept {
    return is_substate_by_variant_index_impl<Variant, TargetState>(
        idx, std::make_index_sequence<std::variant_size_v<Variant>>{});
}

template <typename Variant, std::size_t... Is>
constexpr bool is_substate_by_name_variant_index_impl(std::size_t idx, std::string_view name,
                                                      std::index_sequence<Is...>) noexcept {
    bool matches[] = {state_is_or_descendant_of_static<std::variant_alternative_t<Is, Variant>>(name)...};
    if (idx < sizeof...(Is)) {
        return matches[idx];
    }
    return false;
}

template <typename Variant>
constexpr bool is_substate_by_name_variant_index(std::size_t idx, std::string_view name) noexcept {
    return is_substate_by_name_variant_index_impl<Variant>(idx, name,
                                                           std::make_index_sequence<std::variant_size_v<Variant>>{});
}
}  // namespace detail

template <typename Table, typename InPorts = no_ports, typename OutPorts = no_ports, typename Registers = no_registers,
          typename Services = no_services, std::size_t QueueCapacity = 64,
          typename InitialState = typename Table::initial_state, std::size_t DeferredCapacity = 16>
class spsc_fsm {
    static_assert((QueueCapacity > 1) && ((QueueCapacity & (QueueCapacity - 1)) == 0),
                  "spsc_fsm QueueCapacity must be a power of two");
    static_assert(
        std::is_trivially_copyable_v<Registers> || std::is_same_v<Registers, no_registers>,
        "spsc_fsm requires Registers to be trivially copyable for sound seqlock snapshots without torn reads.");
    static_assert(std::is_trivially_copyable_v<typename Table::event_variant>,
                  "spsc_fsm requires all Event payloads to be trivially copyable for lock-free ring buffer safety.");

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
    spsc_fsm() : fsm_() { publish_snapshot(); }

    explicit spsc_fsm(Services& srv, Table table = Table{}) : fsm_(srv, std::move(table)) { publish_snapshot(); }

    explicit spsc_fsm(Registers reg, Table table = Table{}) : fsm_(std::move(reg), std::move(table)) {
        publish_snapshot();
    }

    explicit spsc_fsm(Registers reg, Services& srv, Table table = Table{})
        : fsm_(std::move(reg), srv, std::move(table)) {
        publish_snapshot();
    }

    explicit spsc_fsm(InitialState initial, Table table = Table{}) : fsm_(std::move(initial), std::move(table)) {
        publish_snapshot();
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
        if constexpr (detail::has_is_valid_v<std::decay_t<Event>>) {
            if (!event.is_valid()) {
                return false;
            }
        }
        return queue_.push(event_variant{std::forward<Event>(event)});
    }

    [[nodiscard]] bool post(const event_variant& event) noexcept {
        const bool payload_ok = std::visit(
            [](const auto& evt) -> bool {
                using EvType = std::decay_t<decltype(evt)>;
                if constexpr (detail::has_is_valid_v<EvType>) {
                    return evt.is_valid();
                } else {
                    return true;
                }
            },
            event);
        if (!payload_ok) {
            return false;
        }
        return queue_.push(event);
    }

    // ========================================================================
    // Consumer API (Single Consumer / Dedicated Worker Thread)
    // ========================================================================

    bool process_one() {
        auto item = queue_.pop();
        if (!item.has_value()) {
            return false;
        }

        mutation_guard guard(*this);
        std::visit([this](const auto& evt) { (void)this->fsm_.dispatch(evt); }, *item);
        return true;
    }

    bool process_one(const in_ports_type& in, out_ports_type& out) {
        auto item = queue_.pop();
        if (!item.has_value()) {
            return false;
        }

        mutation_guard guard(*this);
        std::visit([this, &in, &out](const auto& evt) { (void)this->fsm_.dispatch(evt, in, out); }, *item);
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
        mutation_guard guard(*this);
        return fsm_.step(in, out, srv);
    }

    template <typename DurationRep>
    step_result step(DurationRep dt, const in_ports_type& in, out_ports_type& out, services_type& srv) {
        mutation_guard guard(*this);
        return fsm_.step(dt, in, out, srv);
    }

    step_result step(const in_ports_type& in, out_ports_type& out) {
        mutation_guard guard(*this);
        return fsm_.step(in, out);
    }

    template <typename DurationRep>
    step_result step(DurationRep dt, const in_ports_type& in, out_ports_type& out) {
        mutation_guard guard(*this);
        return fsm_.step(dt, in, out);
    }

    step_result step() {
        mutation_guard guard(*this);
        return fsm_.step();
    }

    template <typename DurationRep>
    step_result step(DurationRep dt) {
        mutation_guard guard(*this);
        return fsm_.step(dt);
    }

    std::size_t tick(std::uint64_t delta_ms) {
        mutation_guard guard(*this);
        return fsm_.tick(delta_ms);
    }

    template <typename Callback>
    std::size_t tick(std::uint64_t delta_ms, Callback on_expired) {
        mutation_guard guard(*this);
        return fsm_.tick(delta_ms, on_expired);
    }

    template <typename Rep, typename Period>
    std::size_t tick(std::chrono::duration<Rep, Period> dt) {
        mutation_guard guard(*this);
        return fsm_.tick(dt);
    }

    template <typename Rep, typename Period, typename Callback>
    std::size_t tick(std::chrono::duration<Rep, Period> dt, Callback on_expired) {
        mutation_guard guard(*this);
        return fsm_.tick(dt, on_expired);
    }

    [[nodiscard]] auto& timer_manager() noexcept { return fsm_.timer_manager(); }
    [[nodiscard]] const auto& timer_manager() const noexcept { return fsm_.timer_manager(); }

    // ========================================================================
    // Read & Introspection API
    // ========================================================================

    void reset() {
        while (queue_.pop().has_value()) {
        }
        mutation_guard guard(*this);
        fsm_.reset();
    }

    void reset(registers_type initial_registers) {
        while (queue_.pop().has_value()) {
        }
        mutation_guard guard(*this);
        fsm_.reset(std::move(initial_registers));
    }

    void clear_history() noexcept {
        mutation_guard guard(*this);
        fsm_.clear_history();
    }

    /// Consumer-thread-only history inspection (non-concurrent; do not call from reader threads).
    [[nodiscard]] std::string_view get_history(std::string_view parent) const noexcept {
        return fsm_.get_history(parent);
    }

    /// Consumer-thread-only typed history inspection (non-concurrent; do not call from reader threads).
    template <typename ParentState>
    [[nodiscard]] std::string_view get_history() const noexcept {
        return fsm_.template get_history<ParentState>();
    }

    void clear_deferred() noexcept {
        mutation_guard guard(*this);
        fsm_.clear_deferred();
    }

    void clear_deferred_events() noexcept {
        mutation_guard guard(*this);
        fsm_.clear_deferred_events();
    }

    void set_invariant_violation_handler(void (*fn)(const invariant_violation_info&)) noexcept {
        mutation_guard guard(*this);
        fsm_.set_invariant_violation_handler(fn);
    }

    void set_invariant_violation_handler(void (*fn)(void*, const invariant_violation_info&), void* ctx) noexcept {
        mutation_guard guard(*this);
        fsm_.set_invariant_violation_handler(fn, ctx);
    }

#if !defined(FSMC_FREESTANDING)
    void set_invariant_violation_handler(std::function<void(const invariant_violation_info&)> handler) {
        mutation_guard guard(*this);
        fsm_.set_invariant_violation_handler(std::move(handler));
    }
#endif

    // ========================================================================
    // Concurrent Telemetry & State Inspection API (Lock-Free Atomic Snapshots)
    // Safe to invoke from external monitoring, telemetry, or reader threads.
    // ========================================================================

    [[nodiscard]] std::size_t state_index() const noexcept { return state_index_.load(std::memory_order_acquire); }
    [[nodiscard]] std::string_view state_name() const noexcept {
        return detail::get_state_name_by_index<typename Table::state_variant>(state_index());
    }

    template <typename State>
    [[nodiscard]] bool is_in() const noexcept {
        return detail::is_substate_by_variant_index<typename Table::state_variant, State>(state_index());
    }

    [[nodiscard]] bool is_in(std::string_view target_name) const noexcept {
        return detail::is_substate_by_name_variant_index<typename Table::state_variant>(state_index(), target_name);
    }

    template <typename State>
    [[nodiscard]] bool is_in_state() const noexcept {
        if constexpr (Table::template has_state<State>) {
            return state_index() == type_list_index_of_v<State, typename Table::states>;
        } else {
            return false;
        }
    }

    [[nodiscard]] std::uint64_t state_residence_time() const noexcept {
        return residence_time_ms_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool has_invariant_violation() const noexcept {
        return has_invariant_violation_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool is_invariant_satisfied() const noexcept { return !has_invariant_violation(); }
    /// Consumer-thread-only detailed invariant violation inspection.
    [[nodiscard]] std::optional<invariant_violation_info> last_invariant_violation() const noexcept {
        return fsm_.last_invariant_violation();
    }

    /**
     * @brief Captures a consistent snapshot of internal registers without blocking the consumer thread.
     * Uses atomic word-level snapshot storage so concurrent reads and writes are 100% data-race-free
     * under the C++20 memory model and ThreadSanitizer.
     */
    [[nodiscard]] Registers snapshot_registers() const noexcept {
        if constexpr (std::is_same_v<Registers, no_registers>) {
            return no_registers{};
        } else {
            return registers_snapshot_.load();
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
    void publish_snapshot() noexcept {
        if constexpr (!std::is_same_v<Registers, no_registers>) {
            registers_snapshot_.publish(fsm_.registers());
        }
        residence_time_ms_.store(fsm_.state_residence_time(), std::memory_order_release);
        has_invariant_violation_.store(fsm_.has_invariant_violation(), std::memory_order_release);
        state_index_.store(fsm_.get_current_state_variant().index(), std::memory_order_release);
    }

    struct mutation_guard {
        explicit mutation_guard(spsc_fsm& owner) noexcept : owner_(owner) {}
        ~mutation_guard() noexcept { owner_.publish_snapshot(); }
        mutation_guard(const mutation_guard&) = delete;
        mutation_guard& operator=(const mutation_guard&) = delete;

      private:
        spsc_fsm& owner_;
    };

    fsm_type fsm_;
    spsc_ring_buffer<event_variant, QueueCapacity> queue_;
    detail::atomic_word_snapshot<Registers> registers_snapshot_{};
    std::atomic<std::size_t> state_index_{0};
    std::atomic<std::uint64_t> residence_time_ms_{0};
    std::atomic<bool> has_invariant_violation_{false};
};

}  // namespace fsm

#include "fsm/backend/cpp/runtime/detail/spsc_policy_adapter.hpp"

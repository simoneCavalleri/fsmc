#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <type_traits>
#if !defined(FSMC_FREESTANDING)
#include <iomanip>
#include <ostream>
#endif

#include "fsm/backend/cpp/runtime/traits/dispatch_result.hpp"

namespace fsm {

/**
 * @brief Formal audit trace entry for embedded flight recorder.
 */
struct TraceEntry {
    std::uint64_t tick{0};
    std::string_view source_state{};
    std::string_view event_name{};
    std::string_view target_state{};
    bool transition_taken{true};

    constexpr bool operator==(const TraceEntry& other) const noexcept {
        return tick == other.tick && source_state == other.source_state && event_name == other.event_name &&
               target_state == other.target_state && transition_taken == other.transition_taken;
    }
};

/**
 * @brief Lock-Free Zero-Allocation Circular Ring Buffer Flight Recorder.
 *
 * Implements deterministic black-box flight data recording for safety-critical
 * and hard real-time systems. Uses atomic word-level slot storage so concurrent
 * single-producer `record()` and external reader `operator[]()` / `last_entry()` / `dump()`
 * execute without data races or heap allocations.
 *
 * @tparam Capacity Fixed compile-time ring buffer capacity (must be > 0).
 */
template <std::size_t Capacity = 64>
class TraceBuffer {
    static_assert(Capacity > 0, "TraceBuffer Capacity must be greater than 0");
    static_assert(std::is_trivially_copyable_v<TraceEntry>, "TraceEntry must be trivially copyable");

    static constexpr std::size_t kEntryWords = (sizeof(TraceEntry) + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t);

    struct AtomicSlot {
        std::atomic<std::uint64_t> seq{0};
        std::array<std::atomic<std::uint64_t>, kEntryWords> words{};

        void store(const TraceEntry& entry) noexcept {
            std::array<std::uint64_t, kEntryWords> raw{};
            std::memcpy(raw.data(), static_cast<const void*>(&entry), sizeof(TraceEntry));
            const std::uint64_t s = seq.load(std::memory_order_relaxed);
            seq.store(s + 1, std::memory_order_seq_cst);
            for (std::size_t i = 0; i < kEntryWords; ++i) {
                words[i].store(raw[i], std::memory_order_release);
            }
            seq.store(s + 2, std::memory_order_release);
        }

        [[nodiscard]] TraceEntry load() const noexcept {
            std::array<std::uint64_t, kEntryWords> raw{};
            for (;;) {
                const std::uint64_t s0 = seq.load(std::memory_order_acquire);
                if ((s0 & 1ULL) != 0ULL) {
                    continue;
                }
                for (std::size_t i = 0; i < kEntryWords; ++i) {
                    raw[i] = words[i].load(std::memory_order_acquire);
                }
                const std::uint64_t s1 = seq.load(std::memory_order_seq_cst);
                if (s0 == s1) {
                    break;
                }
            }
            TraceEntry out{};
            std::memcpy(static_cast<void*>(&out), raw.data(), sizeof(TraceEntry));
            return out;
        }
    };

  public:
    static constexpr std::size_t capacity_val = Capacity;

    TraceBuffer() noexcept = default;

    /**
     * @brief Records a new transition event into the ring buffer.
     * Overwrites oldest entry when capacity is reached.
     */
    void record(std::uint64_t tick, std::string_view source, std::string_view event, std::string_view target,
                bool taken = true) noexcept {
        const std::size_t total = total_written_.load(std::memory_order_relaxed);
        buffer_[total % Capacity].store(TraceEntry{tick, source, event, target, taken});
        total_written_.store(total + 1, std::memory_order_release);
    }

    void push(const TraceEntry& entry) noexcept {
        record(entry.tick, entry.source_state, entry.event_name, entry.target_state, entry.transition_taken);
    }

    [[nodiscard]] std::size_t size() const noexcept {
        const std::size_t total = total_written_.load(std::memory_order_acquire);
        return (total < Capacity) ? total : Capacity;
    }
    [[nodiscard]] constexpr std::size_t capacity() const noexcept { return Capacity; }
    [[nodiscard]] bool empty() const noexcept { return total_written_.load(std::memory_order_acquire) == 0; }
    [[nodiscard]] bool full() const noexcept { return total_written_.load(std::memory_order_acquire) >= Capacity; }

    void clear() noexcept { total_written_.store(0, std::memory_order_release); }

    /**
     * @brief Accesses recorded entry in chronological order (0 = oldest recorded entry).
     */
    [[nodiscard]] TraceEntry operator[](std::size_t index) const noexcept {
        const std::size_t total = total_written_.load(std::memory_order_acquire);
        const std::size_t start = (total >= Capacity) ? (total % Capacity) : 0;
        const std::size_t actual_idx = (start + index) % Capacity;
        return buffer_[actual_idx].load();
    }

    /**
     * @brief Returns the most recent recorded trace entry, if any.
     */
    [[nodiscard]] std::optional<TraceEntry> last_entry() const noexcept {
        const std::size_t total = total_written_.load(std::memory_order_acquire);
        if (total == 0) {
            return std::nullopt;
        }
        const std::size_t last_idx = (total - 1) % Capacity;
        return buffer_[last_idx].load();
    }

#if !defined(FSMC_FREESTANDING)
    /**
     * @brief Formats human-readable flight recorder dump table to output stream.
     */
    void dump(std::ostream& os) const {
        constexpr std::string_view kSeparator =
            "--------------------------------------------------------------------------------------------";
        static_assert(kSeparator.size() == 92);
        const std::size_t count = size();
        os << "=== FSM Flight Recorder Audit Trace (" << count << "/" << Capacity << " entries) ===\n";
        os << std::left << std::setw(10) << "TICK" << std::setw(25) << "SOURCE" << std::setw(25) << "EVENT"
           << std::setw(25) << "TARGET" << "STATUS\n";
        os << kSeparator << "\n";

        for (std::size_t i = 0; i < count; ++i) {
            const auto entry = (*this)[i];
            os << std::left << std::setw(10) << entry.tick << std::setw(25) << entry.source_state << std::setw(25)
               << entry.event_name << std::setw(25) << entry.target_state
               << (entry.transition_taken ? "TAKEN" : "IGNORED") << "\n";
        }
    }
#endif

  private:
    std::array<AtomicSlot, Capacity> buffer_{};
    std::atomic<std::size_t> total_written_{0};
};

/**
 * @brief Telemetry Observer adapter that writes transitions directly into a Flight Recorder.
 */
template <std::size_t Capacity = 64>
class flight_recorder_observer {
  public:
    explicit flight_recorder_observer(std::uint64_t initial_tick = 0) noexcept : current_tick_(initial_tick) {}

    void set_tick(std::uint64_t tick) noexcept { current_tick_.store(tick, std::memory_order_relaxed); }
    void advance_tick(std::uint64_t dt = 1) noexcept { current_tick_.fetch_add(dt, std::memory_order_relaxed); }

    void operator()(const transition_info& info) noexcept {
        recorder_.record(current_tick_.load(std::memory_order_relaxed), info.source, info.event, info.target,
                         info.status == dispatch_status::success);
    }

    void operator()(const transition_info& info) const noexcept {
        recorder_.record(current_tick_.load(std::memory_order_relaxed), info.source, info.event, info.target,
                         info.status == dispatch_status::success);
    }

    template <typename State, typename Event>
    void on_transition(const State& /*src*/, const Event& /*evt*/, std::string_view src_name, std::string_view evt_name,
                       std::string_view dst_name) noexcept {
        recorder_.record(current_tick_.load(std::memory_order_relaxed), src_name, evt_name, dst_name, true);
    }

    [[nodiscard]] const TraceBuffer<Capacity>& recorder() const noexcept { return recorder_; }
    [[nodiscard]] TraceBuffer<Capacity>& recorder() noexcept { return recorder_; }

#if !defined(FSMC_FREESTANDING)
    void dump(std::ostream& os) const { recorder_.dump(os); }
#endif

  private:
    std::atomic<std::uint64_t> current_tick_{0};
    mutable TraceBuffer<Capacity> recorder_{};
};

}  // namespace fsm

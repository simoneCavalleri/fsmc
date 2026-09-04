#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <ostream>
#include <string_view>

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
 * and hard real-time systems. Guaranteed O(1) push and read complexity with zero
 * dynamic heap allocation.
 *
 * @tparam Capacity Fixed compile-time ring buffer capacity.
 */
template <std::size_t Capacity = 64>
class TraceBuffer {
  public:
    static constexpr std::size_t capacity_val = Capacity;

    constexpr TraceBuffer() noexcept = default;

    /**
     * @brief Records a new transition event into the ring buffer.
     * Overwrites oldest entry when capacity is reached.
     */
    constexpr void record(std::uint64_t tick, std::string_view source, std::string_view event, std::string_view target,
                          bool taken = true) noexcept {
        buffer_[head_] = TraceEntry{tick, source, event, target, taken};
        head_ = (head_ + 1) % Capacity;
        if (count_ < Capacity) {
            ++count_;
        }
    }

    constexpr void push(const TraceEntry& entry) noexcept {
        record(entry.tick, entry.source_state, entry.event_name, entry.target_state, entry.transition_taken);
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return count_; }
    [[nodiscard]] constexpr std::size_t capacity() const noexcept { return Capacity; }
    [[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] constexpr bool full() const noexcept { return count_ == Capacity; }

    constexpr void clear() noexcept {
        head_ = 0;
        count_ = 0;
    }

    /**
     * @brief Accesses recorded entry in chronological order (0 = oldest recorded entry).
     */
    [[nodiscard]] constexpr const TraceEntry& operator[](std::size_t index) const noexcept {
        std::size_t start = (count_ == Capacity) ? head_ : 0;
        std::size_t actual_idx = (start + index) % Capacity;
        return buffer_[actual_idx];
    }

    /**
     * @brief Returns the most recent recorded trace entry, if any.
     */
    [[nodiscard]] constexpr std::optional<TraceEntry> last_entry() const noexcept {
        if (count_ == 0) {
            return std::nullopt;
        }
        std::size_t last_idx = (head_ + Capacity - 1) % Capacity;
        return buffer_[last_idx];
    }

    /**
     * @brief Formats human-readable flight recorder dump table to output stream.
     */
    void dump(std::ostream& os) const {
        os << "=== FSM Flight Recorder Audit Trace (" << count_ << "/" << Capacity << " entries) ===\n";
        os << std::left << std::setw(10) << "TICK" << std::setw(25) << "SOURCE" << std::setw(25) << "EVENT"
           << std::setw(25) << "TARGET" << "STATUS\n";
        os << std::string(92, '-') << "\n";

        for (std::size_t i = 0; i < count_; ++i) {
            const auto& entry = (*this)[i];
            os << std::left << std::setw(10) << entry.tick << std::setw(25) << entry.source_state << std::setw(25)
               << entry.event_name << std::setw(25) << entry.target_state
               << (entry.transition_taken ? "TAKEN" : "IGNORED") << "\n";
        }
    }

  private:
    std::array<TraceEntry, Capacity> buffer_{};
    std::size_t head_{0};
    std::size_t count_{0};
};

/**
 * @brief Telemetry Observer adapter that writes transitions directly into a Flight Recorder.
 */
template <std::size_t Capacity = 64>
class flight_recorder_observer {
  public:
    explicit flight_recorder_observer(std::uint64_t initial_tick = 0) noexcept : current_tick_(initial_tick) {}

    void set_tick(std::uint64_t tick) noexcept { current_tick_ = tick; }
    void advance_tick(std::uint64_t dt = 1) noexcept { current_tick_ += dt; }

    void operator()(const transition_info& info) noexcept {
        recorder_.record(current_tick_, info.source, info.event, info.target,
                         info.status == dispatch_status::success);
    }

    void operator()(const transition_info& info) const noexcept {
        const_cast<TraceBuffer<Capacity>&>(recorder_).record(
            current_tick_, info.source, info.event, info.target,
            info.status == dispatch_status::success);
    }

    template <typename State, typename Event>
    void on_transition(const State& /*src*/, const Event& /*evt*/, std::string_view src_name, std::string_view evt_name,
                       std::string_view dst_name) noexcept {
        recorder_.record(current_tick_, src_name, evt_name, dst_name, true);
    }

    [[nodiscard]] const TraceBuffer<Capacity>& recorder() const noexcept { return recorder_; }
    [[nodiscard]] TraceBuffer<Capacity>& recorder() noexcept { return recorder_; }

    void dump(std::ostream& os) const { recorder_.dump(os); }

  private:
    std::uint64_t current_tick_{0};
    TraceBuffer<Capacity> recorder_{};
};

}  // namespace fsm

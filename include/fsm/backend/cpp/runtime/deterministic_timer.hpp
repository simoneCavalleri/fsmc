#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace fsm {

/**
 * @brief Deterministic, zero-allocation tick-based timer entry.
 */
struct timer_entry {
    std::uint32_t timer_id{0};
    std::uint64_t interval_ms{0};
    std::uint64_t elapsed_ms{0};
    bool periodic{false};
    bool active{false};
};

/**
 * @brief Zero-heap, deterministic tick-based timer manager.
 *
 * Designed for hard real-time and safety-critical embedded systems where background
 * threads (such as std::thread or POSIX timers) are prohibited. Timers are stepped
 * synchronously via tick() calls.
 *
 * @tparam MaxTimers Maximum number of concurrent active timers (statically allocated).
 */
template <std::size_t MaxTimers = 32>
class deterministic_timer_manager {
  public:
    static constexpr std::size_t max_timers = MaxTimers;

    constexpr deterministic_timer_manager() noexcept = default;

    /**
     * @brief Starts or restarts a timer.
     * @param timer_id Unique identifier for the timer (e.g. state hash or transition ID).
     * @param duration_ms Timeout duration in milliseconds.
     * @param periodic If true, restarts automatically upon expiration.
     * @return true if successfully scheduled, false if max timer capacity reached.
     */
    constexpr bool start_timer(std::uint32_t timer_id, std::uint64_t duration_ms, bool periodic = false) noexcept {
        // Check if timer already exists
        for (auto& entry : timers_) {
            if (entry.active && entry.timer_id == timer_id) {
                entry.interval_ms = duration_ms;
                entry.elapsed_ms = 0;
                entry.periodic = periodic;
                return true;
            }
        }
        // Find empty slot
        for (auto& entry : timers_) {
            if (!entry.active) {
                entry.timer_id = timer_id;
                entry.interval_ms = duration_ms;
                entry.elapsed_ms = 0;
                entry.periodic = periodic;
                entry.active = true;
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Dynamically updates the interval duration of an active timer.
     */
    constexpr bool set_timer_duration(std::uint32_t timer_id, std::uint64_t duration_ms) noexcept {
        for (auto& entry : timers_) {
            if (entry.active && entry.timer_id == timer_id) {
                entry.interval_ms = duration_ms;
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Cancels an active timer by ID.
     */
    constexpr bool cancel_timer(std::uint32_t timer_id) noexcept {
        for (auto& entry : timers_) {
            if (entry.active && entry.timer_id == timer_id) {
                entry.active = false;
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Resets all timers.
     */
    constexpr void reset() noexcept {
        ++generation_;
        for (auto& entry : timers_) {
            entry.active = false;
            entry.elapsed_ms = 0;
        }
    }

    /**
     * @brief Checks if a specific timer is active.
     */
    [[nodiscard]] constexpr bool is_timer_active(std::uint32_t timer_id) const noexcept {
        for (const auto& entry : timers_) {
            if (entry.active && entry.timer_id == timer_id) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Advances time by delta_ms and invokes callback on expired timers.
     * @tparam Callback Callable with signature void(std::uint32_t timer_id)
     * @param delta_ms Elapsed milliseconds to advance.
     * @param on_expired Functor called for each expired timer.
     * @return Number of expired timers in this tick step.
     */
    template <typename Callback>
    std::size_t tick(std::uint64_t delta_ms, Callback on_expired) {
        std::array<std::uint32_t, MaxTimers> expired_ids{};
        std::size_t expired_count = 0;
        const auto start_gen = generation_;

        for (auto& entry : timers_) {
            if (!entry.active) {
                continue;
            }
            entry.elapsed_ms += delta_ms;
            if (entry.elapsed_ms >= entry.interval_ms) {
                expired_ids[expired_count++] = entry.timer_id;
                if (entry.periodic) {
                    entry.elapsed_ms = (entry.interval_ms > 0) ? (entry.elapsed_ms % entry.interval_ms) : 0;
                } else {
                    entry.active = false;
                }
            }
        }

        std::size_t dispatched = 0;
        for (std::size_t i = 0; i < expired_count; ++i) {
            if (generation_ != start_gen) {
                // The timer manager was reset/re-armed (e.g. state transition occurred).
                // Remaining expired timers from the previous state are obsolete.
                break;
            }
            on_expired(expired_ids[i]);
            ++dispatched;
        }
        return dispatched;
    }

    [[nodiscard]] constexpr std::size_t active_count() const noexcept {
        std::size_t count = 0;
        for (const auto& entry : timers_) {
            if (entry.active) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] constexpr const std::array<timer_entry, MaxTimers>& entries() const noexcept { return timers_; }

    constexpr bool restore_timer(std::uint32_t timer_id, std::uint64_t interval_ms, std::uint64_t elapsed_ms,
                                 bool periodic) noexcept {
        for (auto& entry : timers_) {
            if (!entry.active) {
                entry.timer_id = timer_id;
                entry.interval_ms = interval_ms;
                entry.elapsed_ms = elapsed_ms;
                entry.periodic = periodic;
                entry.active = true;
                return true;
            }
        }
        return false;
    }

  private:
    std::array<timer_entry, MaxTimers> timers_{};
    std::uint32_t generation_{0};
};

/**
 * @brief Zero-overhead specialization for FSMs without deterministic timers (0 bytes).
 */
template <>
class deterministic_timer_manager<0> {
  public:
    static constexpr std::size_t max_timers = 0;

    constexpr deterministic_timer_manager() noexcept = default;

    constexpr bool start_timer(std::uint32_t /*timer_id*/, std::uint64_t /*duration_ms*/,
                               bool /*periodic*/ = false) noexcept {
        return false;
    }

    constexpr bool cancel_timer(std::uint32_t /*timer_id*/) noexcept { return false; }

    constexpr void reset() noexcept {}

    [[nodiscard]] constexpr bool is_timer_active(std::uint32_t /*timer_id*/) const noexcept { return false; }

    template <typename Callback>
    std::size_t tick(std::uint64_t /*delta_ms*/, Callback /*on_expired*/) noexcept {
        return 0;
    }

    [[nodiscard]] constexpr std::size_t active_count() const noexcept { return 0; }

    [[nodiscard]] const std::array<timer_entry, 0>& entries() const noexcept {
        static const std::array<timer_entry, 0> dummy{};
        return dummy;
    }

    constexpr bool restore_timer(std::uint32_t /*timer_id*/, std::uint64_t /*interval_ms*/,
                                 std::uint64_t /*elapsed_ms*/, bool /*periodic*/) noexcept {
        return false;
    }
};

}  // namespace fsm

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
        std::size_t expired_count = 0;
        for (auto& entry : timers_) {
            if (!entry.active) {
                continue;
            }
            entry.elapsed_ms += delta_ms;
            if (entry.elapsed_ms >= entry.interval_ms) {
                ++expired_count;
                std::uint32_t id = entry.timer_id;
                if (entry.periodic) {
                    entry.elapsed_ms = entry.elapsed_ms % entry.interval_ms;
                } else {
                    entry.active = false;
                }
                on_expired(id);
            }
        }
        return expired_count;
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

  private:
    std::array<timer_entry, MaxTimers> timers_{};
};

}  // namespace fsm

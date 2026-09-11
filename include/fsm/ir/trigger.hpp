/**
 * @file trigger.hpp
 * @brief Event, Signal, and Timed Automata Trigger Metamodels in FSM IR.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "fsm/ir/guard.hpp"

namespace fsm::ir {

/**
 * @brief High-level classification of trigger mechanisms initiating state transitions.
 */
enum class TriggerType : std::uint8_t {
    Signal,     ///< Asynchronous or synchronous signal/event reception
    TimeAfter,  ///< Single-shot delay timer relative to state entry: after(duration)
    TimeEvery,  ///< Periodic recurring timer: every(duration)
    TimeAt,     ///< Absolute wall-clock schedule trigger: at(time)
    Change,     ///< Continuous boolean condition change: when(predicate)
    Anonymous   ///< Unconditional / completion transition immediately evaluated upon state quiescence
};

/**
 * @brief Classification of timed trigger execution policies.
 */
enum class TimeTriggerKind : std::uint8_t {
    After,  ///< Single-shot timer (after(duration))
    Every,  ///< Recurring periodic timer (every(duration))
    At      ///< Absolute wall-clock point (at(timestamp))
};

/**
 * @brief Physical time units for formal timed automata transitions and state invariants.
 */
enum class TimeUnit : std::uint8_t {
    Nanoseconds,   ///< ns
    Microseconds,  ///< us
    Milliseconds,  ///< ms
    Seconds,       ///< s
    Minutes        ///< min
};

/**
 * @brief Converts a TimeUnit enum to its canonical string abbreviation.
 */
[[nodiscard]] inline std::string_view time_unit_to_string(TimeUnit unit) noexcept {
    switch (unit) {
        case TimeUnit::Nanoseconds:
            return "ns";
        case TimeUnit::Microseconds:
            return "us";
        case TimeUnit::Milliseconds:
            return "ms";
        case TimeUnit::Seconds:
            return "s";
        case TimeUnit::Minutes:
            return "min";
    }
    return "ms";
}

/**
 * @brief Parses a string into its corresponding TimeUnit enum.
 */
[[nodiscard]] inline TimeUnit time_unit_from_string(std::string_view str) noexcept {
    if (str == "ns" || str == "nanoseconds" || str == "nanosecond")
        return TimeUnit::Nanoseconds;
    if (str == "us" || str == "microseconds" || str == "microsecond")
        return TimeUnit::Microseconds;
    if (str == "ms" || str == "milliseconds" || str == "millisecond")
        return TimeUnit::Milliseconds;
    if (str == "s" || str == "seconds" || str == "second" || str == "sec")
        return TimeUnit::Seconds;
    if (str == "min" || str == "minutes" || str == "minute")
        return TimeUnit::Minutes;
    return TimeUnit::Milliseconds;
}

/**
 * @brief Converts a TimeTriggerKind enum to its string label.
 */
[[nodiscard]] inline std::string_view time_trigger_kind_to_string(TimeTriggerKind kind) noexcept {
    switch (kind) {
        case TimeTriggerKind::After:
            return "after";
        case TimeTriggerKind::Every:
            return "every";
        case TimeTriggerKind::At:
            return "at";
    }
    return "after";
}

/**
 * @brief Parses a string into a TimeTriggerKind enum.
 */
[[nodiscard]] inline TimeTriggerKind time_trigger_kind_from_string(std::string_view str) noexcept {
    if (str == "every" || str == "periodic" || str == "TimeEvery")
        return TimeTriggerKind::Every;
    if (str == "at" || str == "TimeAt")
        return TimeTriggerKind::At;
    return TimeTriggerKind::After;
}

/**
 * @brief Converts a duration and time unit into integer milliseconds.
 */
[[nodiscard]] inline std::uint64_t to_milliseconds(std::uint64_t val, TimeUnit unit) noexcept {
    switch (unit) {
        case TimeUnit::Nanoseconds:
            return val / 1'000'000ULL;
        case TimeUnit::Microseconds:
            return val / 1'000ULL;
        case TimeUnit::Milliseconds:
            return val;
        case TimeUnit::Seconds:
            return val * 1'000ULL;
        case TimeUnit::Minutes:
            return val * 60'000ULL;
    }
    return val;
}

/**
 * @brief Signal trigger initiated by an explicit event or payload message.
 */
struct SignalTrigger {
    std::string signal_name;                 ///< Name of the triggering signal
    std::string payload_binding{"payload"};  ///< Name of payload argument binding (e.g. "payload")
    std::vector<std::pair<std::string, std::string>> payload_parameters;  ///< Parameter bindings: (name, type)
    std::string payload_type;  ///< Concrete or canonical type name of the payload

    SignalTrigger() = default;
    SignalTrigger(std::string name, std::string binding = "payload")
        : signal_name(std::move(name)), payload_binding(std::move(binding)) {}
    SignalTrigger(std::string name, std::vector<std::pair<std::string, std::string>> params,
                  std::string binding = "payload")
        : signal_name(std::move(name)), payload_binding(std::move(binding)), payload_parameters(std::move(params)) {}

    bool operator==(const SignalTrigger& other) const noexcept {
        return signal_name == other.signal_name && payload_binding == other.payload_binding &&
               payload_parameters == other.payload_parameters && payload_type == other.payload_type;
    }
};

/**
 * @brief Timed Automata trigger representing periodic ticks or single-shot state timeouts.
 */
struct TimeTrigger {
    TimeTriggerKind kind{TimeTriggerKind::After};  ///< Timeout policy: After, Every, At
    std::uint64_t duration_value{0};               ///< Numeric duration value
    TimeUnit unit{TimeUnit::Milliseconds};         ///< Duration physical unit
    std::string dynamic_expression;                ///< Dynamic timeout expression if variable (e.g. "timeout_var * 2")
    std::uint64_t duration_ms{0};                  ///< Precomputed duration in milliseconds for O(1) runtime execution
    bool periodic{false};                          ///< True for recurring every(ms), false for single-shot after(ms)

    TimeTrigger() = default;

    TimeTrigger(std::uint64_t dur_ms, bool is_periodic = false)
        : kind(is_periodic ? TimeTriggerKind::Every : TimeTriggerKind::After),
          duration_value(dur_ms),
          unit(TimeUnit::Milliseconds),
          duration_ms(dur_ms),
          periodic(is_periodic) {}

    TimeTrigger(TimeTriggerKind trigger_kind, std::uint64_t val, TimeUnit time_unit = TimeUnit::Milliseconds,
                std::string dyn_expr = "")
        : kind(trigger_kind),
          duration_value(val),
          unit(time_unit),
          dynamic_expression(std::move(dyn_expr)),
          duration_ms(to_milliseconds(val, time_unit)),
          periodic(trigger_kind == TimeTriggerKind::Every) {}

    /**
     * @brief Computes or retrieves duration normalized to integer milliseconds.
     */
    [[nodiscard]] std::uint64_t duration_in_ms() const noexcept {
        return duration_ms > 0 ? duration_ms : to_milliseconds(duration_value, unit);
    }

    /**
     * @brief Checks whether the timer is recurring.
     */
    [[nodiscard]] bool is_periodic() const noexcept { return periodic || kind == TimeTriggerKind::Every; }

    bool operator==(const TimeTrigger& other) const noexcept {
        return kind == other.kind && duration_value == other.duration_value && unit == other.unit &&
               dynamic_expression == other.dynamic_expression && duration_ms == other.duration_ms &&
               periodic == other.periodic;
    }
};

/**
 * @brief Continuous condition change trigger firing on boolean predicate false -> true transitions.
 */
struct ChangeTrigger {
    std::string raw_expression;                           ///< Raw boolean predicate text (e.g. "altitude > 10000")
    std::optional<GuardAstNode> predicate{std::nullopt};  ///< Structured boolean predicate AST
    bool active_on_true{true};                            ///< True for rising-edge (false -> true) trigger

    ChangeTrigger() = default;
    /* implicit */ ChangeTrigger(std::string expr, bool on_true = true)
        : raw_expression(expr),
          predicate(expr.empty() ? std::nullopt : std::make_optional(GuardAstNode(expr))),
          active_on_true(on_true) {}
    ChangeTrigger(GuardAstNode pred, bool on_true = true)
        : raw_expression(pred.to_string()), predicate(std::move(pred)), active_on_true(on_true) {}

    bool operator==(const ChangeTrigger& other) const noexcept {
        return raw_expression == other.raw_expression && active_on_true == other.active_on_true &&
               predicate == other.predicate;
    }
};

/**
 * @brief Anonymous / Completion trigger firing immediately when internal state activities complete.
 */
struct AnonymousTrigger {
    bool is_completion{true};               ///< True if standard completion transition
    std::string description{"completion"};  ///< Human-readable documentation comment

    constexpr bool operator==(const AnonymousTrigger& other) const noexcept {
        return is_completion == other.is_completion;
    }
};

/**
 * @brief Variant type encapsulating all possible trigger mechanisms in FSM IR.
 */
using TriggerVariant = std::variant<SignalTrigger, TimeTrigger, ChangeTrigger, AnonymousTrigger>;

}  // namespace fsm::ir

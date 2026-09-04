#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace fsm::codegen {

// ============================================================================
// Triggers (Signal, Timed, Anonymous)
// ============================================================================

enum class TriggerType : std::uint8_t { Signal, TimeAfter, TimeEvery, TimeAt, Anonymous };

enum class TimeTriggerKind : std::uint8_t { After, Every, At };

enum class TimeUnit : std::uint8_t { Nanoseconds, Microseconds, Milliseconds, Seconds, Minutes };

inline std::string_view time_unit_to_string(TimeUnit unit) noexcept {
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

inline TimeUnit time_unit_from_string(std::string_view str) noexcept {
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

inline std::string_view time_trigger_kind_to_string(TimeTriggerKind kind) noexcept {
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

inline TimeTriggerKind time_trigger_kind_from_string(std::string_view str) noexcept {
    if (str == "every" || str == "periodic" || str == "TimeEvery")
        return TimeTriggerKind::Every;
    if (str == "at" || str == "TimeAt")
        return TimeTriggerKind::At;
    return TimeTriggerKind::After;
}

inline std::uint64_t to_milliseconds(std::uint64_t val, TimeUnit unit) noexcept {
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

struct SignalTrigger {
    std::string signal_name;
    std::string payload_binding{"payload"};                               ///< Name of payload argument, e.g. "payload"
    std::vector<std::pair<std::string, std::string>> payload_parameters;  ///< Parameter bindings: (name, type)
    std::string payload_type;

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

struct TimeTrigger {
    TimeTriggerKind kind{TimeTriggerKind::After};
    std::uint64_t duration_value{0};
    TimeUnit unit{TimeUnit::Milliseconds};
    std::string dynamic_expression;  ///< Dynamic timeout expression if variable (e.g. "timeout_var * 2")
    std::uint64_t duration_ms{0};    ///< Precomputed duration in milliseconds for fast execution
    bool periodic{false};            ///< True for every(ms), false for after(ms)

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

    [[nodiscard]] std::uint64_t duration_in_ms() const noexcept {
        return duration_ms > 0 ? duration_ms : to_milliseconds(duration_value, unit);
    }

    [[nodiscard]] bool is_periodic() const noexcept { return periodic || kind == TimeTriggerKind::Every; }

    bool operator==(const TimeTrigger& other) const noexcept {
        return kind == other.kind && duration_value == other.duration_value && unit == other.unit &&
               dynamic_expression == other.dynamic_expression && duration_ms == other.duration_ms &&
               periodic == other.periodic;
    }
};

struct AnonymousTrigger {
    bool is_completion{true};
    std::string description{"completion"};

    constexpr bool operator==(const AnonymousTrigger& other) const noexcept {
        return is_completion == other.is_completion;
    }
};

using TriggerVariant = std::variant<SignalTrigger, TimeTrigger, AnonymousTrigger>;

}  // namespace fsm::codegen

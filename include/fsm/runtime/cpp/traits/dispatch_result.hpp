#pragma once

#include <cstdint>
#include <string_view>

namespace fsm {

// Result of an event dispatch operation
enum class dispatch_status : std::uint8_t {
    success,         // Transition successfully executed
    deferred,        // Event was deferred by the active state
    guard_rejected,  // Transition matched event, but all candidate guard conditions evaluated to false
    unhandled        // No transition defined for (current_state, event)
};

inline constexpr std::string_view to_string(dispatch_status s) noexcept {
    switch (s) {
        case dispatch_status::success:
            return "success";
        case dispatch_status::deferred:
            return "deferred";
        case dispatch_status::guard_rejected:
            return "guard_rejected";
        case dispatch_status::unhandled:
            return "unhandled";
    }
    return "unknown";
}

struct dispatch_result {
    dispatch_status status = dispatch_status::unhandled;

    constexpr dispatch_result() noexcept = default;
    constexpr dispatch_result(dispatch_status s) noexcept : status(s) {}

    [[nodiscard]] constexpr bool is_success() const noexcept { return status == dispatch_status::success; }
    [[nodiscard]] constexpr bool is_deferred() const noexcept { return status == dispatch_status::deferred; }
    [[nodiscard]] constexpr bool is_guard_rejected() const noexcept {
        return status == dispatch_status::guard_rejected;
    }
    [[nodiscard]] constexpr bool is_unhandled() const noexcept { return status == dispatch_status::unhandled; }
    [[nodiscard]] constexpr bool is_ok() const noexcept { return is_success() || is_deferred(); }

    [[nodiscard]] constexpr explicit operator bool() const noexcept { return is_ok(); }
    constexpr bool operator==(const dispatch_result& other) const noexcept { return status == other.status; }
    constexpr bool operator==(dispatch_status other_status) const noexcept { return status == other_status; }
    constexpr bool operator!=(const dispatch_result& other) const noexcept { return status != other.status; }
    constexpr bool operator!=(dispatch_status other_status) const noexcept { return status != other_status; }

    [[nodiscard]] constexpr std::string_view to_string() const noexcept { return fsm::to_string(status); }
};

enum class transition_kind : std::uint8_t { external, internal };

inline constexpr std::string_view to_string(transition_kind k) noexcept {
    switch (k) {
        case transition_kind::external:
            return "external";
        case transition_kind::internal:
            return "internal";
    }
    return "external";
}

// Information about a transition or dispatch attempt passed to observers
struct transition_info {
    std::string_view source;
    std::string_view target;
    std::string_view event;
    dispatch_status status = dispatch_status::success;
    transition_kind kind = transition_kind::external;

    [[nodiscard]] constexpr bool is_internal() const noexcept { return kind == transition_kind::internal; }
    [[nodiscard]] constexpr bool is_external() const noexcept { return kind == transition_kind::external; }
    [[nodiscard]] constexpr bool is_success() const noexcept { return status == dispatch_status::success; }
    [[nodiscard]] constexpr bool is_deferred() const noexcept { return status == dispatch_status::deferred; }
    [[nodiscard]] constexpr bool is_guard_rejected() const noexcept {
        return status == dispatch_status::guard_rejected;
    }
    [[nodiscard]] constexpr bool is_unhandled() const noexcept { return status == dispatch_status::unhandled; }
};

}  // namespace fsm

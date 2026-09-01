#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "fsm/backend/cpp/runtime/traits/dispatch_result.hpp"

namespace fsm {

// Result of a periodic sampled step() operation in the synchronous control loop
enum class step_status : std::uint8_t {
    steady,       // Machine remains nominally in active state (no continuous guard satisfied)
    transitioned  // A continuous/sampled transition condition fired and state updated
};

inline constexpr std::string_view to_string(step_status s) noexcept {
    switch (s) {
        case step_status::steady:
            return "steady";
        case step_status::transitioned:
            return "transitioned";
    }
    return "steady";
}

struct step_result {
    step_status status = step_status::steady;
    std::optional<transition_trace> trace = std::nullopt;

    constexpr step_result() noexcept = default;
    constexpr step_result(step_status s) noexcept : status(s), trace(std::nullopt) {}
    constexpr step_result(step_status s, transition_trace t) noexcept : status(s), trace(t) {}
    constexpr step_result(step_status s, std::optional<transition_trace> t) noexcept : status(s), trace(t) {}

    [[nodiscard]] constexpr bool has_transitioned() const noexcept { return status == step_status::transitioned; }
    [[nodiscard]] constexpr bool is_transitioned() const noexcept { return status == step_status::transitioned; }
    [[nodiscard]] constexpr bool is_steady() const noexcept { return status == step_status::steady; }

    [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_transitioned(); }
    constexpr bool operator==(const step_result& other) const noexcept { return status == other.status; }
    constexpr bool operator==(step_status other_status) const noexcept { return status == other_status; }
    constexpr bool operator!=(const step_result& other) const noexcept { return status != other.status; }
    constexpr bool operator!=(step_status other_status) const noexcept { return status != other_status; }

    [[nodiscard]] constexpr std::string_view to_string() const noexcept { return fsm::to_string(status); }
};

}  // namespace fsm

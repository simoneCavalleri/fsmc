/**
 * @file clock_definition.hpp
 * @brief Continuous Timed Automata Clock Variables and Reset Operations in FSM IR.
 */

#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/ir/deterministic_id.hpp"
#include "fsm/ir/trigger.hpp"

namespace fsm::ir {

/**
 * @brief Continuous real-time clock variable definition (Alur-Dill Timed Automata semantics).
 *
 * In Timed Automata, clocks advance synchronously with continuous physical time (\dot{x} = 1)
 * during state residence and may be instantaneously reset to zero upon transition execution.
 */
struct ClockDefinition {
    std::string name;                             ///< Unique clock identifier (e.g. "stay_clk", "t_heartbeat")
    std::string id;                               ///< Deterministic 64-bit hex hash identifier
    TimeUnit resolution{TimeUnit::Microseconds};  ///< Physical time measurement unit
    std::string description;                      ///< Optional engineering documentation

    ClockDefinition() = default;

    explicit ClockDefinition(std::string clock_name, TimeUnit unit = TimeUnit::Microseconds, std::string desc = "")
        : name(std::move(clock_name)), resolution(unit), description(std::move(desc)) {
        if (!name.empty()) {
            id = compute_deterministic_id(name);
        }
    }

    ClockDefinition(std::string clock_id, std::string clock_name, TimeUnit unit, std::string desc = "")
        : name(std::move(clock_name)), id(std::move(clock_id)), resolution(unit), description(std::move(desc)) {}

    bool operator==(const ClockDefinition& other) const noexcept {
        return name == other.name && id == other.id && resolution == other.resolution &&
               description == other.description;
    }

    bool operator<(const ClockDefinition& other) const noexcept { return name < other.name; }
};

/**
 * @brief Instantaneous clock reset operation executed during transition firing (x := 0).
 */
struct ClockResetOp {
    std::string clock_name;  ///< Target clock to reset to zero

    ClockResetOp() = default;
    explicit ClockResetOp(std::string clk) : clock_name(std::move(clk)) {}

    bool operator==(const ClockResetOp& other) const noexcept { return clock_name == other.clock_name; }
};

}  // namespace fsm::ir

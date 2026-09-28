/**
 * @file variable_definition.hpp
 * @brief Extended Finite State Machine (EFSM) state variables, bounds, and physical units.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "fsm/ir/data_type.hpp"

namespace fsm::ir {

/**
 * @brief Extended Finite State Machine (EFSM) internal register / state variable definition.
 *
 * Holds internal datapath states, initial values, physical units, and optional interval bounds
 * used for SMT verification, range checking, and code generation.
 */
struct VariableDefinition {
    std::string name;                          ///< Variable identifier (e.g. "battery_soc")
    DataType type{PrimitiveTypeKind::UInt32};  ///< Canonical language-neutral data type
    std::string initial_value{"0"};            ///< Initial value expression (e.g. "0", "100.0f")
    std::optional<std::string> physical_unit;  ///< SysML v2 ISQ units (e.g. "[mm/s]", "[degC]", "[kW*h]")
    std::optional<int64_t> min_value;          ///< Optional lower bound contract
    std::optional<int64_t> max_value;          ///< Optional upper bound contract
    std::optional<std::size_t> register_index{std::nullopt};  ///< Allocated hardware register or memory slot index
    std::string description;                                  ///< Human-readable documentation comment

    VariableDefinition() = default;

    VariableDefinition(std::string var_name, DataType var_type, std::string init_val = "0",
                       std::optional<int64_t> min_val = std::nullopt, std::optional<int64_t> max_val = std::nullopt,
                       std::string desc = "", std::optional<std::string> unit = std::nullopt,
                       std::optional<std::size_t> reg_idx = std::nullopt)
        : name(std::move(var_name)),
          type(std::move(var_type)),
          initial_value(std::move(init_val)),
          physical_unit(std::move(unit)),
          min_value(min_val),
          max_value(max_val),
          register_index(reg_idx),
          description(std::move(desc)) {}

    bool operator==(const VariableDefinition& other) const noexcept {
        return name == other.name && type == other.type && initial_value == other.initial_value &&
               physical_unit == other.physical_unit && min_value == other.min_value && max_value == other.max_value &&
               register_index == other.register_index && description == other.description;
    }
};

}  // namespace fsm::ir

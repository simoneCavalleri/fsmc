/**
 * @file port_definition.hpp
 * @brief MBSE typed input/output ports and range contracts for the FSM Intermediate Representation.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "fsm/ir/data_type.hpp"

namespace fsm::ir {

/**
 * @brief Direction of an I/O interaction port on the state machine boundary.
 */
enum class PortDirection : std::uint8_t {
    In,    ///< Input port (incoming signal/data stream)
    Out,   ///< Output port (outgoing actuator/command stream)
    InOut  ///< Bidirectional port (shared bus or duplex channel)
};

/**
 * @brief Converts a port direction enum to its string label.
 * @param dir The PortDirection enum.
 * @return String view ("in", "out", "inout").
 */
[[nodiscard]] inline std::string_view port_direction_to_string(PortDirection dir) noexcept {
    switch (dir) {
        case PortDirection::In:
            return "in";
        case PortDirection::Out:
            return "out";
        case PortDirection::InOut:
            return "inout";
    }
    return "in";
}

/**
 * @brief Parses a string token into a PortDirection enum.
 * @param str The string view to parse ("in", "out", "inout").
 * @return Parsed PortDirection enum.
 */
[[nodiscard]] inline PortDirection string_to_port_direction(std::string_view str) noexcept {
    if (str == "out" || str == "Out" || str == "OUT") {
        return PortDirection::Out;
    }
    if (str == "inout" || str == "InOut" || str == "INOUT") {
        return PortDirection::InOut;
    }
    return PortDirection::In;
}

/**
 * @brief Architectural Port Definition representing formal I/O contracts on state machine boundaries.
 *
 * Models typed ports with optional range contracts [min_value, max_value], formal assert constraints,
 * default values, and physical SI/ISQ engineering units (e.g. "[m/s]", "[rad]").
 */
struct PortDefinition {
    std::string name;                            ///< Unique port identifier (e.g. "altitude_cmd")
    DataType type{PrimitiveTypeKind::Float32};   ///< Canonical language-neutral data type
    PortDirection direction{PortDirection::In};  ///< Port direction (In, Out, InOut)
    std::optional<double> min_value;             ///< Optional numeric lower bound contract
    std::optional<double> max_value;             ///< Optional numeric upper bound contract
    std::string constraint;                      ///< Formal assert constraint e.g. "self >= 0.0 and self <= 100.0"
    std::string default_value;                   ///< Default value expression
    std::optional<std::string> physical_unit;    ///< Physical engineering unit (e.g. "[ft]", "[m/s]")
    std::string description;                     ///< Human-readable documentation comment

    PortDefinition() = default;

    PortDefinition(std::string port_name, DataType port_type, PortDirection port_dir = PortDirection::In,
                   std::optional<double> min_val = std::nullopt, std::optional<double> max_val = std::nullopt,
                   std::string constr = "", std::string def_val = "", std::optional<std::string> unit = std::nullopt,
                   std::string desc = "")
        : name(std::move(port_name)),
          type(std::move(port_type)),
          direction(port_dir),
          min_value(min_val),
          max_value(max_val),
          constraint(std::move(constr)),
          default_value(std::move(def_val)),
          physical_unit(std::move(unit)),
          description(std::move(desc)) {}

    /**
     * @brief Checks whether the port accepts incoming data (In or InOut).
     */
    [[nodiscard]] bool is_in() const noexcept {
        return direction == PortDirection::In || direction == PortDirection::InOut;
    }

    /**
     * @brief Checks whether the port emits outgoing data (Out or InOut).
     */
    [[nodiscard]] bool is_out() const noexcept {
        return direction == PortDirection::Out || direction == PortDirection::InOut;
    }

    bool operator==(const PortDefinition& other) const noexcept {
        return name == other.name && type == other.type && direction == other.direction &&
               min_value == other.min_value && max_value == other.max_value && constraint == other.constraint &&
               default_value == other.default_value && physical_unit == other.physical_unit &&
               description == other.description;
    }
};

}  // namespace fsm::ir

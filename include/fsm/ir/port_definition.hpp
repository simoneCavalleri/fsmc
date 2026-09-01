#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "fsm/ir/variable_definition.hpp"

namespace fsm::codegen {

// ============================================================================
// Port Definitions & Domain Contracts (SysML v2 / MBSE Typed Ports)
// ============================================================================

enum class PortDirection : std::uint8_t { In, Out, InOut };

inline std::string_view port_direction_to_string(PortDirection dir) noexcept {
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

inline PortDirection string_to_port_direction(std::string_view str) noexcept {
    if (str == "out" || str == "Out" || str == "OUT") {
        return PortDirection::Out;
    }
    if (str == "inout" || str == "InOut" || str == "INOUT") {
        return PortDirection::InOut;
    }
    return PortDirection::In;
}

struct PortDefinition {
    std::string name;
    std::string type{"float"};
    VariableTypeKind type_kind{VariableTypeKind::Float};
    PortDirection direction{PortDirection::In};
    std::optional<double> min_value;
    std::optional<double> max_value;
    std::string constraint;  ///< Formal assert constraint e.g. "self >= 0.0 and self <= 100.0"
    std::string default_value;
    std::optional<std::string> physical_unit;
    std::string description;

    PortDefinition() = default;
    PortDefinition(std::string port_name, std::string port_type, PortDirection port_dir = PortDirection::In,
                   std::optional<double> min_val = std::nullopt, std::optional<double> max_val = std::nullopt,
                   std::string constr = "", std::string def_val = "",
                   std::optional<std::string> unit = std::nullopt, std::string desc = "")
        : name(std::move(port_name)),
          type(std::move(port_type)),
          type_kind(infer_type_kind(type)),
          direction(port_dir),
          min_value(min_val),
          max_value(max_val),
          constraint(std::move(constr)),
          default_value(std::move(def_val)),
          physical_unit(std::move(unit)),
          description(std::move(desc)) {}

    [[nodiscard]] bool is_in() const noexcept { return direction == PortDirection::In || direction == PortDirection::InOut; }
    [[nodiscard]] bool is_out() const noexcept { return direction == PortDirection::Out || direction == PortDirection::InOut; }

    bool operator==(const PortDefinition& other) const noexcept {
        return name == other.name && type == other.type && type_kind == other.type_kind &&
               direction == other.direction && min_value == other.min_value && max_value == other.max_value &&
               constraint == other.constraint && default_value == other.default_value &&
               physical_unit == other.physical_unit && description == other.description;
    }
};

}  // namespace fsm::codegen

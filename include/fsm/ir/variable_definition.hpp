#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace fsm::codegen {

// ============================================================================
// State Variables (Extended Finite State Machine - EFSM)
// ============================================================================

enum class VariableTypeKind : std::uint8_t { Boolean, Integer, UnsignedInteger, Float, Enum, CustomStruct };

inline std::string_view variable_type_kind_to_string(VariableTypeKind kind) noexcept {
    switch (kind) {
        case VariableTypeKind::Boolean:
            return "Boolean";
        case VariableTypeKind::Integer:
            return "Integer";
        case VariableTypeKind::UnsignedInteger:
            return "UnsignedInteger";
        case VariableTypeKind::Float:
            return "Float";
        case VariableTypeKind::Enum:
            return "Enum";
        case VariableTypeKind::CustomStruct:
            return "CustomStruct";
    }
    return "UnsignedInteger";
}

inline VariableTypeKind infer_type_kind(std::string_view type_name) noexcept {
    if (type_name == "bool" || type_name == "boolean" || type_name == "Boolean") {
        return VariableTypeKind::Boolean;
    }
    if (type_name == "int" || type_name == "int32_t" || type_name == "int64_t" || type_name == "int16_t" ||
        type_name == "int8_t" || type_name == "Integer" || type_name == "Int32") {
        return VariableTypeKind::Integer;
    }
    if (type_name == "uint32_t" || type_name == "uint64_t" || type_name == "uint16_t" || type_name == "uint8_t" ||
        type_name == "unsigned" || type_name == "Natural" || type_name == "Positive" || type_name == "size_t") {
        return VariableTypeKind::UnsignedInteger;
    }
    if (type_name == "float" || type_name == "double" || type_name == "Real" || type_name == "Float" ||
        type_name == "Double") {
        return VariableTypeKind::Float;
    }
    if (type_name.rfind("enum ", 0) == 0 || type_name.rfind("enum class ", 0) == 0) {
        return VariableTypeKind::Enum;
    }
    return VariableTypeKind::CustomStruct;
}

struct VariableDefinition {
    std::string name;
    std::string type{"uint32_t"};  // e.g. "uint32_t", "bool", "int32_t", "double"
    VariableTypeKind type_kind{VariableTypeKind::UnsignedInteger};
    std::string initial_value{"0"};
    std::optional<std::string> physical_unit;  ///< SysML v2 ISQ units, e.g. "[mm/s]", "[degC]", "[kW*h]"
    std::optional<int64_t> min_value;
    std::optional<int64_t> max_value;
    std::string description;

    VariableDefinition() = default;
    VariableDefinition(std::string var_name, std::string var_type, std::string init_val = "0",
                       std::optional<int64_t> min_val = std::nullopt, std::optional<int64_t> max_val = std::nullopt,
                       std::string desc = "", std::optional<std::string> unit = std::nullopt)
        : name(std::move(var_name)),
          type(std::move(var_type)),
          type_kind(infer_type_kind(type)),
          initial_value(std::move(init_val)),
          physical_unit(std::move(unit)),
          min_value(min_val),
          max_value(max_val),
          description(std::move(desc)) {}

    bool operator==(const VariableDefinition& other) const noexcept {
        return name == other.name && type == other.type && type_kind == other.type_kind &&
               initial_value == other.initial_value && physical_unit == other.physical_unit &&
               min_value == other.min_value && max_value == other.max_value && description == other.description;
    }
};

}  // namespace fsm::codegen

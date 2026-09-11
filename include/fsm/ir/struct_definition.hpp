/**
 * @file struct_definition.hpp
 * @brief Structured Data Definitions, Fields, and Contracts (MBSE / SysML v2 struct def) in FSM IR.
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/ir/data_type.hpp"

namespace fsm::ir {

/**
 * @brief Represents an individual field or attribute within a struct/datatype definition.
 */
struct StructField {
    std::string name;                           ///< Field identifier (e.g. "targetAltitude_ft")
    DataType type{PrimitiveTypeKind::Float32};  ///< Canonical language-neutral data type
    std::string default_value;                  ///< Default initializer expression (e.g. "0.0f", "false")
    std::optional<std::string> physical_unit;   ///< SysML v2 ISQ units (e.g. "[ft]", "[m/s]", "[degC]")
    std::optional<double> min_value;            ///< Optional numeric lower bound contract
    std::optional<double> max_value;            ///< Optional numeric upper bound contract
    std::string description;                    ///< Optional field documentation or annotation

    StructField() = default;

    StructField(std::string field_name, DataType field_type, std::string def_val = "",
                std::optional<std::string> unit = std::nullopt, std::optional<double> min_val = std::nullopt,
                std::optional<double> max_val = std::nullopt, std::string desc = "")
        : name(std::move(field_name)),
          type(std::move(field_type)),
          default_value(std::move(def_val)),
          physical_unit(std::move(unit)),
          min_value(min_val),
          max_value(max_val),
          description(std::move(desc)) {}

    bool operator==(const StructField& other) const noexcept {
        return name == other.name && type == other.type && default_value == other.default_value &&
               physical_unit == other.physical_unit && min_value == other.min_value && max_value == other.max_value &&
               description == other.description;
    }
};

/**
 * @brief Represents a user-defined structured data type or record in the FSM IR.
 *
 * Corresponds to SysML v2 `struct def` or `datatype def`. Emits standard C++ POD
 * structures with default initializers, zero heap allocations, and relational comparison operators.
 */
struct StructDefinition {
    std::string name;                 ///< Struct/Datatype name (e.g. "FlightPlanWaypoint")
    std::vector<StructField> fields;  ///< Ordered sequence of struct fields
    bool is_datatype{false};          ///< True if declared as value datatype def, false if struct def
    std::string description;          ///< Optional struct-level documentation

    StructDefinition() = default;
    explicit StructDefinition(std::string struct_name, bool is_data = false, std::string desc = "")
        : name(std::move(struct_name)), is_datatype(is_data), description(std::move(desc)) {}

    /**
     * @brief Checks whether a field with the specified name exists.
     */
    [[nodiscard]] bool has_field(std::string_view field_name) const noexcept {
        return find_field(field_name) != nullptr;
    }

    /**
     * @brief Finds a field by its name.
     * @param field_name Field identifier string.
     * @return Const pointer to StructField if found, nullptr otherwise.
     */
    [[nodiscard]] const StructField* find_field(std::string_view field_name) const noexcept {
        for (const auto& f : fields) {
            if (f.name == field_name) {
                return &f;
            }
        }
        return nullptr;
    }

    /**
     * @brief Finds a mutable field by its name.
     * @param field_name Field identifier string.
     * @return Mutable pointer to StructField if found, nullptr otherwise.
     */
    [[nodiscard]] StructField* find_field_mut(std::string_view field_name) noexcept {
        for (auto& f : fields) {
            if (f.name == field_name) {
                return &f;
            }
        }
        return nullptr;
    }

    /**
     * @brief Adds or replaces a field in the structure definition.
     * @param field Field definition to insert.
     */
    void add_field(StructField field) {
        for (auto& existing : fields) {
            if (existing.name == field.name) {
                existing = std::move(field);
                return;
            }
        }
        fields.push_back(std::move(field));
    }

    bool operator==(const StructDefinition& other) const noexcept {
        return name == other.name && fields == other.fields && is_datatype == other.is_datatype &&
               description == other.description;
    }
};

}  // namespace fsm::ir

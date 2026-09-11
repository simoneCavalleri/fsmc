/**
 * @file type_definition.hpp
 * @brief Compound User-Defined Types Metamodel (Enum, Struct, Alias) for the FSM IR.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/ir/enum_definition.hpp"
#include "fsm/ir/struct_definition.hpp"

namespace fsm::ir {

/**
 * @brief Categorization of compound user-defined data types in FSM IR.
 */
enum class TypeKind : std::uint8_t {
    Enum,    ///< Enumeration with symbolic literals and optional numeric mapping
    Struct,  ///< Record/Product type with named fields, contracts, and units
    Alias    ///< Type alias (typedef/using) mapping an identifier to another type
};

/**
 * @brief Converts a TypeKind enum to its canonical string representation.
 */
[[nodiscard]] constexpr std::string_view type_kind_to_string(TypeKind kind) noexcept {
    switch (kind) {
        case TypeKind::Enum:
            return "enum";
        case TypeKind::Struct:
            return "struct";
        case TypeKind::Alias:
            return "alias";
    }
    return "struct";
}

/**
 * @brief Parses a string token into a TypeKind enum.
 */
[[nodiscard]] constexpr TypeKind string_to_type_kind(std::string_view str) noexcept {
    if (str == "enum" || str == "Enum") {
        return TypeKind::Enum;
    }
    if (str == "alias" || str == "Alias") {
        return TypeKind::Alias;
    }
    return TypeKind::Struct;
}

/**
 * @brief Unified metamodel representing user-defined compound types (Enum, Struct, Alias).
 *
 * Serves as the single canonical source of truth for user-defined types in FSM IR:
 * - Enum: holds `underlying_type` and `literals`.
 * - Struct: holds `fields` and `is_datatype`.
 * - Alias: holds `underlying_type` (the target aliased type).
 */
struct TypeDefinition {
    std::string name;                   ///< Type identifier (e.g. "NavigationMode", "Waypoint")
    TypeKind kind{TypeKind::Struct};    ///< Kind of user type (Enum, Struct, Alias)
    std::string underlying_type;        ///< Integer type for Enum, or target type for Alias
    bool is_datatype{false};            ///< For Struct: true if declared as value datatype
    std::vector<StructField> fields;    ///< Struct fields (for Struct kind)
    std::vector<EnumLiteral> literals;  ///< Symbolic literals (for Enum kind)
    std::string description;            ///< Documentation or MBSE annotation

    TypeDefinition() = default;

    TypeDefinition(std::string type_name, TypeKind type_kind, std::string underlying = "", bool is_data = false,
                   std::string desc = "")
        : name(std::move(type_name)),
          kind(type_kind),
          underlying_type(std::move(underlying)),
          is_datatype(is_data),
          description(std::move(desc)) {}

    /* implicit */ TypeDefinition(EnumDefinition ed)
        : name(std::move(ed.name)),
          kind(TypeKind::Enum),
          underlying_type(std::move(ed.underlying_type)),
          literals(std::move(ed.literals)),
          description(std::move(ed.description)) {}

    /* implicit */ TypeDefinition(StructDefinition sd)
        : name(std::move(sd.name)),
          kind(TypeKind::Struct),
          underlying_type(""),
          is_datatype(sd.is_datatype),
          fields(std::move(sd.fields)),
          description(std::move(sd.description)) {}

    /**
     * @brief Converts to transitional EnumDefinition.
     */
    [[nodiscard]] EnumDefinition to_enum_definition() const {
        EnumDefinition ed(name, underlying_type.empty() ? "uint8_t" : underlying_type, description);
        ed.literals = literals;
        return ed;
    }

    /**
     * @brief Converts to transitional StructDefinition.
     */
    [[nodiscard]] StructDefinition to_struct_definition() const {
        StructDefinition sd(name, is_datatype, description);
        sd.fields = fields;
        return sd;
    }

    // --- Factory Methods ---

    static TypeDefinition make_enum(std::string name, std::string underlying_type = "uint8_t",
                                    std::vector<EnumLiteral> literals = {}, std::string desc = "") {
        TypeDefinition td(std::move(name), TypeKind::Enum, std::move(underlying_type), false, std::move(desc));
        td.literals = std::move(literals);
        return td;
    }

    static TypeDefinition make_struct(std::string name, std::vector<StructField> fields = {}, bool is_datatype = false,
                                      std::string desc = "") {
        TypeDefinition td(std::move(name), TypeKind::Struct, "", is_datatype, std::move(desc));
        td.fields = std::move(fields);
        return td;
    }

    static TypeDefinition make_alias(std::string name, std::string target_type, std::string desc = "") {
        return TypeDefinition(std::move(name), TypeKind::Alias, std::move(target_type), false, std::move(desc));
    }

    [[nodiscard]] constexpr bool is_enum() const noexcept { return kind == TypeKind::Enum; }

    [[nodiscard]] constexpr bool is_struct() const noexcept { return kind == TypeKind::Struct; }

    [[nodiscard]] constexpr bool is_alias() const noexcept { return kind == TypeKind::Alias; }

    // --- Field Lookups & Mutations (Struct) ---

    [[nodiscard]] bool has_field(std::string_view field_name) const noexcept {
        return find_field(field_name) != nullptr;
    }

    [[nodiscard]] const StructField* find_field(std::string_view field_name) const noexcept {
        for (const auto& f : fields) {
            if (f.name == field_name) {
                return &f;
            }
        }
        return nullptr;
    }

    [[nodiscard]] StructField* find_field_mut(std::string_view field_name) noexcept {
        for (auto& f : fields) {
            if (f.name == field_name) {
                return &f;
            }
        }
        return nullptr;
    }

    void add_field(StructField field) {
        for (auto& existing : fields) {
            if (existing.name == field.name) {
                existing = std::move(field);
                return;
            }
        }
        fields.push_back(std::move(field));
    }

    // --- Literal Lookups & Mutations (Enum) ---

    [[nodiscard]] bool has_literal(std::string_view lit_name) const noexcept {
        return find_literal(lit_name) != nullptr;
    }

    [[nodiscard]] const EnumLiteral* find_literal(std::string_view lit_name) const noexcept {
        for (const auto& lit : literals) {
            if (lit.name == lit_name) {
                return &lit;
            }
        }
        return nullptr;
    }

    [[nodiscard]] EnumLiteral* find_literal_mut(std::string_view lit_name) noexcept {
        for (auto& lit : literals) {
            if (lit.name == lit_name) {
                return &lit;
            }
        }
        return nullptr;
    }

    void add_literal(EnumLiteral lit) {
        for (auto& existing : literals) {
            if (existing.name == lit.name) {
                existing = std::move(lit);
                return;
            }
        }
        literals.push_back(std::move(lit));
    }

    void add_literal(std::string lit_name, std::optional<int64_t> val = std::nullopt, std::string desc = "") {
        add_literal(EnumLiteral(std::move(lit_name), val, std::move(desc)));
    }

    bool operator==(const TypeDefinition& other) const noexcept {
        return name == other.name && kind == other.kind && underlying_type == other.underlying_type &&
               is_datatype == other.is_datatype && fields == other.fields && literals == other.literals &&
               description == other.description;
    }
};

}  // namespace fsm::ir

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsm::ir {

// ============================================================================
// Enum Definitions & Literals (MBSE / SysML v2 / C++ Codegen)
// ============================================================================

/**
 * @brief Represents an individual literal within an enumeration definition.
 */
struct EnumLiteral {
    std::string name;              ///< Literal identifier (e.g. "Standby", "Active")
    std::optional<int64_t> value;  ///< Explicit numeric value if assigned
    std::string description;       ///< Optional documentation or annotation

    EnumLiteral() = default;
    explicit EnumLiteral(std::string lit_name, std::optional<int64_t> val = std::nullopt, std::string desc = "")
        : name(std::move(lit_name)), value(val), description(std::move(desc)) {}

    bool operator==(const EnumLiteral& other) const noexcept {
        return name == other.name && value == other.value && description == other.description;
    }
};

/**
 * @brief Represents a user-defined enumeration type definition in the FSM IR.
 *
 * Mapped to C++ `enum class <Name> : <UnderlyingType>` during code emission,
 * enabling strong typing and reflection serializers (`to_string()`).
 */
struct EnumDefinition {
    std::string name;  ///< Enumeration type name (e.g. "FmsOperatingMode")
    std::string underlying_type{
        "uint8_t"};                     ///< Underlying integer type (default uint8_t for embedded memory efficiency)
    std::vector<EnumLiteral> literals;  ///< Enumeration literals
    std::string description;            ///< Optional type-level documentation

    EnumDefinition() = default;
    explicit EnumDefinition(std::string enum_name, std::string underlying = "uint8_t", std::string desc = "")
        : name(std::move(enum_name)), underlying_type(std::move(underlying)), description(std::move(desc)) {}

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


    bool operator==(const EnumDefinition& other) const noexcept {
        return name == other.name && underlying_type == other.underlying_type && literals == other.literals &&
               description == other.description;
    }
};

}  // namespace fsm::ir

namespace fsm {
using EnumLiteral = ir::EnumLiteral;
using EnumDefinition = ir::EnumDefinition;
}


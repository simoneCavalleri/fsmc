/**
 * @file data_type.hpp
 * @brief Canonical Language-Neutral Data Type Metamodel and Target Lowerings for FSM IR.
 */

#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace fsm::ir {

/**
 * @brief Categorization of canonical primitive types in the language-agnostic FSM IR.
 */
enum class PrimitiveTypeKind : std::uint8_t {
    Boolean,  ///< Boolean logic (true/false)
    Int8,     ///< 8-bit signed integer
    Int16,    ///< 16-bit signed integer
    Int32,    ///< 32-bit signed integer
    Int64,    ///< 64-bit signed integer
    UInt8,    ///< 8-bit unsigned integer / byte
    UInt16,   ///< 16-bit unsigned integer
    UInt32,   ///< 32-bit unsigned integer
    UInt64,   ///< 64-bit unsigned integer
    Float32,  ///< Single-precision IEEE 754 float
    Float64,  ///< Double-precision IEEE 754 float
    String,   ///< UTF-8 character string
    Void      ///< Empty / void type
};

/**
 * @brief Higher-level classification of types in FSM IR.
 */
enum class TypeClassification : std::uint8_t {
    Primitive,  ///< Canonical scalar primitive (bool, int, float, string)
    Enum,       ///< User-defined enumeration
    Struct,     ///< User-defined compound structure
    Alias,      ///< User-defined type alias
    Custom      ///< Target-specific or opaque external type
};

/**
 * @brief Canonical, Language-Neutral Data Type Metamodel for FSM Intermediate Representation.
 *
 * Fully decouples the IR from backend languages (C++, Rust, Python, SMV, VHDL, PLC ST).
 * Frontends lower source constructs into canonical DataType, while each backend lowers
 * DataType into its specific syntax.
 */
class DataType {
  public:
    DataType() : classification_(TypeClassification::Primitive), primitive_(PrimitiveTypeKind::UInt32) {}

    /* implicit */ DataType(PrimitiveTypeKind prim)
        : classification_(TypeClassification::Primitive), primitive_(prim) {}

    /* implicit */ DataType(const char* raw_type) : DataType(from_string(raw_type)) {}

    /* implicit */ DataType(std::string_view raw_type) : DataType(from_string(raw_type)) {}

    /* implicit */ DataType(const std::string& raw_type) : DataType(from_string(raw_type)) {}

    DataType(TypeClassification classif, std::string custom_name)
        : classification_(classif), primitive_(PrimitiveTypeKind::Void), custom_name_(std::move(custom_name)) {}

    DataType& operator=(std::string_view raw_type) {
        *this = from_string(raw_type);
        return *this;
    }

    DataType& operator=(const std::string& raw_type) {
        *this = from_string(raw_type);
        return *this;
    }

    DataType& operator=(const char* raw_type) {
        *this = from_string(raw_type);
        return *this;
    }

    // --- Static Factories ---

    [[nodiscard]] static DataType boolean() noexcept { return DataType(PrimitiveTypeKind::Boolean); }
    [[nodiscard]] static DataType int8() noexcept { return DataType(PrimitiveTypeKind::Int8); }
    [[nodiscard]] static DataType int16() noexcept { return DataType(PrimitiveTypeKind::Int16); }
    [[nodiscard]] static DataType int32() noexcept { return DataType(PrimitiveTypeKind::Int32); }
    [[nodiscard]] static DataType int64() noexcept { return DataType(PrimitiveTypeKind::Int64); }
    [[nodiscard]] static DataType uint8() noexcept { return DataType(PrimitiveTypeKind::UInt8); }
    [[nodiscard]] static DataType uint16() noexcept { return DataType(PrimitiveTypeKind::UInt16); }
    [[nodiscard]] static DataType uint32() noexcept { return DataType(PrimitiveTypeKind::UInt32); }
    [[nodiscard]] static DataType uint64() noexcept { return DataType(PrimitiveTypeKind::UInt64); }
    [[nodiscard]] static DataType float32() noexcept { return DataType(PrimitiveTypeKind::Float32); }
    [[nodiscard]] static DataType float64() noexcept { return DataType(PrimitiveTypeKind::Float64); }
    [[nodiscard]] static DataType string() noexcept { return DataType(PrimitiveTypeKind::String); }
    [[nodiscard]] static DataType custom(std::string name) {
        return DataType(TypeClassification::Custom, std::move(name));
    }
    [[nodiscard]] static DataType enumeration(std::string name) {
        return DataType(TypeClassification::Enum, std::move(name));
    }
    [[nodiscard]] static DataType structure(std::string name) {
        return DataType(TypeClassification::Struct, std::move(name));
    }

    /**
     * @brief Parse a raw type string from any supported frontend or legacy C++ syntax into a canonical DataType.
     * @param raw_type Source type string (e.g. "uint32_t", "Boolean", "Real", "int").
     * @return Canonical DataType representation.
     */
    [[nodiscard]] static DataType from_string(std::string_view raw_type);

    // --- Inspection Queries ---

    [[nodiscard]] TypeClassification classification() const noexcept { return classification_; }
    [[nodiscard]] PrimitiveTypeKind primitive() const noexcept { return primitive_; }
    [[nodiscard]] const std::string& custom_name() const noexcept { return custom_name_; }

    [[nodiscard]] bool is_primitive() const noexcept { return classification_ == TypeClassification::Primitive; }
    [[nodiscard]] bool is_enum() const noexcept { return classification_ == TypeClassification::Enum; }
    [[nodiscard]] bool is_struct() const noexcept { return classification_ == TypeClassification::Struct; }
    [[nodiscard]] bool is_custom() const noexcept { return classification_ != TypeClassification::Primitive; }

    [[nodiscard]] bool is_boolean() const noexcept {
        return is_primitive() && primitive_ == PrimitiveTypeKind::Boolean;
    }

    [[nodiscard]] bool is_integer() const noexcept {
        if (!is_primitive())
            return false;
        switch (primitive_) {
            case PrimitiveTypeKind::Int8:
            case PrimitiveTypeKind::Int16:
            case PrimitiveTypeKind::Int32:
            case PrimitiveTypeKind::Int64:
            case PrimitiveTypeKind::UInt8:
            case PrimitiveTypeKind::UInt16:
            case PrimitiveTypeKind::UInt32:
            case PrimitiveTypeKind::UInt64:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] bool is_signed_integer() const noexcept {
        if (!is_primitive())
            return false;
        switch (primitive_) {
            case PrimitiveTypeKind::Int8:
            case PrimitiveTypeKind::Int16:
            case PrimitiveTypeKind::Int32:
            case PrimitiveTypeKind::Int64:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] bool is_unsigned_integer() const noexcept {
        if (!is_primitive())
            return false;
        switch (primitive_) {
            case PrimitiveTypeKind::UInt8:
            case PrimitiveTypeKind::UInt16:
            case PrimitiveTypeKind::UInt32:
            case PrimitiveTypeKind::UInt64:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] bool is_floating_point() const noexcept {
        return is_primitive() && (primitive_ == PrimitiveTypeKind::Float32 || primitive_ == PrimitiveTypeKind::Float64);
    }

    [[nodiscard]] bool is_string() const noexcept { return is_primitive() && primitive_ == PrimitiveTypeKind::String; }

    /**
     * @brief Returns the bit-width of primitive data types (e.g. 8, 16, 32, 64).
     */
    [[nodiscard]] uint32_t bit_width() const noexcept {
        if (!is_primitive())
            return 32;
        switch (primitive_) {
            case PrimitiveTypeKind::Boolean:
            case PrimitiveTypeKind::Int8:
            case PrimitiveTypeKind::UInt8:
                return 8;
            case PrimitiveTypeKind::Int16:
            case PrimitiveTypeKind::UInt16:
                return 16;
            case PrimitiveTypeKind::Int32:
            case PrimitiveTypeKind::UInt32:
            case PrimitiveTypeKind::Float32:
                return 32;
            case PrimitiveTypeKind::Int64:
            case PrimitiveTypeKind::UInt64:
            case PrimitiveTypeKind::Float64:
                return 64;
            default:
                return 0;
        }
    }

    // --- Target Language Lowering ---

    /**
     * @brief Canonical language-neutral string representation for serialization & logging.
     */
    [[nodiscard]] std::string to_canonical_string() const;

    /**
     * @brief Lowering for C++ Backend (ISO C++17 / C++20).
     */
    [[nodiscard]] std::string to_cpp_type() const;

    /**
     * @brief Lowering for OMG SysML v2 / KerML Model Emitters.
     */
    [[nodiscard]] std::string to_sysml_type() const;

    /**
     * @brief Lowering for Rust Backend (Safe Systems Programming).
     */
    [[nodiscard]] std::string to_rust_type() const;

    /**
     * @brief Lowering for nuXmv / SMV Symbolic Model Verification.
     * @param min_val Optional lower range limit for integer subranges.
     * @param max_val Optional upper range limit for integer subranges.
     */
    [[nodiscard]] std::string to_smv_type(std::optional<int64_t> min_val = std::nullopt,
                                          std::optional<int64_t> max_val = std::nullopt) const;

    // --- Comparison Operators ---

    bool operator==(const DataType& other) const noexcept {
        if (classification_ != other.classification_)
            return false;
        if (classification_ == TypeClassification::Primitive) {
            return primitive_ == other.primitive_;
        }
        return custom_name_ == other.custom_name_;
    }

    bool operator==(const char* str) const noexcept { return *this == std::string_view(str); }

    bool operator==(std::string_view str) const noexcept;
    bool operator<(const DataType& other) const noexcept;

  private:
    TypeClassification classification_{TypeClassification::Primitive};
    PrimitiveTypeKind primitive_{PrimitiveTypeKind::UInt32};
    std::string custom_name_;

    static std::string trim(std::string_view sv);
};

/**
 * @brief Converts a TypeClassification enum into its string representation.
 */
[[nodiscard]] constexpr std::string_view type_classification_to_string(TypeClassification c) noexcept {
    switch (c) {
        case TypeClassification::Primitive:
            return "primitive";
        case TypeClassification::Enum:
            return "enum";
        case TypeClassification::Struct:
            return "struct";
        case TypeClassification::Alias:
            return "alias";
        case TypeClassification::Custom:
            return "custom";
    }
    return "primitive";
}

std::ostream& operator<<(std::ostream& os, const DataType& dt);
void PrintTo(const DataType& dt, std::ostream* os);

}  // namespace fsm::ir

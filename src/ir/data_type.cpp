#include "fsm/ir/data_type.hpp"

#include <iostream>

namespace fsm::ir {

std::string DataType::trim(std::string_view sv) {
    size_t s = 0;
    while (s < sv.size() && (sv[s] == ' ' || sv[s] == '\t' || sv[s] == '\r' || sv[s] == '\n'))
        s++;
    if (s == sv.size())
        return "";
    size_t e = sv.size() - 1;
    while (e > s && (sv[e] == ' ' || sv[e] == '\t' || sv[e] == '\r' || sv[e] == '\n'))
        e--;
    return std::string(sv.substr(s, e - s + 1));
}

DataType DataType::from_string(std::string_view raw_type) {
    std::string t = trim(raw_type);
    auto colon_pos = t.rfind("::");
    if (colon_pos != std::string::npos) {
        t = t.substr(colon_pos + 2);
    }

    // Boolean
    if (t == "bool" || t == "boolean" || t == "Boolean") {
        return DataType::boolean();
    }

    // Signed integers
    if (t == "int8" || t == "int8_t" || t == "Int8")
        return DataType::int8();
    if (t == "int16" || t == "int16_t" || t == "Int16")
        return DataType::int16();
    if (t == "int" || t == "int32" || t == "int32_t" || t == "Int32" || t == "int_t")
        return DataType::int32();
    if (t == "int64" || t == "int64_t" || t == "Int64")
        return DataType::int64();

    // Unsigned integers
    if (t == "uint8" || t == "uint8_t" || t == "UInt8" || t == "byte")
        return DataType::uint8();
    if (t == "uint16" || t == "uint16_t" || t == "UInt16")
        return DataType::uint16();
    if (t == "uint32" || t == "uint32_t" || t == "UInt32" || t == "Integer" || t == "Natural" || t == "Positive" ||
        t == "unsigned" || t == "size_t")
        return DataType::uint32();
    if (t == "uint64" || t == "uint64_t" || t == "UInt64")
        return DataType::uint64();

    // Floating point
    if (t == "float" || t == "float32" || t == "Float" || t == "Real" || t == "single")
        return DataType::float32();
    if (t == "double" || t == "float64" || t == "Double")
        return DataType::float64();

    // Strings
    if (t == "string" || t == "String" || t == "std::string")
        return DataType::string();

    // Void
    if (t == "void" || t == "Void")
        return DataType(PrimitiveTypeKind::Void);

    // Custom, Enum, or Struct
    if (t.rfind("enum ", 0) == 0) {
        return DataType::enumeration(t.substr(5));
    }
    if (t.rfind("struct ", 0) == 0) {
        return DataType::structure(t.substr(7));
    }

    return DataType::custom(t);
}

std::string DataType::to_canonical_string() const {
    if (!is_primitive()) {
        return custom_name_;
    }
    switch (primitive_) {
        case PrimitiveTypeKind::Boolean:
            return "bool";
        case PrimitiveTypeKind::Int8:
            return "int8";
        case PrimitiveTypeKind::Int16:
            return "int16";
        case PrimitiveTypeKind::Int32:
            return "int32";
        case PrimitiveTypeKind::Int64:
            return "int64";
        case PrimitiveTypeKind::UInt8:
            return "uint8";
        case PrimitiveTypeKind::UInt16:
            return "uint16";
        case PrimitiveTypeKind::UInt32:
            return "uint32";
        case PrimitiveTypeKind::UInt64:
            return "uint64";
        case PrimitiveTypeKind::Float32:
            return "float32";
        case PrimitiveTypeKind::Float64:
            return "float64";
        case PrimitiveTypeKind::String:
            return "string";
        case PrimitiveTypeKind::Void:
            return "void";
    }
    return "uint32";
}

std::string DataType::to_cpp_type() const {
    if (!is_primitive()) {
        return custom_name_;
    }
    switch (primitive_) {
        case PrimitiveTypeKind::Boolean:
            return "bool";
        case PrimitiveTypeKind::Int8:
            return "int8_t";
        case PrimitiveTypeKind::Int16:
            return "int16_t";
        case PrimitiveTypeKind::Int32:
            return "int32_t";
        case PrimitiveTypeKind::Int64:
            return "int64_t";
        case PrimitiveTypeKind::UInt8:
            return "uint8_t";
        case PrimitiveTypeKind::UInt16:
            return "uint16_t";
        case PrimitiveTypeKind::UInt32:
            return "uint32_t";
        case PrimitiveTypeKind::UInt64:
            return "uint64_t";
        case PrimitiveTypeKind::Float32:
            return "float";
        case PrimitiveTypeKind::Float64:
            return "double";
        case PrimitiveTypeKind::String:
            return "std::string";
        case PrimitiveTypeKind::Void:
            return "void";
    }
    return "uint32_t";
}

std::string DataType::to_sysml_type() const {
    if (!is_primitive()) {
        return custom_name_;
    }
    switch (primitive_) {
        case PrimitiveTypeKind::Boolean:
            return "Boolean";
        case PrimitiveTypeKind::Int8:
        case PrimitiveTypeKind::Int16:
        case PrimitiveTypeKind::Int32:
        case PrimitiveTypeKind::Int64:
        case PrimitiveTypeKind::UInt8:
        case PrimitiveTypeKind::UInt16:
        case PrimitiveTypeKind::UInt32:
        case PrimitiveTypeKind::UInt64:
            return "Integer";
        case PrimitiveTypeKind::Float32:
        case PrimitiveTypeKind::Float64:
            return "Real";
        case PrimitiveTypeKind::String:
            return "String";
        case PrimitiveTypeKind::Void:
            return "";
    }
    return "Integer";
}

std::string DataType::to_rust_type() const {
    if (!is_primitive()) {
        return custom_name_;
    }
    switch (primitive_) {
        case PrimitiveTypeKind::Boolean:
            return "bool";
        case PrimitiveTypeKind::Int8:
            return "i8";
        case PrimitiveTypeKind::Int16:
            return "i16";
        case PrimitiveTypeKind::Int32:
            return "i32";
        case PrimitiveTypeKind::Int64:
            return "i64";
        case PrimitiveTypeKind::UInt8:
            return "u8";
        case PrimitiveTypeKind::UInt16:
            return "u16";
        case PrimitiveTypeKind::UInt32:
            return "u32";
        case PrimitiveTypeKind::UInt64:
            return "u64";
        case PrimitiveTypeKind::Float32:
            return "f32";
        case PrimitiveTypeKind::Float64:
            return "f64";
        case PrimitiveTypeKind::String:
            return "String";
        case PrimitiveTypeKind::Void:
            return "()";
    }
    return "u32";
}

std::string DataType::to_smv_type(std::optional<int64_t> min_val, std::optional<int64_t> max_val) const {
    if (is_boolean()) {
        return "boolean";
    }
    if (min_val.has_value() && max_val.has_value()) {
        return std::to_string(*min_val) + ".." + std::to_string(*max_val);
    }
    if (is_integer()) {
        return "0..100";
    }
    return "0..100";
}

bool DataType::operator==(std::string_view str) const noexcept {
    return to_canonical_string() == str || to_cpp_type() == str || to_sysml_type() == str || to_rust_type() == str ||
           custom_name_ == str;
}

bool DataType::operator<(const DataType& other) const noexcept {
    if (classification_ != other.classification_) {
        return classification_ < other.classification_;
    }
    if (classification_ == TypeClassification::Primitive) {
        return primitive_ < other.primitive_;
    }
    return custom_name_ < other.custom_name_;
}

std::ostream& operator<<(std::ostream& os, const DataType& dt) {
    return os << dt.to_canonical_string();
}

void PrintTo(const DataType& dt, std::ostream* os) {
    if (os) {
        *os << dt.to_canonical_string();
    }
}

}  // namespace fsm::ir

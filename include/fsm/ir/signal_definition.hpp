/**
 * @file signal_definition.hpp
 * @brief MBSE Typed Signal Definitions, Payloads, Attributes, and Validators for the FSM IR.
 */

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "fsm/ir/data_type.hpp"

namespace fsm::ir {

/**
 * @brief Attribute payload parameter within a typed signal definition.
 */
struct SignalAttribute {
    std::string name;                          ///< Attribute name identifier
    DataType type{PrimitiveTypeKind::UInt32};  ///< Canonical language-neutral data type
    std::string default_value;                 ///< Default value expression

    SignalAttribute() = default;

    SignalAttribute(std::string attr_name, DataType attr_type, std::string def_val = "")
        : name(std::move(attr_name)), type(std::move(attr_type)), default_value(std::move(def_val)) {}

    bool operator==(const SignalAttribute& other) const noexcept {
        return name == other.name && type == other.type && default_value == other.default_value;
    }
};

/**
 * @brief MBSE Signal Definition representing asynchronous events with typed attributes and validation predicates.
 */
struct SignalDefinition {
    std::string name;                         ///< Unique signal name (e.g. "PacketReceived")
    std::vector<SignalAttribute> attributes;  ///< Sequence of payload attributes
    std::vector<std::string> validators;      ///< Predicate assertions (e.g. "len > 0", "ptr != nullptr")
    std::string description;                  ///< Human-readable documentation comment

    SignalDefinition() = default;
    explicit SignalDefinition(std::string sig_name) : name(std::move(sig_name)) {}

    bool operator==(const SignalDefinition& other) const noexcept {
        return name == other.name && attributes == other.attributes && validators == other.validators &&
               description == other.description;
    }
};

}  // namespace fsm::ir

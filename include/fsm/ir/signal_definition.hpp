#pragma once

#include <string>
#include <utility>
#include <vector>

namespace fsm::codegen {

// ============================================================================
// Signal / Payload Definitions & Attributes
// ============================================================================

struct SignalAttribute {
    std::string name;
    std::string type;  // e.g. "uint32_t", "const uint8_t*", "std::string"
    std::string default_value;

    SignalAttribute() = default;
    SignalAttribute(std::string attr_name, std::string attr_type, std::string def_val = "")
        : name(std::move(attr_name)), type(std::move(attr_type)), default_value(std::move(def_val)) {}

    bool operator==(const SignalAttribute& other) const noexcept {
        return name == other.name && type == other.type && default_value == other.default_value;
    }
};

struct SignalDefinition {
    std::string name;
    std::vector<SignalAttribute> attributes;
    std::vector<std::string> validators;  // Predicates e.g. "len > 0", "ptr != nullptr"
    std::string description;

    SignalDefinition() = default;
    explicit SignalDefinition(std::string sig_name) : name(std::move(sig_name)) {}

    bool operator==(const SignalDefinition& other) const noexcept {
        return name == other.name && attributes == other.attributes && validators == other.validators &&
               description == other.description;
    }
};

}  // namespace fsm::codegen

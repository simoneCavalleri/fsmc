/**
 * @file companion_manifest.hpp
 * @brief Companion Manifest data structures (.fsm.yaml, .fsm.json) shared across frontends and backends.
 */

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace fsm::ir {

struct CompanionPort {
    std::string name;
    std::string type = "uint32_t";
    std::string direction = "in";
    std::optional<double> min_value;
    std::optional<double> max_value;
    std::string constraint;
};

struct CompanionVariable {
    std::string name;
    std::string type = "uint32_t";
    std::string initial_value;
    std::string unit;
    std::optional<double> min_value;
    std::optional<double> max_value;
};

struct CompanionSignalAttr {
    std::string name;
    std::string type = "uint32_t";
    std::string default_value;
};

struct CompanionSignal {
    std::string name;
    std::vector<CompanionSignalAttr> attributes;
};

struct CompanionAction {
    std::string name;
    std::string signature;
    std::string inv;
};

struct CompanionProperty {
    std::string name;
    std::string kind;
    std::string formula;
    std::string req;
    std::string desc;
};

struct CompanionTypeField {
    std::string name;
    std::string type = "uint32_t";
};

struct CompanionCustomType {
    std::string name;
    std::string kind = "struct";
    std::string external_module;
    std::string external_symbol;
    std::vector<CompanionTypeField> fields;
    std::vector<std::string> enum_literals;
};

struct CompanionServiceParam {
    std::string name;
    std::string type = "int32_t";
};

struct CompanionService {
    std::string name;
    std::string return_type = "void";
    bool is_pure = false;
    bool is_noexcept = true;
    std::string desc;
    std::vector<CompanionServiceParam> params;
};

struct CompanionManifest {
    std::string package_name;
    std::string fsm_name;
    std::string initial_state;
    std::vector<CompanionCustomType> types;
    std::vector<CompanionPort> ports;
    std::vector<CompanionVariable> variables;
    std::vector<CompanionSignal> signals;
    std::vector<CompanionService> services;
    std::unordered_map<std::string, std::string> invariants;
    std::vector<CompanionProperty> properties;
    std::vector<CompanionAction> actions;
    std::vector<std::string> requirements;
};

}  // namespace fsm::ir

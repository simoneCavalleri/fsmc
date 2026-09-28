/**
 * @file companion_manifest_parser.hpp
 * @brief Companion Manifest Parser (.fsm.yaml, .fsm.json) enriching diagrams with formal contracts.
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fsm::frontend {

/**
 * @brief Formal port contract definition declared in a companion manifest.
 */
struct CompanionPort {
    std::string name;                 ///< Port identifier
    std::string type = "uint32_t";    ///< Data type name
    std::string direction = "in";     ///< Port direction ("in", "out", "inout")
    std::optional<double> min_value;  ///< Lower bound contract
    std::optional<double> max_value;  ///< Upper bound contract
    std::string constraint;           ///< Predicate assert constraint
};

/**
 * @brief Extended state variable declared in a companion manifest.
 */
struct CompanionVariable {
    std::string name;                 ///< Variable identifier
    std::string type = "uint32_t";    ///< Data type name
    std::string initial_value;        ///< Initial value expression
    std::string unit;                 ///< Physical unit
    std::optional<double> min_value;  ///< Lower bound contract
    std::optional<double> max_value;  ///< Upper bound contract
};

/**
 * @brief Signal payload attribute in a companion manifest.
 */
struct CompanionSignalAttr {
    std::string name;               ///< Attribute identifier
    std::string type = "uint32_t";  ///< Data type name
    std::string default_value;      ///< Default value expression
};

/**
 * @brief Typed signal definition in a companion manifest.
 */
struct CompanionSignal {
    std::string name;                             ///< Signal identifier
    std::vector<CompanionSignalAttr> attributes;  ///< Payload attributes
};

/**
 * @brief Action specification in a companion manifest.
 */
struct CompanionAction {
    std::string name;       ///< Action name
    std::string signature;  ///< C++ or method signature
    std::string inv;        ///< Invocation statement
};

/**
 * @brief Formal verification property in a companion manifest.
 */
struct CompanionProperty {
    std::string name;     ///< Property identifier
    std::string formula;  ///< Temporal logic formula (LTL/CTL)
};

/**
 * @brief Unified companion manifest specification enriching visual diagrams.
 */
struct CompanionManifest {
    std::string package_name;                                 ///< Package / module namespace
    std::string fsm_name;                                     ///< State machine class name
    std::string initial_state;                                ///< Default initial state
    std::vector<CompanionPort> ports;                         ///< I/O interface ports
    std::vector<CompanionVariable> variables;                 ///< Internal datapath variables
    std::vector<CompanionSignal> signals;                     ///< Typed signal models
    std::unordered_map<std::string, std::string> invariants;  ///< State permanence invariants
    std::vector<CompanionProperty> properties;                ///< LTL/CTL temporal verification properties
    std::vector<CompanionAction> actions;                     ///< Action signatures
    std::vector<std::string> requirements;                    ///< Requirement traceability IDs
};

/**
 * @brief Zero-dependency YAML and JSON Companion Manifest Parser (.fsm.yaml, .fsm.json).
 *
 * Enriches visual diagram topologies (PlantUML, Mermaid, DOT) with formal contracts:
 * typed ports, domain variables, signal payloads, temporal invariants, and safety properties.
 */
class CompanionManifestParser {
  public:
    /**
     * @brief Parses companion manifest text in YAML or JSON format.
     * @param content Manifest file text.
     * @param[out] manifest Parsed companion manifest structure.
     * @param[out] error_message Error message if parsing fails.
     * @return True if parsing succeeded, false otherwise.
     */
    static bool parse(std::string_view content, CompanionManifest& manifest, std::string& error_message);
};

}  // namespace fsm::frontend

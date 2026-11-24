/**
 * @file companion_manifest_parser.hpp
 * @brief Companion Manifest Parser (.fsm.yaml, .fsm.json) enriching diagrams with formal contracts.
 */

#pragma once

#include <string>
#include <string_view>

#include "fsm/ir/companion_manifest.hpp"

namespace fsm::frontend {

using ir::CompanionAction;
using ir::CompanionCustomType;
using ir::CompanionManifest;
using ir::CompanionPort;
using ir::CompanionProperty;
using ir::CompanionService;
using ir::CompanionServiceParam;
using ir::CompanionSignal;
using ir::CompanionSignalAttr;
using ir::CompanionTypeField;
using ir::CompanionVariable;

/**
 * @brief Zero-dependency YAML and JSON Companion Manifest Parser (.fsm.yaml, .fsm.json).
 *
 * Enriches visual diagram topologies (PlantUML, Mermaid, DOT) with formal contracts:
 * typed ports, domain variables, signal payloads, temporal invariants, and safety properties.
 */
class CompanionManifestParser {
  public:
    static bool parse(std::string_view content, CompanionManifest& manifest, std::string& error_message);
};

}  // namespace fsm::frontend

/**
 * @file diagram_contract_combiner.hpp
 * @brief Utility for combining visual diagram topology with a companion contract manifest.
 */

#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "fsm/frontend/common/companion_manifest_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::frontend::diagram {

/**
 * @class DiagramContractCombiner
 * @brief Combines visual diagram topology (PlantUML, Mermaid, DOT) with a Companion Manifest.
 *
 * Promotes an untyped visual diagram into a formal EFSM with:
 * - Typed ports and contracts
 * - Variables with initial values and physical units
 * - Signal payload definitions
 * - State stay duration invariants
 * - LTL/CTL temporal verification properties
 * - Requirement traceability IDs
 */
class DiagramContractCombiner {
  public:
    /**
     * @brief Merges companion manifest specifications into an existing visual diagram model.
     * @param model Target FsmIr model to enrich.
     * @param manifest Parsed companion manifest containing contracts and types.
     * @param error_msg Output error description if binding fails.
     * @return True if integration succeeded, false otherwise.
     */
    static bool combine(ir::FsmIr& model, const CompanionManifest& manifest, std::string& error_msg);
};

}  // namespace fsm::frontend::diagram

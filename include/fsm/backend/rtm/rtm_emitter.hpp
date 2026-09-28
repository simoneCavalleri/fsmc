/**
 * @file rtm_emitter.hpp
 * @brief Requirements Traceability Matrix (RTM) generation and audit export.
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/model_checker.hpp"

namespace fsm::backend::rtm {
using ir::FsmIr;

using ::fsm::diagnostic::Diagnostic;
using ::fsm::diagnostic::DiagnosticEngine;
using ::fsm::middleend::analysis::ModelCheckResult;

/**
 * @enum RtmFormat
 * @brief Output report formats for requirements traceability.
 */
enum class RtmFormat { Json, Markdown };

inline RtmFormat rtm_format_from_string(std::string_view fmt) {
    if (fmt == "json" || fmt == "JSON")
        return RtmFormat::Json;
    return RtmFormat::Markdown;
}

/**
 * @struct RequirementRecord
 * @brief Aggregated traceability record for a single system requirement.
 */
struct RequirementRecord {
    std::string id;                                 ///< Formal requirement identifier (e.g. "REQ-001")
    std::string description;                        ///< Requirement statement text
    std::vector<std::string> covering_states;       ///< States tagged with this requirement
    std::vector<std::string> covering_transitions;  ///< Transitions tagged with this requirement
    std::vector<std::string> formal_properties;     ///< Linked LTL/CTL property IDs
    bool all_properties_passed{true};               ///< Verification pass status
};

/**
 * @class RtmEmitter
 * @brief Requirement Traceability Matrix (RTM) Emitter.
 *
 * Generates audit-ready compliance matrices (JSON & Markdown) linking formal
 * system requirements to states, transitions, and verified temporal properties.
 */
class RtmEmitter {
  public:
    /**
     * @brief Performs safety/traceability audit checking for unmapped requirements.
     */
    static void audit_traceability(const FsmIr& ir, DiagnosticEngine& diag);

    /**
     * @brief Emits complete traceability matrix in the specified format.
     */
    static std::string emit(const FsmIr& ir, const std::vector<ModelCheckResult>& results,
                            RtmFormat format = RtmFormat::Markdown);

    /**
     * @brief Emits machine-readable JSON compliance report.
     */
    static std::string emit_json(const FsmIr& ir, const std::vector<RequirementRecord>& records);

    /**
     * @brief Emits human-readable Markdown table compliance report.
     */
    static std::string emit_markdown(const FsmIr& ir, const std::vector<RequirementRecord>& records);

  private:
    static std::vector<RequirementRecord> aggregate_records(const FsmIr& ir,
                                                            const std::vector<ModelCheckResult>& results);

    static std::string escape_json(const std::string& str);
};

}  // namespace fsm::backend::rtm

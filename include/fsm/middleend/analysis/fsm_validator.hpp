/**
 * @file fsm_validator.hpp
 * @brief Structural, behavioral, and safety validation engine for FsmIr models.
 */

#pragma once

#include <set>
#include <string>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/semantic_analyzer.hpp"

namespace fsm::middleend::analysis {

using diagnostic::DiagnosticSeverity;
using ir::FsmIr;

/**
 * @struct DiagnosticMessage
 * @brief Categorized diagnostic message with severity tag.
 */
struct DiagnosticMessage {
    DiagnosticSeverity severity;  ///< Error, Warning, or Info severity level
    std::string category;         ///< Validation domain (e.g. "Structure", "Determinism")
    std::string message;          ///< Detailed explanatory message
};

/**
 * @struct ValidationResult
 * @brief Aggregated outcome of model validation including errors, warnings, and diagnostics.
 */
struct ValidationResult {
    bool is_valid = true;                        ///< False if any errors were encountered
    std::vector<std::string> errors;             ///< List of error messages
    std::vector<std::string> warnings;           ///< List of warning messages
    std::vector<DiagnosticMessage> diagnostics;  ///< Structured diagnostic records

    void add_error(const std::string& category, const std::string& msg) {
        is_valid = false;
        errors.push_back(msg);
        diagnostics.push_back({DiagnosticSeverity::Error, category, msg});
    }

    void add_warning(const std::string& category, const std::string& msg) {
        warnings.push_back(msg);
        diagnostics.push_back({DiagnosticSeverity::Warning, category, msg});
    }

    void add_safety_critical(const std::string& category, const std::string& msg) {
        warnings.push_back("[SAFETY CRITICAL] " + msg);
        diagnostics.push_back({DiagnosticSeverity::SafetyCritical, category, msg});
    }

    void add_info(const std::string& category, const std::string& msg) {
        diagnostics.push_back({DiagnosticSeverity::Info, category, msg});
    }
};

/**
 * @class FsmValidator
 * @brief Comprehensive model validator checking reachability, determinism, deadlock, and livelock.
 */
class FsmValidator {
  public:
    /**
     * @brief Performs complete validation suite over the given FsmIr model.
     * @param model FsmIr model to analyze.
     * @return Aggregated ValidationResult.
     */
    static ValidationResult validate(const FsmIr& model);

  private:
    static void validate_initial_state(const FsmIr& model, const std::set<std::string>& node_names,
                                       ValidationResult& result);

    static void validate_transition_endpoints(const FsmIr& model, const std::set<std::string>& node_names,
                                              ValidationResult& result);

    static void validate_choice_pseudostates(const FsmIr& model, ValidationResult& result);

    static void validate_reachability(const FsmIr& model, ValidationResult& result);

    static void validate_livelock_cycles(const FsmIr& model, ValidationResult& result);

    static void validate_deadlock_states(const FsmIr& model, ValidationResult& result);

    static void validate_transition_determinism(const FsmIr& model, ValidationResult& result);

    static void validate_timed_transitions(const FsmIr& model, ValidationResult& result);
};

}  // namespace fsm::middleend::analysis

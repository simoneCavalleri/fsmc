/**
 * @file semantic_analyzer.hpp
 * @brief Semantic validation and type checking for canonical FsmIr models.
 */

#pragma once

#include <string>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::analysis {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @struct SemanticAnalysisResult
 * @brief Analysis result structure holding semantic validity status and diagnostics.
 */
struct SemanticAnalysisResult {
    bool valid{true};                   ///< True if model has no semantic errors
    std::vector<std::string> errors;    ///< Collected semantic errors
    std::vector<std::string> warnings;  ///< Collected semantic warnings
};

/**
 * @class SemanticAnalyzer
 * @brief Middle-End Semantic Analyzer and Type Checker for Canonical FsmIr.
 */
class SemanticAnalyzer {
  public:
    /**
     * @brief Validates identifiers, port bindings, variable types, and initial states.
     */
    static bool validate(const FsmIr& ir, std::vector<std::string>& errors, std::vector<std::string>& warnings);

    /**
     * @brief Performs semantic analysis and returns structured SemanticAnalysisResult.
     */
    static SemanticAnalysisResult analyze(const FsmIr& ir);

    /**
     * @brief Validates model semantics and reports diagnostics directly to DiagnosticEngine.
     */
    static bool validate(const FsmIr& ir, DiagnosticEngine& diag);
};

}  // namespace fsm::middleend::analysis

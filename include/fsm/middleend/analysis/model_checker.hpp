/**
 * @file model_checker.hpp
 * @brief Formal verification and explicit state model checking engine.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/efsm_interval_analysis.hpp"

namespace fsm::middleend::analysis {

using namespace fsm::ir;
using diagnostic::DiagnosticEngine;

/**
 * @struct CounterexampleStep
 * @brief A single transition step in an execution trace violating a formal property.
 */
struct CounterexampleStep {
    std::size_t step_index{0};    ///< Sequence number along the execution trace
    std::string state_name;       ///< State visited during this step
    std::string event_name;       ///< Trigger event executed
    std::string guard_condition;  ///< Guard predicate evaluated
    std::string description;      ///< Human-readable explanation of the step
};

/**
 * @struct ModelCheckResult
 * @brief Detailed outcome of formal property verification.
 */
struct ModelCheckResult {
    bool passed{true};                                     ///< True if property holds globally
    std::string property_name;                             ///< Identifier of verified property
    std::string property_formula;                          ///< Formula specification string
    PropertyKind kind{PropertyKind::Safety};               ///< Safety, Liveness, or Invariant
    std::string violation_reason;                          ///< Explanation if verification failed
    std::vector<CounterexampleStep> counterexample_trace;  ///< Diagnostic counterexample trace

    /**
     * @brief Formats the counterexample trace into a human-readable diagnostic report.
     */
    [[nodiscard]] std::string format_counterexample() const;
};

/**
 * @class ModelChecker
 * @brief Formal Verification and Model Checking Engine.
 *
 * Explores the explicit state reachability graph (Kripke model) and evaluates
 * Linear Temporal Logic (LTL) and Computation Tree Logic (CTL) specifications:
 * - Safety Invariants: G (Predicate)
 * - Reachability / Target: F (Predicate)
 * - Response / Liveness: G (Trigger -> F (Target))
 * - Mutual Exclusion: G (!(StateA && StateB))
 * - Deadlock Freedom: G (!Deadlock)
 */
class ModelChecker {
  public:
    explicit ModelChecker(const FsmIr& ir);

    /**
     * @brief Verifies a single formal temporal property against the state machine.
     */
    ModelCheckResult verify_property(const FormalProperty& prop);

    /**
     * @brief Verifies all formal properties declared in the FsmIr model.
     */
    std::vector<ModelCheckResult> verify_all();

    /**
     * @brief Verifies data path bounds using abstract interval interpretation.
     */
    std::vector<EFSMAnalysisFinding> verify_efsm_data_paths(DiagnosticEngine& diag);

  private:
    struct GraphEdge {
        std::string target;
        std::string event;
        std::string guard;
    };

    const FsmIr& ir_;
    std::string root_state_;
    std::unordered_map<std::string, std::vector<GraphEdge>> adj_;
    std::unordered_set<std::string> reachable_states_;
    std::unordered_map<std::string, std::pair<std::string, GraphEdge>> predecessor_map_;

    void build_graph();
    [[nodiscard]] std::vector<CounterexampleStep> reconstruct_trace(const std::string& target_state,
                                                                    const std::string& violation_desc) const;
    bool eval_predicate(const PropertyAstNode& node, const std::string& state) const;
    ModelCheckResult check_invariant(const FormalProperty& prop, const PropertyAstNode& predicate);
    ModelCheckResult check_reachability(const FormalProperty& prop, const PropertyAstNode& target);
    ModelCheckResult check_response(const FormalProperty& prop, const PropertyAstNode& trigger,
                                    const PropertyAstNode& response_target);
};

}  // namespace fsm::middleend::analysis

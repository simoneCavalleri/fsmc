#include "fsm/middleend/pass_manager.hpp"

#include <chrono>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "fsm/middleend/analysis/efsm_interval_analysis.hpp"
#include "fsm/middleend/analysis/event_queue_bound_pass.hpp"
#include "fsm/middleend/analysis/guard_satisfiability_pass.hpp"
#include "fsm/middleend/analysis/livelock_analysis_pass.hpp"
#include "fsm/middleend/analysis/model_checker.hpp"
#include "fsm/middleend/analysis/priority_conflict_pass.hpp"
#include "fsm/middleend/analysis/semantic_analyzer.hpp"
#include "fsm/middleend/analysis/timed_invariants_verifier_pass.hpp"
#include "fsm/middleend/passes/boundary_action_fusion_pass.hpp"
#include "fsm/middleend/passes/choice_inlining_pass.hpp"
#include "fsm/middleend/passes/common_action_factoring_pass.hpp"
#include "fsm/middleend/passes/constant_folding_pass.hpp"
#include "fsm/middleend/passes/dead_action_elimination_pass.hpp"
#include "fsm/middleend/passes/dead_state_pruning_pass.hpp"
#include "fsm/middleend/passes/deferred_event_lowering_pass.hpp"
#include "fsm/middleend/passes/determinism_enforcement_pass.hpp"
#include "fsm/middleend/passes/fork_join_lowering_pass.hpp"
#include "fsm/middleend/passes/guard_simplification_pass.hpp"
#include "fsm/middleend/passes/history_lowering_pass.hpp"
#include "fsm/middleend/passes/orthogonal_interference_pass.hpp"
#include "fsm/middleend/passes/orthogonal_product_pass.hpp"
#include "fsm/middleend/passes/pipe_through_pass.hpp"
#include "fsm/middleend/passes/register_liveness_pass.hpp"
#include "fsm/middleend/passes/state_minimization_pass.hpp"
#include "fsm/middleend/passes/submachine_inlining_pass.hpp"
#include "fsm/middleend/passes/timed_deadlock_pass.hpp"
#include "fsm/middleend/passes/transition_fusion_pass.hpp"
#include "fsm/middleend/passes/wcet_analysis_pass.hpp"

namespace fsm::middleend {

using namespace diagnostic;
using namespace analysis;
using namespace passes;

// ============================================================================
// Pass: HierarchyCanonicalizationPass
// ============================================================================
std::string HierarchyCanonicalizationPass::name() const {
    return "HierarchyCanonicalization";
}

std::string HierarchyCanonicalizationPass::description() const {
    return "Normalizes state hierarchy, FQNs, and canonical order";
}

bool HierarchyCanonicalizationPass::run(FsmIr& ir, DiagnosticEngine& /*diag*/) {
    ir.normalize_hierarchy();
    ir.canonicalize();
    return true;
}

// ============================================================================
// Pass: SemanticValidationPass
// ============================================================================
std::string SemanticValidationPass::name() const {
    return "SemanticValidation";
}

std::string SemanticValidationPass::description() const {
    return "Validates variable/port targets, type resolution, and algebraic assignment semantics";
}

bool SemanticValidationPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    return ::fsm::middleend::SemanticAnalyzer::validate(ir, diag);
}

// ============================================================================
// Pass: GuardSimplificationPassWrapper
// ============================================================================
std::string GuardSimplificationPassWrapper::name() const {
    return GuardSimplificationPass::name();
}

std::string GuardSimplificationPassWrapper::description() const {
    return GuardSimplificationPass::description();
}

bool GuardSimplificationPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    GuardSimplificationPass pass;
    return pass.run(ir, diag);
}

// ============================================================================
// Pass: GuardSatisfiabilityPassWrapper
// ============================================================================
std::string GuardSatisfiabilityPassWrapper::name() const {
    return GuardSatisfiabilityPass::name();
}

std::string GuardSatisfiabilityPassWrapper::description() const {
    return GuardSatisfiabilityPass::description();
}

bool GuardSatisfiabilityPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    GuardSatisfiabilityPass pass;
    return pass.run(ir, diag);
}

// ============================================================================
// Pass: DeterminismEnforcementPassWrapper
// ============================================================================
std::string DeterminismEnforcementPassWrapper::name() const {
    return DeterminismEnforcementPass::name();
}

std::string DeterminismEnforcementPassWrapper::description() const {
    return DeterminismEnforcementPass::description();
}

bool DeterminismEnforcementPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    DeterminismEnforcementPass pass;
    return pass.run(ir, diag);
}

// ============================================================================
// Pass: OrthogonalInterferencePassWrapper
// ============================================================================
std::string OrthogonalInterferencePassWrapper::name() const {
    return OrthogonalInterferencePass::name();
}

std::string OrthogonalInterferencePassWrapper::description() const {
    return OrthogonalInterferencePass::description();
}

bool OrthogonalInterferencePassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    OrthogonalInterferencePass pass;
    return pass.run(ir, diag);
}

// ============================================================================
// Pass: ChoiceCompletenessPass
// ============================================================================
std::string ChoiceCompletenessPass::name() const {
    return "ChoiceCompleteness";
}

std::string ChoiceCompletenessPass::description() const {
    return "Verifies choice pseudostate branch exhaustiveness and determinism";
}

bool ChoiceCompletenessPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    for (const auto& choice : ir.choice_nodes) {
        std::vector<const TransitionEdge*> outgoing;
        for (const auto& t : ir.transitions) {
            if (t.source == choice.name) {
                outgoing.push_back(&t);
            }
        }

        if (outgoing.empty()) {
            diag.report(Diagnostic::warning(
                "W0101", "Choice pseudostate '" + choice.name + "' has no outgoing branches (trap)."));
            continue;
        }

        bool has_unconditional = false;
        std::set<std::string> seen_guards;

        for (const auto* t : outgoing) {
            if (!t->guard.has_value() || *t->guard == "else" || *t->guard == "otherwise" || *t->guard == "default") {
                has_unconditional = true;
            } else {
                if (seen_guards.count(*t->guard) != 0) {
                    diag.report(Diagnostic::warning("W0102", "Choice pseudostate '" + choice.name +
                                                                 "' has duplicate guard condition '[" + *t->guard +
                                                                 "]' leading to non-deterministic branch selection."));
                }
                seen_guards.insert(*t->guard);
            }
        }

        if (!has_unconditional) {
            diag.report(Diagnostic::warning(
                "W0103", "Choice pseudostate '" + choice.name +
                             "' lacks an unconditional else/default fallback branch (potential stall)."));
        }
    }
    return true;
}

// ============================================================================
// Pass: ChoiceInliningPassWrapper
// ============================================================================
std::string ChoiceInliningPassWrapper::name() const {
    return ChoiceInliningPass::name();
}

std::string ChoiceInliningPassWrapper::description() const {
    return ChoiceInliningPass::description();
}

bool ChoiceInliningPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    ChoiceInliningPass pass;
    return pass.run(ir, diag);
}

// ============================================================================
// Pass: ModelSafetyVerifierPass
// ============================================================================
std::string ModelSafetyVerifierPass::name() const {
    return "ModelSafetyVerifier";
}

std::string ModelSafetyVerifierPass::description() const {
    return "Formal verification of reachability, deadlock traps, and livelocks";
}

bool ModelSafetyVerifierPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    std::string root = ir.initial_state_id.empty() ? ir.initial_state : ir.initial_state_id;
    if (root.empty() && !ir.states.empty()) {
        root = ir.states.front().name;
    }

    if (root.empty()) {
        return true;
    }

    // Reachability analysis with bidirectional hierarchy propagation
    std::unordered_set<std::string> reachable;
    std::queue<std::string> queue;
    queue.push(root);
    reachable.insert(root);

    while (!queue.empty()) {
        std::string curr = queue.front();
        queue.pop();

        // 1. Include sub-states if composite or parallel
        for (const auto& s : ir.states) {
            if (s.parent_state == curr && reachable.count(s.name) == 0) {
                reachable.insert(s.name);
                queue.push(s.name);
            }
        }

        // 2. Include parent state if sub-state is reached
        const auto* curr_st = ir.find_state(curr);
        if (curr_st != nullptr && !curr_st->parent_state.empty()) {
            if (reachable.count(curr_st->parent_state) == 0) {
                reachable.insert(curr_st->parent_state);
                queue.push(curr_st->parent_state);
            }
        }

        // 3. Outgoing transitions
        for (const auto& t : ir.transitions) {
            if (t.source == curr) {
                if (reachable.count(t.target) == 0 && !t.target.empty()) {
                    reachable.insert(t.target);
                    queue.push(t.target);
                }
            }
        }
    }

    for (const auto& s : ir.states) {
        if (s.kind == StateKind::Final || ir.is_choice_node(s.name))
            continue;
        if (reachable.count(s.name) == 0) {
            diag.report(Diagnostic::warning("W0201", "State unreachable from initial state: '" + s.name + "'."));
        }
    }

    // Helper lambda: check if a state or any of its ancestors has outgoing transitions
    auto has_outgoing_hierarchical = [&](const StateNode& state) {
        // Check direct outgoing
        for (const auto& t : ir.transitions) {
            if (t.source == state.name) {
                return true;
            }
        }
        // Check ancestors
        std::string parent_name = state.parent_state;
        while (!parent_name.empty()) {
            for (const auto& t : ir.transitions) {
                if (t.source == parent_name) {
                    return true;
                }
            }
            const auto* p = ir.find_state(parent_name);
            parent_name = (p != nullptr) ? p->parent_state : "";
        }
        // Check descendants (if composite)
        std::queue<std::string> child_q;
        child_q.push(state.name);
        while (!child_q.empty()) {
            std::string c_curr = child_q.front();
            child_q.pop();
            for (const auto& child : ir.states) {
                if (child.parent_state == c_curr) {
                    for (const auto& t : ir.transitions) {
                        if (t.source == child.name) {
                            return true;
                        }
                    }
                    child_q.push(child.name);
                }
            }
        }
        return false;
    };

    // Trap state (deadlock) analysis
    for (const auto& s : ir.states) {
        if (s.kind == StateKind::Final || ir.is_choice_node(s.name) || s.is_composite ||
            s.name.rfind("Terminal", 0) == 0 || s.name.rfind("Final", 0) == 0)
            continue;

        bool has_in = false;
        for (const auto& t : ir.transitions) {
            if (t.target == s.name) {
                has_in = true;
                break;
            }
        }

        bool has_out = has_outgoing_hierarchical(s);

        if (has_in && !has_out) {
            diag.report(Diagnostic::warning("W0202", "Potential trap / deadlock state: '" + s.name +
                                                         "' has incoming transitions but no outgoing transitions."));
        }
    }

    return true;
}

// ============================================================================
// Pass: DeadStatePruningPassWrapper
// ============================================================================
DeadStatePruningPassWrapper::DeadStatePruningPassWrapper(bool enable_pruning)
    : pass_(std::make_unique<DeadStatePruningPass>(enable_pruning)) {}

DeadStatePruningPassWrapper::~DeadStatePruningPassWrapper() = default;

std::string DeadStatePruningPassWrapper::name() const {
    return DeadStatePruningPass::name();
}

std::string DeadStatePruningPassWrapper::description() const {
    return DeadStatePruningPass::description();
}

bool DeadStatePruningPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    return pass_->run(ir, diag);
}

// ============================================================================
// Pass: ModelCheckingPass
// ============================================================================
std::string ModelCheckingPass::name() const {
    return "ModelCheckingPass";
}

std::string ModelCheckingPass::description() const {
    return "Executes formal model checking verification for temporal LTL/CTL properties and safety invariants";
}

bool ModelCheckingPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    if (ir.properties.empty()) {
        return true;
    }

    ModelChecker checker(ir);
    auto results = checker.verify_all();

    for (const auto& res : results) {
        if (!res.passed) {
            std::string msg = "Formal property '" + res.property_name + "' [" + property_kind_to_string(res.kind) +
                              "] VIOLATED: " + res.violation_reason;
            std::string trace_str = res.format_counterexample();
            if (!trace_str.empty()) {
                msg += "\n" + trace_str;
            }
            diag.report(Diagnostic::error("E_MODEL_CHECK_VIOLATION", msg));
        }
    }

    return !diag.has_errors();
}

// ============================================================================
// Pass: TimedDeadlockPassWrapper
// ============================================================================
std::string TimedDeadlockPassWrapper::name() const {
    return TimedDeadlockPass::name();
}

std::string TimedDeadlockPassWrapper::description() const {
    return TimedDeadlockPass::description();
}

bool TimedDeadlockPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    TimedDeadlockPass pass;
    return pass.run(ir, diag);
}

// ============================================================================
// Pass: EFSMDataPathPass
// ============================================================================
std::string EFSMDataPathPass::name() const {
    return "EFSMDataPathPass";
}

std::string EFSMDataPathPass::description() const {
    return "Analyzes EFSM data paths and variable ranges via abstract interpretation to detect unsatisfiable "
           "guards";
}

bool EFSMDataPathPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    if (ir.variables.empty() && ir.ports.empty()) {
        return true;
    }

    EFSMIntervalAnalyzer analyzer(ir);
    analyzer.analyze(diag);
    return !diag.has_errors();
}

// ============================================================================
// Pass: OrthogonalProductPassWrapper
// ============================================================================
std::string OrthogonalProductPassWrapper::name() const {
    return OrthogonalProductPass::name();
}

std::string OrthogonalProductPassWrapper::description() const {
    return OrthogonalProductPass::description();
}

bool OrthogonalProductPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    OrthogonalProductPass pass;
    return pass.run(ir, diag);
}

// ============================================================================
// Pass: WcetAnalysisPassWrapper
// ============================================================================
WcetAnalysisPassWrapper::WcetAnalysisPassWrapper(std::size_t max_microstep_threshold)
    : pass_(std::make_unique<WcetAnalysisPass>(max_microstep_threshold)) {}

WcetAnalysisPassWrapper::~WcetAnalysisPassWrapper() = default;

std::string WcetAnalysisPassWrapper::name() const {
    return WcetAnalysisPass::name();
}

std::string WcetAnalysisPassWrapper::description() const {
    return WcetAnalysisPass::description();
}

bool WcetAnalysisPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    return pass_->run(ir, diag);
}

// ============================================================================
// Pass: ConstantFoldingPassWrapper
// ============================================================================
std::string ConstantFoldingPassWrapper::name() const {
    return ConstantFoldingPass::name();
}

std::string ConstantFoldingPassWrapper::description() const {
    return ConstantFoldingPass::description();
}

bool ConstantFoldingPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    ConstantFoldingPass pass;
    return pass.run(ir, diag);
}

// ============================================================================
// Pass: StateMinimizationPassWrapper
// ============================================================================
std::string StateMinimizationPassWrapper::name() const {
    return StateMinimizationPass::name();
}

std::string StateMinimizationPassWrapper::description() const {
    return StateMinimizationPass::description();
}

bool StateMinimizationPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    StateMinimizationPass pass;
    return pass.run(ir, diag);
}

// ============================================================================
// Pass: PipeThroughPassWrapper
// ============================================================================
PipeThroughPassWrapper::PipeThroughPassWrapper(std::string cmd)
    : pass_(std::make_unique<PipeThroughPass>(std::move(cmd))) {}

PipeThroughPassWrapper::~PipeThroughPassWrapper() = default;

std::string PipeThroughPassWrapper::name() const {
    return PipeThroughPass::name();
}

std::string PipeThroughPassWrapper::description() const {
    return PipeThroughPass::description();
}

bool PipeThroughPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    return pass_->run(ir, diag);
}

// ============================================================================
// Structural Lowering Pass Wrappers
// ============================================================================

std::string ForkJoinLoweringPassWrapper::name() const {
    return ForkJoinLoweringPass::name();
}
std::string ForkJoinLoweringPassWrapper::description() const {
    return ForkJoinLoweringPass::description();
}
bool ForkJoinLoweringPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    ForkJoinLoweringPass pass;
    return pass.run(ir, diag);
}

std::string HistoryLoweringPassWrapper::name() const {
    return HistoryLoweringPass::name();
}
std::string HistoryLoweringPassWrapper::description() const {
    return HistoryLoweringPass::description();
}
bool HistoryLoweringPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    HistoryLoweringPass pass;
    return pass.run(ir, diag);
}

std::string DeferredEventLoweringPassWrapper::name() const {
    return DeferredEventLoweringPass::name();
}
std::string DeferredEventLoweringPassWrapper::description() const {
    return DeferredEventLoweringPass::description();
}
bool DeferredEventLoweringPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    DeferredEventLoweringPass pass;
    return pass.run(ir, diag);
}

std::string BoundaryActionFusionPassWrapper::name() const {
    return BoundaryActionFusionPass::name();
}
std::string BoundaryActionFusionPassWrapper::description() const {
    return BoundaryActionFusionPass::description();
}
bool BoundaryActionFusionPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    BoundaryActionFusionPass pass;
    return pass.run(ir, diag);
}

// ============================================================================
// Data-Path Optimization Wrappers
// ============================================================================

std::string DeadActionEliminationPassWrapper::name() const {
    return DeadActionEliminationPass::name();
}
std::string DeadActionEliminationPassWrapper::description() const {
    return DeadActionEliminationPass::description();
}
bool DeadActionEliminationPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    DeadActionEliminationPass pass;
    return pass.run(ir, diag);
}

std::string RegisterLivenessPassWrapper::name() const {
    return RegisterLivenessPass::name();
}
std::string RegisterLivenessPassWrapper::description() const {
    return RegisterLivenessPass::description();
}
bool RegisterLivenessPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    RegisterLivenessPass pass;
    return pass.run(ir, diag);
}

std::string TransitionFusionPassWrapper::name() const {
    return TransitionFusionPass::name();
}
std::string TransitionFusionPassWrapper::description() const {
    return TransitionFusionPass::description();
}
bool TransitionFusionPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    TransitionFusionPass pass;
    return pass.run(ir, diag);
}

std::string CommonActionFactoringPassWrapper::name() const {
    return CommonActionFactoringPass::name();
}
std::string CommonActionFactoringPassWrapper::description() const {
    return CommonActionFactoringPass::description();
}
bool CommonActionFactoringPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    CommonActionFactoringPass pass;
    return pass.run(ir, diag);
}

// ============================================================================
// Formal Verification Wrappers
// ============================================================================

std::string LivelockAnalysisPassWrapper::name() const {
    return LivelockAnalysisPass::name();
}
std::string LivelockAnalysisPassWrapper::description() const {
    return LivelockAnalysisPass::description();
}
bool LivelockAnalysisPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    LivelockAnalysisPass pass;
    return pass.run(ir, diag);
}

std::string PriorityConflictPassWrapper::name() const {
    return PriorityConflictPass::name();
}
std::string PriorityConflictPassWrapper::description() const {
    return PriorityConflictPass::description();
}
bool PriorityConflictPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    PriorityConflictPass pass;
    return pass.run(ir, diag);
}

std::string TimedInvariantsVerifierPassWrapper::name() const {
    return TimedInvariantsVerifierPass::name();
}
std::string TimedInvariantsVerifierPassWrapper::description() const {
    return TimedInvariantsVerifierPass::description();
}
bool TimedInvariantsVerifierPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    TimedInvariantsVerifierPass pass;
    return pass.run(ir, diag);
}

std::string EventQueueBoundPassWrapper::name() const {
    return EventQueueBoundPass::name();
}
std::string EventQueueBoundPassWrapper::description() const {
    return EventQueueBoundPass::description();
}
bool EventQueueBoundPassWrapper::run(FsmIr& ir, DiagnosticEngine& diag) {
    EventQueueBoundPass pass;
    return pass.run(ir, diag);
}

// ============================================================================
// PassManager: Pipeline Coordinator
// ============================================================================
PassManager::PassManager() = default;
PassManager::~PassManager() {
    passes_.clear();
}
PassManager::PassManager(PassManager&&) noexcept = default;
PassManager& PassManager::operator=(PassManager&&) noexcept = default;

PassManager PassManager::create_default_pipeline() {
    PassManager pm;
    pm.add_pass(std::make_unique<HierarchyCanonicalizationPass>());
    pm.add_pass(std::make_unique<SemanticValidationPass>());
    pm.add_pass(std::make_unique<GuardSimplificationPassWrapper>());
    pm.add_pass(std::make_unique<DeterminismEnforcementPassWrapper>());
    pm.add_pass(std::make_unique<OrthogonalInterferencePassWrapper>());
    pm.add_pass(std::make_unique<ChoiceCompletenessPass>());
    pm.add_pass(std::make_unique<ChoiceInliningPassWrapper>());
    pm.add_pass(std::make_unique<TimedDeadlockPassWrapper>());
    pm.add_pass(std::make_unique<EFSMDataPathPass>());
    pm.add_pass(std::make_unique<GuardSatisfiabilityPassWrapper>());
    pm.add_pass(std::make_unique<ModelSafetyVerifierPass>());
    pm.add_pass(std::make_unique<ModelCheckingPass>());
    return pm;
}

PassManager PassManager::create_optimizing_pipeline(bool prune_dead_states, bool minimize_states) {
    PassManager pm;
    pm.add_pass(std::make_unique<HierarchyCanonicalizationPass>());
    pm.add_pass(std::make_unique<SemanticValidationPass>());
    pm.add_pass(std::make_unique<GuardSimplificationPassWrapper>());
    pm.add_pass(std::make_unique<ConstantFoldingPassWrapper>());
    pm.add_pass(std::make_unique<DeterminismEnforcementPassWrapper>());
    pm.add_pass(std::make_unique<OrthogonalInterferencePassWrapper>());
    if (prune_dead_states) {
        pm.add_pass(std::make_unique<DeadStatePruningPassWrapper>(true));
    }
    if (minimize_states) {
        pm.add_pass(std::make_unique<StateMinimizationPassWrapper>());
    }
    pm.add_pass(std::make_unique<ChoiceCompletenessPass>());
    pm.add_pass(std::make_unique<ChoiceInliningPassWrapper>());
    pm.add_pass(std::make_unique<TimedDeadlockPassWrapper>());
    pm.add_pass(std::make_unique<WcetAnalysisPassWrapper>());
    pm.add_pass(std::make_unique<EFSMDataPathPass>());
    pm.add_pass(std::make_unique<GuardSatisfiabilityPassWrapper>());
    pm.add_pass(std::make_unique<ModelSafetyVerifierPass>());
    pm.add_pass(std::make_unique<ModelCheckingPass>());
    return pm;
}

PassManager PassManager::create_verified_7stage_pipeline(bool optimize) {
    PassManager pm;
    // Stage 1: Frontend Canonicalization & Syntax Desugaring
    pm.add_pass(std::make_unique<HierarchyCanonicalizationPass>());
    pm.add_pass(std::make_unique<SemanticValidationPass>());
    pm.add_pass(std::make_unique<GuardSimplificationPassWrapper>());

    // Stage 2: Structural Lowering Suite
    pm.add_pass(std::make_unique<ForkJoinLoweringPassWrapper>());
    pm.add_pass(std::make_unique<HistoryLoweringPassWrapper>());
    pm.add_pass(std::make_unique<DeferredEventLoweringPassWrapper>());
    pm.add_pass(std::make_unique<BoundaryActionFusionPassWrapper>());

    // Stage 3: Formal Invariant & Safety Verification
    pm.add_pass(std::make_unique<LivelockAnalysisPassWrapper>());
    pm.add_pass(std::make_unique<PriorityConflictPassWrapper>());
    pm.add_pass(std::make_unique<TimedInvariantsVerifierPassWrapper>());
    pm.add_pass(std::make_unique<EventQueueBoundPassWrapper>());
    pm.add_pass(std::make_unique<GuardSatisfiabilityPassWrapper>());

    // Stage 4: Symbolic Model Checking
    pm.add_pass(std::make_unique<TimedDeadlockPassWrapper>());
    pm.add_pass(std::make_unique<ModelSafetyVerifierPass>());
    pm.add_pass(std::make_unique<ModelCheckingPass>());

    // Stage 5: Optimization & Minimization
    if (optimize) {
        pm.add_pass(std::make_unique<ConstantFoldingPassWrapper>());
        pm.add_pass(std::make_unique<DeadStatePruningPassWrapper>(true));
        pm.add_pass(std::make_unique<DeadActionEliminationPassWrapper>());
        pm.add_pass(std::make_unique<CommonActionFactoringPassWrapper>());
        pm.add_pass(std::make_unique<TransitionFusionPassWrapper>());
    }

    // Stage 6: Data-Path Optimization & Register Allocation
    pm.add_pass(std::make_unique<EFSMDataPathPass>());
    if (optimize) {
        pm.add_pass(std::make_unique<RegisterLivenessPassWrapper>());
    }

    // Stage 7: Backend Preparation & Code Emitter Handoff
    pm.add_pass(std::make_unique<DeterminismEnforcementPassWrapper>());
    pm.add_pass(std::make_unique<WcetAnalysisPassWrapper>());

    return pm;
}

void PassManager::add_pass(std::unique_ptr<IPass> pass) {
    passes_.push_back(std::move(pass));
}

bool PassManager::load_plugin(const std::string& plugin_path, DiagnosticEngine& diag) {
    return plugin_loader_.load_plugin(plugin_path, *this, diag);
}

bool PassManager::run(FsmIr& ir, DiagnosticEngine& diag) {
    stats_.clear();
    std::unordered_set<std::string> executed_passes;

    for (auto& pass : passes_) {
        // Validate prerequisites
        for (const auto& prereq : pass->required_prerequisites()) {
            if (!executed_passes.count(prereq)) {
                diag.report(diagnostic::Diagnostic::warning(
                    "W0501", "Pipeline ordering warning: Pass '" + pass->name() + "' specifies prerequisite '" +
                                 prereq + "' which was not executed beforehand."));
            }
        }

        auto t0 = std::chrono::steady_clock::now();
        bool ok = pass->run(ir, diag);
        auto t1 = std::chrono::steady_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        PassExecutionStats s;
        s.pass_name = pass->name();
        s.duration_ms = elapsed_ms;
        s.modified_ir = ok && !pass->preserves_ir();
        stats_.push_back(s);

        if (!ok && diag.has_errors()) {
            return false;
        }

        executed_passes.insert(pass->name());
        for (const auto& inv : pass->invalidated_analyses()) {
            executed_passes.erase(inv);
        }
    }
    return !diag.has_errors();
}

const std::vector<PassExecutionStats>& PassManager::get_stats() const noexcept {
    return stats_;
}

}  // namespace fsm::middleend

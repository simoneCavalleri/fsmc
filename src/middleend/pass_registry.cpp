#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fsm/middleend/analysis/event_queue_bound_pass.hpp"
#include "fsm/middleend/analysis/guard_satisfiability_pass.hpp"
#include "fsm/middleend/analysis/livelock_analysis_pass.hpp"
#include "fsm/middleend/analysis/priority_conflict_pass.hpp"
#include "fsm/middleend/analysis/timed_invariants_verifier_pass.hpp"
#include "fsm/middleend/pass_interface.hpp"
#include "fsm/middleend/pass_manager.hpp"
#include "fsm/middleend/passes/boundary_action_fusion_pass.hpp"
#include "fsm/middleend/passes/choice_inlining_pass.hpp"
#include "fsm/middleend/passes/common_action_factoring_pass.hpp"
#include "fsm/middleend/passes/connective_junction_chaining_pass.hpp"
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
#include "fsm/middleend/passes/sampled_change_trigger_pass.hpp"
#include "fsm/middleend/passes/state_minimization_pass.hpp"
#include "fsm/middleend/passes/submachine_inlining_pass.hpp"
#include "fsm/middleend/passes/timed_deadlock_pass.hpp"
#include "fsm/middleend/passes/transition_fusion_pass.hpp"
#include "fsm/middleend/passes/wcet_analysis_pass.hpp"

namespace fsm::middleend {

using namespace analysis;
using namespace passes;

const std::vector<PassDescriptor>& PassRegistry::descriptors() {
    static const std::vector<PassDescriptor> kDescriptors = {
        {"hierarchy-canonicalization",
         {"canonicalize", "HierarchyCanonicalization"},
         "Canonicalization",
         "Normalizes hierarchy, sorts IR collections, and computes deterministic IDs.",
         false,
         []() { return std::make_unique<HierarchyCanonicalizationPass>(); }},
        {"semantic-validation",
         {"SemanticValidation"},
         "Verification",
         "Validates types, port/register references, signal payloads, and structural integrity.",
         true,
         []() { return std::make_unique<SemanticValidationPass>(); }},
        {"guard-simplification",
         {},
         "Optimization",
         "Simplifies redundant boolean guard expressions and algebraic tautologies.",
         false,
         []() { return std::make_unique<GuardSimplificationPass>(); }},
        {"determinism-enforcement",
         {"determinism"},
         "Verification",
         "Verifies transition determinism and detects overlapping nondeterministic triggers.",
         true,
         []() { return std::make_unique<DeterminismEnforcementPass>(); }},
        {"orthogonal-interference",
         {"race-check"},
         "Verification",
         "Detects write-write and read-write data races across parallel orthogonal regions.",
         true,
         []() { return std::make_unique<OrthogonalInterferencePass>(); }},
        {"dead-state-pruning",
         {},
         "Optimization",
         "Prunes unreachable states and dead transitions from the state graph.",
         false,
         []() { return std::make_unique<DeadStatePruningPass>(true); }},
        {"choice-completeness",
         {"ChoiceCompleteness"},
         "Verification",
         "Checks that Choice pseudostates have exhaustive outgoing branches (or else fallback).",
         true,
         []() { return std::make_unique<ChoiceCompletenessPass>(); }},
        {"choice-inlining",
         {},
         "Lowering",
         "Inlines UML Choice pseudostates into guarded direct transitions.",
         false,
         []() { return std::make_unique<ChoiceInliningPass>(); }},
        {"timed-deadlock",
         {},
         "Verification",
         "Detects timed deadlocks and Zeno zero-time cycles in timed automata.",
         true,
         []() { return std::make_unique<TimedDeadlockPass>(); }},
        {"efsm-data-path",
         {"EFSMDataPath"},
         "Analysis",
         "Performs EFSM interval abstract interpretation on numerical guards and registers.",
         true,
         []() { return std::make_unique<EFSMDataPathPass>(); }},
        {"safety-verifier",
         {"ModelSafetyVerifier"},
         "Verification",
         "Verifies graph reachability, initial state validity, and deadlock trap freedom.",
         true,
         []() { return std::make_unique<ModelSafetyVerifierPass>(); }},
        {"model-checking",
         {"ModelCheckingPass"},
         "Verification",
         "Evaluates LTL/CTL temporal logic specifications against the explicit Kripke graph.",
         true,
         []() { return std::make_unique<ModelCheckingPass>(); }},
        {"orthogonal-product",
         {},
         "Lowering",
         "Expands parallel orthogonal regions into deterministic Cartesian product states.",
         false,
         []() { return std::make_unique<OrthogonalProductPass>(); }},
        {"wcet-analysis",
         {},
         "Analysis",
         "Computes static worst-case execution time (micro-step bound) per macro-step.",
         true,
         []() { return std::make_unique<WcetAnalysisPass>(); }},
        {"constant-folding",
         {},
         "Optimization",
         "Folds constant expressions and propagates compile-time known values.",
         false,
         []() { return std::make_unique<ConstantFoldingPass>(); }},
        {"state-minimization",
         {},
         "Optimization",
         "Minimizes equivalent states using Hopcroft/Moore DFA partition refinement.",
         false,
         []() { return std::make_unique<StateMinimizationPass>(); }},
        {"guard-satisfiability",
         {},
         "Verification",
         "Verifies guard satisfiability and detects statically contradictory or shadowed guards.",
         true,
         []() { return std::make_unique<GuardSatisfiabilityPass>(); }},
        {"fork-join-lowering",
         {},
         "Lowering",
         "Lowers UML Fork and Join pseudostates into synchronized parallel region transitions.",
         false,
         []() { return std::make_unique<ForkJoinLoweringPass>(); }},
        {"history-lowering",
         {},
         "Lowering",
         "Lowers UML shallow and deep history pseudostates into explicit register state tracking.",
         false,
         []() { return std::make_unique<HistoryLoweringPass>(); }},
        {"deferred-event-lowering",
         {},
         "Lowering",
         "Lowers deferred events into explicit software queue state machine transitions.",
         false,
         []() { return std::make_unique<DeferredEventLoweringPass>(); }},
        {"boundary-action-fusion",
         {},
         "Lowering",
         "Fuses LCA hierarchical exit/entry boundary actions into atomic transition actions.",
         false,
         []() { return std::make_unique<BoundaryActionFusionPass>(); }},
        {"dead-action-elimination",
         {},
         "Optimization",
         "Eliminates dead register writes whose values are never read on any reachable path.",
         false,
         []() { return std::make_unique<DeadActionEliminationPass>(); }},
        {"register-liveness",
         {},
         "Optimization",
         "Performs backward dataflow liveness analysis and interference-graph register coloring.",
         false,
         []() { return std::make_unique<RegisterLivenessPass>(); }},
        {"transition-fusion",
         {},
         "Optimization",
         "Fuses consecutive anonymous transitions across pass-through states.",
         false,
         []() { return std::make_unique<TransitionFusionPass>(); }},
        {"common-action-factoring",
         {},
         "Optimization",
         "Hoists common entry/exit actions across sibling transitions into state hooks.",
         false,
         []() { return std::make_unique<CommonActionFactoringPass>(); }},
        {"livelock-analysis",
         {},
         "Verification",
         "Detects autonomous zero-event livelock cycles in the state graph.",
         true,
         []() { return std::make_unique<LivelockAnalysisPass>(); }},
        {"priority-conflict-check",
         {},
         "Verification",
         "Detects ambiguous transition priority conflicts on shared source states and triggers.",
         true,
         []() { return std::make_unique<PriorityConflictPass>(); }},
        {"timed-invariants-verifier",
         {},
         "Verification",
         "Verifies state residence time invariants against outgoing timed transition bounds.",
         true,
         []() { return std::make_unique<TimedInvariantsVerifierPass>(); }},
        {"event-queue-bound",
         {},
         "Analysis",
         "Computes static upper bounds on required runtime event queue capacity.",
         false,
         []() { return std::make_unique<EventQueueBoundPass>(); }},
        {"submachine-inlining",
         {"inline-submachines"},
         "Lowering",
         "Inlines referenced submachine statecharts into the host FsmIr.",
         false,
         []() { return std::make_unique<SubmachineInliningPass>(); }},
        {"sampled-change-trigger",
         {"sampled-change-triggers"},
         "Lowering",
         "Lowers continuous when(...) change triggers into discrete z^-1 edge detectors.",
         false,
         []() { return std::make_unique<SampledChangeTriggerPass>(); }},
        {"connective-junction-chaining",
         {},
         "Lowering",
         "Chains multi-hop connective junction paths into atomic composite transitions.",
         false,
         []() { return std::make_unique<ConnectiveJunctionChainingPass>(); }},
        {"pipe-through",
         {},
         "Extensibility",
         "Pipes serialized Canonical IR JSON through an external filter command.",
         false,
         []() { return std::make_unique<PipeThroughPass>(""); }},
    };
    return kDescriptors;
}

std::vector<std::string_view> PassRegistry::registered_names() {
    std::vector<std::string_view> names;
    const auto& descs = descriptors();
    names.reserve(descs.size());
    for (const auto& d : descs) {
        names.push_back(d.canonical_name);
    }
    return names;
}

std::unique_ptr<IPass> PassRegistry::create(std::string_view pass_name, std::string_view pipe_cmd,
                                            SubmachineResolver submachine_resolver) {
    if (pass_name == "pipe-through") {
        return std::make_unique<PipeThroughPass>(std::string(pipe_cmd));
    }
    if (pass_name == "submachine-inlining" || pass_name == "inline-submachines") {
        return std::make_unique<SubmachineInliningPass>(std::move(submachine_resolver));
    }
    for (const auto& desc : descriptors()) {
        if (desc.canonical_name == pass_name) {
            return desc.factory();
        }
        for (const auto& alias : desc.aliases) {
            if (alias == pass_name) {
                return desc.factory();
            }
        }
    }
    return nullptr;
}

}  // namespace fsm::middleend

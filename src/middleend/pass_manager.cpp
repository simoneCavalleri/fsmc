#include "fsm/middleend/pass_manager.hpp"

#include <algorithm>
#include <cctype>
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
    if (ir.states.empty() && ir.choice_nodes.empty()) {
        diag.report(Diagnostic::error("E0001", "State machine does not contain any states."));
        return false;
    }

    std::set<std::string> node_names;
    for (const auto& s : ir.states) {
        if (!s.name.empty()) {
            node_names.insert(s.name);
        }
        if (!s.id.empty()) {
            node_names.insert(s.id);
        }
    }
    for (const auto& c : ir.choice_nodes) {
        if (!c.name.empty()) {
            node_names.insert(c.name);
        }
    }

    // 1. Initial state validation
    std::string init_st = ir.initial_state;
    if (init_st.empty() && !ir.initial_state_id.empty()) {
        if (const auto* s = ir.find_state_by_id(ir.initial_state_id)) {
            init_st = s->name;
        } else {
            init_st = ir.initial_state_id;
        }
    }
    if (init_st.empty()) {
        if (!ir.states.empty()) {
            diag.report(Diagnostic::warning("W0001", "No initial state '[*]' specified. Defaulting to first state: '" +
                                                         ir.states.front().name + "'."));
        }
    } else if (node_names.count(init_st) == 0 && ir.find_state(init_st) == nullptr &&
               ir.find_state_by_id(init_st) == nullptr) {
        diag.report(Diagnostic::error("E0002", "Initial state '" + init_st + "' is not defined in the state list."));
    }

    // 2. Transition endpoint validation
    for (const auto& t : ir.transitions) {
        const bool src_found = node_names.count(t.source) != 0 ||
                               (!t.source_id.empty() && node_names.count(t.source_id) != 0) ||
                               ir.find_state(t.source) != nullptr || ir.find_state_by_id(t.source) != nullptr;
        if (!src_found) {
            diag.report(Diagnostic::error("E0003", "Unknown source state/node in transition: '" + t.source + "'."));
        }
        const bool tgt_found = node_names.count(t.target) != 0 ||
                               (!t.target_id.empty() && node_names.count(t.target_id) != 0) ||
                               ir.find_state(t.target) != nullptr || ir.find_state_by_id(t.target) != nullptr ||
                               t.target_is_history || t.target == "[H]" || t.target == "[H*]";
        if (!tgt_found) {
            diag.report(Diagnostic::error("E0003", "Unknown target state/node in transition: '" + t.target + "'."));
        }
    }

    // 3. State hierarchy acyclicity and parent existence
    for (const auto& s : ir.states) {
        std::set<std::string> visited;
        visited.insert(s.name);
        std::string curr = s.parent_state;
        while (!curr.empty()) {
            if (visited.count(curr) != 0) {
                diag.report(Diagnostic::error(
                    "E0004", "Cyclic parent-child state hierarchy detected involving state '" + curr + "'."));
                break;
            }
            visited.insert(curr);
            const auto* p = ir.find_state(curr);
            if (p == nullptr) {
                diag.report(Diagnostic::error(
                    "E0005", "State '" + s.name + "' references unknown parent state '" + curr + "'."));
                break;
            }
            curr = p->parent_state;
        }
    }

    const bool sem_ok = ::fsm::middleend::SemanticAnalyzer::validate(ir, diag);
    return sem_ok && !diag.has_errors();
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
            diag.report(Diagnostic::error("E0101",
                                          "Choice pseudostate '" + choice.name + "' has no outgoing branches (trap)."));
            continue;
        }

        int fallback_count = 0;
        std::set<std::string> seen_guards;

        for (const auto* t : outgoing) {
            if (!t->guard.has_value() || t->guard->empty() || *t->guard == "else" || *t->guard == "otherwise" ||
                *t->guard == "default") {
                ++fallback_count;
            } else {
                if (seen_guards.count(*t->guard) != 0) {
                    diag.report(Diagnostic::warning("W0102", "Choice pseudostate '" + choice.name +
                                                                 "' has duplicate guard condition '[" + *t->guard +
                                                                 "]' leading to non-deterministic branch selection."));
                }
                seen_guards.insert(*t->guard);
            }
        }

        if (fallback_count == 0) {
            diag.report(Diagnostic::warning(
                "W0103", "Choice pseudostate '" + choice.name +
                             "' lacks an unconditional else/default fallback branch (potential stall)."));
        } else if (fallback_count > 1) {
            diag.report(Diagnostic::warning(
                "W0104", "Choice pseudostate '" + choice.name +
                             "' has multiple unconditional else/default fallback branches (non-deterministic)."));
        }
    }
    return !diag.has_errors();
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
    std::string root = ir.initial_state;
    if (root.empty()) {
        if (const auto* s = ir.find_state_by_id(ir.initial_state_id)) {
            root = s->name;
        } else {
            root = ir.initial_state_id;
        }
    }
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
    if (const auto* root_node = ir.find_state_by_name(root)) {
        if (!root_node->id.empty()) {
            reachable.insert(root_node->id);
        }
    } else if (const auto* root_by_id = ir.find_state_by_id(root)) {
        reachable.insert(root_by_id->name);
        reachable.insert(root_by_id->id);
    }

    while (!queue.empty()) {
        std::string curr = queue.front();
        queue.pop();

        const auto* curr_st = ir.find_state_by_name(curr);
        if (!curr_st) {
            curr_st = ir.find_state_by_id(curr);
        }

        // 1. Include sub-states: for sequential Composite states with an explicit initial_sub_state,
        // only automatically mark initial_sub_state as reachable; for Parallel or empty initial_sub_state, mark all
        // children.
        bool mark_all_children = (curr_st == nullptr) || (curr_st->kind == StateKind::Parallel) ||
                                 !curr_st->orthogonal_regions.empty() || curr_st->initial_sub_state.empty();
        if (mark_all_children) {
            for (const auto& s : ir.states) {
                bool matches_parent =
                    (!s.parent_state.empty() && s.parent_state == curr) ||
                    (s.parent_id.has_value() && *s.parent_id == curr) ||
                    (curr_st != nullptr && !s.parent_state.empty() && s.parent_state == curr_st->name);
                if (matches_parent && reachable.count(s.name) == 0) {
                    reachable.insert(s.name);
                    if (!s.id.empty()) {
                        reachable.insert(s.id);
                    }
                    queue.push(s.name);
                }
            }
        } else if (!curr_st->initial_sub_state.empty() && reachable.count(curr_st->initial_sub_state) == 0) {
            reachable.insert(curr_st->initial_sub_state);
            if (const auto* sub_node = ir.find_state_by_name(curr_st->initial_sub_state)) {
                if (!sub_node->id.empty()) {
                    reachable.insert(sub_node->id);
                }
            }
            queue.push(curr_st->initial_sub_state);
        }

        // 2. Include parent state if sub-state is reached
        if (curr_st != nullptr && !curr_st->parent_state.empty()) {
            if (reachable.count(curr_st->parent_state) == 0) {
                reachable.insert(curr_st->parent_state);
                if (curr_st->parent_id.has_value() && !curr_st->parent_id->empty()) {
                    reachable.insert(*curr_st->parent_id);
                }
                queue.push(curr_st->parent_state);
            }
        }

        // 3. Outgoing transitions (including multi-source/target fork transitions and parent-inherited transitions)
        auto push_target_ref = [&](const std::string& ref) {
            if (ref.empty()) {
                return;
            }
            if (const auto* tn = ir.find_state_by_id(ref)) {
                if (reachable.insert(tn->name).second) {
                    queue.push(tn->name);
                }
                if (!tn->id.empty()) {
                    reachable.insert(tn->id);
                }
            } else if (const auto* tn_name = ir.find_state_by_name(ref)) {
                if (reachable.insert(tn_name->name).second) {
                    queue.push(tn_name->name);
                }
                if (!tn_name->id.empty()) {
                    reachable.insert(tn_name->id);
                }
            } else if (reachable.insert(ref).second) {
                queue.push(ref);
            }
        };

        for (const auto& t : ir.transitions) {
            bool matches_source = (t.source == curr) || (!t.source_id.empty() && t.source_id == curr) ||
                                  (std::find(t.source_ids.begin(), t.source_ids.end(), curr) != t.source_ids.end());
            if (!matches_source && curr_st != nullptr) {
                matches_source =
                    (t.source == curr_st->name) || (!t.source_id.empty() && t.source_id == curr_st->id) ||
                    (std::find(t.source_ids.begin(), t.source_ids.end(), curr_st->name) != t.source_ids.end()) ||
                    (!curr_st->parent_state.empty() && t.source == curr_st->parent_state);
            }
            if (matches_source) {
                push_target_ref(t.target);
                push_target_ref(t.target_id);
                for (const auto& tid : t.target_ids) {
                    push_target_ref(tid);
                }
                for (const auto& tid : t.multi_target_ids) {
                    push_target_ref(tid);
                }
            }
        }
    }

    for (const auto& s : ir.states) {
        if (s.kind == StateKind::Final || ir.is_choice_node(s.name))
            continue;
        if (reachable.count(s.name) == 0 && reachable.count(s.id) == 0) {
            diag.report(Diagnostic::warning("W0201", "State unreachable from initial state: '" + s.name + "'."));
        }
    }

    // Helper lambda: check if a state or any of its ancestors/descendants has outgoing transitions to another state
    auto has_outgoing_hierarchical = [&](const StateNode& state) {
        auto has_non_self_out = [&](const std::string& st_name) {
            for (const auto& t : ir.transitions) {
                if (t.source == st_name && t.target != st_name) {
                    return true;
                }
            }
            return false;
        };

        if (has_non_self_out(state.name)) {
            return true;
        }
        // Check ancestors (cycle-guarded)
        std::unordered_set<std::string> visited_anc;
        std::string parent_name = state.parent_state;
        while (!parent_name.empty() && visited_anc.insert(parent_name).second) {
            if (has_non_self_out(parent_name)) {
                return true;
            }
            const auto* p = ir.find_state(parent_name);
            parent_name = (p != nullptr) ? p->parent_state : "";
        }
        // Check descendants (if composite, cycle-guarded)
        std::unordered_set<std::string> visited_desc;
        std::queue<std::string> child_q;
        visited_desc.insert(state.name);
        child_q.push(state.name);
        while (!child_q.empty()) {
            std::string c_curr = child_q.front();
            child_q.pop();
            for (const auto& child : ir.states) {
                if (child.parent_state == c_curr && visited_desc.insert(child.name).second) {
                    if (has_non_self_out(child.name)) {
                        return true;
                    }
                    child_q.push(child.name);
                }
            }
        }
        return false;
    };

    // Trap state (deadlock) analysis
    for (const auto& s : ir.states) {
        if (s.kind == StateKind::Final || ir.is_choice_node(s.name) || s.is_composite) {
            continue;
        }
        std::string lower_name = s.name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool is_intended_final =
            (lower_name == "end" || lower_name == "final" || lower_name == "stop" || lower_name == "terminate" ||
             lower_name == "completed" || lower_name == "done" || lower_name == "landed" ||
             lower_name.rfind("final", 0) == 0 || lower_name.rfind("end_", 0) == 0 ||
             (lower_name.size() >= 4 && lower_name.rfind("_end") == lower_name.size() - 4) ||
             lower_name.find("terminal") != std::string::npos);
        if (is_intended_final) {
            continue;
        }

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

// Pass: EFSMDataPathPass
// ============================================================================
EFSMDataPathPass::EFSMDataPathPass(bool prune_dead_transitions) : prune_dead_transitions_(prune_dead_transitions) {}

std::string EFSMDataPathPass::name() const {
    return "EFSMDataPath";
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
    auto findings = analyzer.analyze(diag);

    if (prune_dead_transitions_) {
        std::size_t removed_count = 0;
        for (const auto& f : findings) {
            if (f.kind != analysis::EFSMFindingKind::UnsatisfiableGuard) {
                continue;
            }
            auto it = std::find_if(ir.transitions.begin(), ir.transitions.end(), [&](const TransitionEdge& t) {
                if (!f.transition_id.empty() && t.id == f.transition_id)
                    return true;
                return t.source == f.source_state && t.target == f.target_state;
            });
            if (it != ir.transitions.end()) {
                ir.transitions.erase(it);
                removed_count++;
            }
        }
        if (removed_count > 0) {
            diag.report(Diagnostic::info(
                "I_EFSM_PRUNED_TRANSITIONS",
                "Pruned " + std::to_string(removed_count) + " dead transition(s) with unsatisfiable guard intervals."));
        }
    }

    return !diag.has_errors();
}

// ============================================================================

// ============================================================================
// PassManager: Pipeline Coordinator
// ============================================================================
PassManager::PassManager() = default;
PassManager::~PassManager() {
    passes_.clear();
}
PassManager::PassManager(PassManager&&) noexcept = default;
PassManager& PassManager::operator=(PassManager&& other) noexcept {
    if (this != &other) {
        // Destroy passes BEFORE unloading old plugin handles to prevent use-after-dlclose.
        passes_.clear();
        plugin_loader_ = std::move(other.plugin_loader_);
        passes_ = std::move(other.passes_);
        stats_ = std::move(other.stats_);
    }
    return *this;
}

std::size_t PassManager::pass_count() const noexcept {
    return passes_.size();
}

void PassManager::truncate_passes(std::size_t count) noexcept {
    if (count < passes_.size()) {
        passes_.resize(count);
    }
}

PassManager PassManager::create_default_pipeline() {
    PassManager pm;
    pm.add_pass(std::make_unique<HierarchyCanonicalizationPass>());
    pm.add_pass(std::make_unique<SemanticValidationPass>());
    pm.add_pass(std::make_unique<GuardSimplificationPass>());
    pm.add_pass(std::make_unique<DeterminismEnforcementPass>());
    pm.add_pass(std::make_unique<OrthogonalInterferencePass>());
    pm.add_pass(std::make_unique<ChoiceCompletenessPass>());
    pm.add_pass(std::make_unique<ConnectiveJunctionChainingPass>());
    pm.add_pass(std::make_unique<ChoiceInliningPass>());
    pm.add_pass(std::make_unique<SampledChangeTriggerPass>());
    pm.add_pass(std::make_unique<TimedDeadlockPass>());
    pm.add_pass(std::make_unique<LivelockAnalysisPass>());
    pm.add_pass(std::make_unique<EFSMDataPathPass>());
    pm.add_pass(std::make_unique<GuardSatisfiabilityPass>());
    pm.add_pass(std::make_unique<ModelSafetyVerifierPass>());
    pm.add_pass(std::make_unique<ModelCheckingPass>());
    return pm;
}

PassManager PassManager::create_optimizing_pipeline(bool prune_dead_states, bool minimize_states) {
    PassManager pm;
    pm.add_pass(std::make_unique<HierarchyCanonicalizationPass>());
    pm.add_pass(std::make_unique<SemanticValidationPass>());
    pm.add_pass(std::make_unique<GuardSimplificationPass>());
    pm.add_pass(std::make_unique<ConstantFoldingPass>());
    pm.add_pass(std::make_unique<DeterminismEnforcementPass>());
    pm.add_pass(std::make_unique<OrthogonalInterferencePass>());
    if (prune_dead_states) {
        pm.add_pass(std::make_unique<DeadStatePruningPass>(true));
    }
    if (minimize_states) {
        pm.add_pass(std::make_unique<StateMinimizationPass>());
    }
    pm.add_pass(std::make_unique<ChoiceCompletenessPass>());
    pm.add_pass(std::make_unique<ConnectiveJunctionChainingPass>());
    pm.add_pass(std::make_unique<ChoiceInliningPass>());
    pm.add_pass(std::make_unique<SampledChangeTriggerPass>());
    pm.add_pass(std::make_unique<TimedDeadlockPass>());
    pm.add_pass(std::make_unique<LivelockAnalysisPass>());
    pm.add_pass(std::make_unique<WcetAnalysisPass>());
    pm.add_pass(std::make_unique<EFSMDataPathPass>(true));
    pm.add_pass(std::make_unique<GuardSatisfiabilityPass>());
    pm.add_pass(std::make_unique<ModelSafetyVerifierPass>());
    pm.add_pass(std::make_unique<ModelCheckingPass>());
    return pm;
}

PassManager PassManager::create_verified_7stage_pipeline(bool optimize) {
    PassManager pm;
    // Stage 1: Frontend Canonicalization & Syntax Desugaring
    pm.add_pass(std::make_unique<HierarchyCanonicalizationPass>());
    pm.add_pass(std::make_unique<SemanticValidationPass>());
    pm.add_pass(std::make_unique<GuardSimplificationPass>());

    // Stage 2: Structural Lowering Suite
    pm.add_pass(std::make_unique<ForkJoinLoweringPass>());
    pm.add_pass(std::make_unique<HistoryLoweringPass>());
    pm.add_pass(std::make_unique<EventQueueBoundPass>());
    pm.add_pass(std::make_unique<OrthogonalProductPass>());
    pm.add_pass(std::make_unique<ConnectiveJunctionChainingPass>());
    pm.add_pass(std::make_unique<ChoiceInliningPass>());
    pm.add_pass(std::make_unique<BoundaryActionFusionPass>());
    pm.add_pass(std::make_unique<SampledChangeTriggerPass>());

    // Stage 3: Formal Invariant & Safety Verification
    pm.add_pass(std::make_unique<LivelockAnalysisPass>());
    pm.add_pass(std::make_unique<PriorityConflictPass>());
    pm.add_pass(std::make_unique<TimedInvariantsVerifierPass>());
    pm.add_pass(std::make_unique<GuardSatisfiabilityPass>());

    // Stage 4: Symbolic Model Checking
    pm.add_pass(std::make_unique<TimedDeadlockPass>());
    pm.add_pass(std::make_unique<ModelSafetyVerifierPass>());
    pm.add_pass(std::make_unique<ModelCheckingPass>());

    // Stage 5: Optimization & Minimization
    if (optimize) {
        pm.add_pass(std::make_unique<ConstantFoldingPass>());
        pm.add_pass(std::make_unique<DeadStatePruningPass>(true));
        pm.add_pass(std::make_unique<DeadActionEliminationPass>());
        pm.add_pass(std::make_unique<CommonActionFactoringPass>());
        pm.add_pass(std::make_unique<TransitionFusionPass>());
    }

    // Stage 6: Data-Path Optimization & Register Allocation
    pm.add_pass(std::make_unique<EFSMDataPathPass>(optimize));
    if (optimize) {
        pm.add_pass(std::make_unique<RegisterLivenessPass>());
    }

    // Stage 7: Backend Preparation & Code Emitter Handoff
    pm.add_pass(std::make_unique<DeterminismEnforcementPass>());
    pm.add_pass(std::make_unique<WcetAnalysisPass>());

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
        const auto invalidated = pass->invalidated_analyses();
        for (const auto& inv : invalidated) {
            executed_passes.erase(inv);
        }
        if (!invalidated.empty()) {
            analysis_manager_.invalidate(invalidated);
        } else if (ok && !pass->preserves_ir()) {
            analysis_manager_.invalidate_all();
        }
    }
    return !diag.has_errors();
}

const std::vector<PassExecutionStats>& PassManager::get_stats() const noexcept {
    return stats_;
}

PassManager PassManager::create_validation_pipeline() {
    PassManager pm;
    pm.add_pass(std::make_unique<SemanticValidationPass>());
    pm.add_pass(std::make_unique<ChoiceCompletenessPass>());
    pm.add_pass(std::make_unique<LivelockAnalysisPass>());
    pm.add_pass(std::make_unique<DeterminismEnforcementPass>());
    pm.add_pass(std::make_unique<ModelSafetyVerifierPass>());
    pm.add_pass(std::make_unique<TimedDeadlockPass>());
    return pm;
}

std::unique_ptr<IPass> PassManager::create_pass_by_name(std::string_view p_name, std::string_view pipe_cmd,
                                                        PassRegistry::SubmachineResolver submachine_resolver) {
    return PassRegistry::create(p_name, pipe_cmd, std::move(submachine_resolver));
}

std::vector<std::string_view> PassManager::registered_pass_names() {
    return PassRegistry::registered_names();
}

}  // namespace fsm::middleend

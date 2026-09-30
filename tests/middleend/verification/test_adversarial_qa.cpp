/**
 * @file test_adversarial_qa.cpp
 * @brief Adversarial, Chaos & Edge-Case QA Test Suite for fsmc.
 *
 * Covers:
 * 1. Reachability & Orphan Substate detection in hierarchical composite states.
 * 2. Trap / Deadlock detection avoiding substring collisions ('Pending', 'Suspended') and checking StateKind::Final.
 * 3. Synchronization between TriggerVariant and FsmValidator eventless/determinism analysis.
 * 4. Choice pseudostate stall prevention (single guarded branch without fallback).
 * 5. Semantic analysis enforcement on State entry_actions and exit_actions (InPort immutability).
 * 6. Reserved C++ keyword handling in models.
 */

#include <gtest/gtest.h>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/ir/action.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/fsm_validator.hpp"
#include "fsm/middleend/analysis/guard_satisfiability_pass.hpp"
#include "fsm/middleend/analysis/semantic_analyzer.hpp"
#include "fsm/middleend/passes/boundary_action_fusion_pass.hpp"

using namespace fsm;
using namespace fsm::ir;
using namespace fsm::middleend::analysis;
using namespace fsm::diagnostic;

// ============================================================================
// 1. Hierarchical Reachability: Orphan Sub-State Detection
// ============================================================================

TEST(AdversarialQA, HierarchicalOrphanSubState_DetectedAsUnreachable) {
    FsmIr ir;
    ir.name = "HierarchicalOrphanModel";
    ir.initial_state = "CompositeActive";

    StateNode comp("CompositeActive", "CompositeActive");
    comp.is_composite = true;
    comp.initial_sub_state = "SubRunning";

    StateNode sub1("SubRunning", "SubRunning");
    sub1.parent_state = "CompositeActive";

    StateNode orphan("SubOrphan", "SubOrphan");
    orphan.parent_state = "CompositeActive";
    // SubOrphan has NO incoming transition and is NOT the initial sub-state!

    ir.states.push_back(comp);
    ir.states.push_back(sub1);
    ir.states.push_back(orphan);

    ir.transitions.emplace_back("SubRunning", "SubRunning", "Tick");
    ir.transitions.emplace_back("SubOrphan", "SubRunning", "Recover");

    const auto validation = FsmValidator::validate(ir);

    // SubOrphan MUST be warned as unreachable
    bool found_orphan_warning = false;
    for (const auto& w : validation.warnings) {
        if (w.find("SubOrphan") != std::string::npos && w.find("unreachable") != std::string::npos) {
            found_orphan_warning = true;
            break;
        }
    }
    EXPECT_TRUE(found_orphan_warning) << "Validator failed to flag isolated sub-state SubOrphan inside composite state!";
}

// ============================================================================
// 2. Deadlock Detection: Heuristic Collision & StateKind::Final
// ============================================================================

TEST(AdversarialQA, TrapStateNamedPending_DetectedAsDeadlock) {
    FsmIr ir;
    ir.name = "PendingTrapModel";
    ir.initial_state = "Init";

    StateNode init("Init", "Init");
    // "Pending" contains "end" as substring, but is NOT a final state!
    StateNode pending("Pending", "Pending");

    ir.states.push_back(init);
    ir.states.push_back(pending);

    ir.transitions.emplace_back("Init", "Pending", "StartWork");

    const auto validation = FsmValidator::validate(ir);

    bool found_deadlock_warning = false;
    for (const auto& w : validation.warnings) {
        if (w.find("Pending") != std::string::npos && w.find("deadlock") != std::string::npos) {
            found_deadlock_warning = true;
            break;
        }
    }
    EXPECT_TRUE(found_deadlock_warning) << "Validator suppressed deadlock warning for 'Pending' due to 'end' substring match!";
}

TEST(AdversarialQA, StateKindFinalNamedSuccess_NotReportedAsDeadlock) {
    FsmIr ir;
    ir.name = "SuccessFinalModel";
    ir.initial_state = "Init";

    StateNode init("Init", "Init");
    StateNode success("Success", "Success");
    success.kind = StateKind::Final; // Explicitly marked as Final

    ir.states.push_back(init);
    ir.states.push_back(success);

    ir.transitions.emplace_back("Init", "Success", "Finish");

    const auto validation = FsmValidator::validate(ir);

    bool has_deadlock_on_success = false;
    for (const auto& w : validation.warnings) {
        if (w.find("Success") != std::string::npos && w.find("deadlock") != std::string::npos) {
            has_deadlock_on_success = true;
            break;
        }
    }
    EXPECT_FALSE(has_deadlock_on_success) << "Validator produced false positive deadlock warning on explicit StateKind::Final 'Success'!";
}

// ============================================================================
// 3. TriggerVariant vs FsmValidator Synchronization
// ============================================================================

TEST(AdversarialQA, DiscreteSignalCycleWithTriggerVariant_NotReportedAsLivelock) {
    FsmIr ir;
    ir.name = "SignalCycleModel";
    ir.initial_state = "StateA";

    StateNode sA("StateA", "StateA");
    StateNode sB("StateB", "StateB");
    ir.states.push_back(sA);
    ir.states.push_back(sB);

    // Transitions triggered by discrete signals via modern TriggerVariant
    ir.add_transition("StateA", "StateB", SignalTrigger("PING"));
    ir.add_transition("StateB", "StateA", SignalTrigger("PONG"));

    const auto validation = FsmValidator::validate(ir);

    bool found_livelock = false;
    for (const auto& diag : validation.diagnostics) {
        if (diag.category == "Livelock") {
            found_livelock = true;
            break;
        }
    }
    EXPECT_FALSE(found_livelock) << "Discrete signal cycle was falsely detected as an eventless livelock cycle!";
}

TEST(AdversarialQA, NonDeterministicConflict_DetectedWithTriggerVariant) {
    FsmIr ir;
    ir.name = "NonDetTriggerVariantModel";
    ir.initial_state = "Idle";

    StateNode idle("Idle", "Idle");
    StateNode active1("Active1", "Active1");
    StateNode active2("Active2", "Active2");
    ir.states.push_back(idle);
    ir.states.push_back(active1);
    ir.states.push_back(active2);

    // Two unconditional transitions on the same signal trigger
    ir.add_transition("Idle", "Active1", SignalTrigger("GO"));
    ir.add_transition("Idle", "Active2", SignalTrigger("GO"));

    const auto validation = FsmValidator::validate(ir);

    bool found_determinism_conflict = false;
    for (const auto& diag : validation.diagnostics) {
        if (diag.category == "Determinism") {
            found_determinism_conflict = true;
            break;
        }
    }
    EXPECT_TRUE(found_determinism_conflict) << "Validator failed to detect determinism conflict on TriggerVariant transitions!";
}

// ============================================================================
// 4. Choice Pseudostate: Single Guarded Branch Stall Prevention
// ============================================================================

TEST(AdversarialQA, ChoicePseudostate_SingleGuardedBranchWithoutElse_EmitsSafetyCritical) {
    FsmIr ir;
    ir.name = "ChoiceSingleBranchModel";
    ir.initial_state = "Init";

    StateNode init("Init", "Init");
    StateNode target("Target", "Target");
    ir.states.push_back(init);
    ir.states.push_back(target);

    ChoiceNodeModel c1;
    c1.name = "C1";
    ir.choice_nodes.push_back(c1);

    ir.transitions.emplace_back("Init", "C1", "Step");
    
    // Only 1 outgoing branch from choice, and it is guarded!
    TransitionEdge t_choice("C1", "Target", "");
    t_choice.guard = "velocity > 100";
    ir.transitions.push_back(t_choice);

    const auto validation = FsmValidator::validate(ir);

    bool found_choice_safety_critical = false;
    for (const auto& diag : validation.diagnostics) {
        if (diag.category == "Choice" && diag.severity == DiagnosticSeverity::SafetyCritical) {
            found_choice_safety_critical = true;
            break;
        }
    }
    EXPECT_TRUE(found_choice_safety_critical)
        << "Validator failed to report potential stall on Choice node with single guarded branch and no fallback!";
}

// ============================================================================
// 5. Semantic Analysis: Entry/Exit Action Immutability & Validation
// ============================================================================

TEST(AdversarialQA, EntryActionAssigningToReadOnlyInPort_RejectedBySemanticAnalyzer) {
    FsmIr ir;
    ir.name = "ReadOnlyInPortEntryActionModel";
    ir.initial_state = "Active";

    ir.ports.push_back(PortDefinition("sensor_input", PrimitiveTypeKind::Int32, PortDirection::In));

    StateNode active("Active", "Active");
    ActionSignature entry_act("InitSensors");
    entry_act.assignments.emplace_back("sensor_input", "42");
    active.entry_actions.push_back(entry_act);
    ir.states.push_back(active);

    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    bool ok = SemanticAnalyzer::validate(ir, errors, warnings);

    EXPECT_FALSE(ok) << "SemanticAnalyzer accepted illegal assignment to InPort inside state entry_action!";
    bool found_inport_error = false;
    for (const auto& err : errors) {
        if (err.find("Cannot assign to read-only InPort") != std::string::npos) {
            found_inport_error = true;
            break;
        }
    }
    EXPECT_TRUE(found_inport_error);
}

// ============================================================================
// 6. Cyclic Hierarchy: Ouroboros Detection & Pass Termination
// ============================================================================

TEST(AdversarialQA, CircularAncestry_DetectedByValidatorAndPassDoesNotHang) {
    FsmIr ir;
    ir.name = "OuroborosModel";
    ir.initial_state = "StateA";

    StateNode a("StateA", "StateA");
    a.is_composite = true;
    a.parent_state = "StateB"; // A child of B

    StateNode b("StateB", "StateB");
    b.is_composite = true;
    b.parent_state = "StateA"; // B child of A (Cycle!)

    ir.states.push_back(a);
    ir.states.push_back(b);

    ir.transitions.emplace_back("StateA", "StateB", "EV_STEP");

    const auto validation = FsmValidator::validate(ir);
    EXPECT_FALSE(validation.is_valid);
    bool found_hierarchy_error = false;
    for (const auto& err : validation.errors) {
        if (err.find("Cyclic parent-child state hierarchy detected") != std::string::npos) {
            found_hierarchy_error = true;
            break;
        }
    }
    EXPECT_TRUE(found_hierarchy_error) << "Validator failed to flag circular parent-child dependency!";

    // BoundaryActionFusionPass must terminate without hanging in an infinite loop
    DiagnosticEngine diag;
    fsm::middleend::passes::BoundaryActionFusionPass fusion_pass;
    EXPECT_NO_THROW(fusion_pass.run(ir, diag));
}

// ============================================================================
// 7. Impure Mutating Guards: Side-Effect Operator Proscription
// ============================================================================

TEST(AdversarialQA, ImpureMutatingGuard_RejectedBySemanticAnalyzer) {
    FsmIr ir;
    ir.name = "ImpureGuardModel";
    ir.initial_state = "Active";

    ir.variables.push_back(VariableDefinition("counter", PrimitiveTypeKind::Int32, "0"));

    StateNode active("Active", "Active");
    StateNode done("Done", "Done");
    ir.states.push_back(active);
    ir.states.push_back(done);

    // Guard with mutating side-effect (++)
    TransitionEdge t("Active", "Done", "STEP");
    t.guard = "reg.counter++ >= 3";
    ir.transitions.push_back(t);

    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    bool ok = SemanticAnalyzer::validate(ir, errors, warnings);

    EXPECT_FALSE(ok) << "SemanticAnalyzer accepted impure guard with mutating operator '++'!";
    bool found_mut_error = false;
    for (const auto& err : errors) {
        if (err.find("contains side-effect mutating operator '++'") != std::string::npos) {
            found_mut_error = true;
            break;
        }
    }
    EXPECT_TRUE(found_mut_error);
}

// ============================================================================
// 8. Boundary Race Condition: Ambiguous Singularity Overlap
// ============================================================================

TEST(AdversarialQA, BoundarySingularity_FlaggedByGuardSatisfiabilityPass) {
    FsmIr ir;
    ir.name = "BoundarySingularityModel";
    ir.initial_state = "Active";

    StateNode active("Active", "Active");
    StateNode safe("Safe", "Safe");
    StateNode fast("Fast", "Fast");
    ir.states.push_back(active);
    ir.states.push_back(safe);
    ir.states.push_back(fast);

    // T1: in.pressure <= 10.0
    TransitionEdge t1("Active", "Safe", "STOP");
    t1.guard = "in.pressure <= 10.0";
    t1.priority = 0;
    ir.transitions.push_back(t1);

    // T2: in.pressure >= 10.0 (Singularity at pressure == 10.0!)
    TransitionEdge t2("Active", "Fast", "STOP");
    t2.guard = "in.pressure >= 10.0";
    t2.priority = 0;
    ir.transitions.push_back(t2);

    DiagnosticEngine diag;
    GuardSatisfiabilityPass pass;
    pass.run(ir, diag);

    bool found_overlap_warning = false;
    for (const auto& d : diag.get_diagnostics()) {
        if (d.code == "W0301" && d.message.find("simultaneously satisfiable") != std::string::npos) {
            found_overlap_warning = true;
            break;
        }
    }
    EXPECT_TRUE(found_overlap_warning)
        << "GuardSatisfiabilityPass failed to flag ambiguous boundary overlap on pressure == 10.0!";
}


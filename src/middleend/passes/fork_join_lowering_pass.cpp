#include "fsm/middleend/passes/fork_join_lowering_pass.hpp"

#include <algorithm>
#include <vector>

namespace fsm::middleend::passes {

using namespace fsm::ir;
using namespace fsm::diagnostic;

bool ForkJoinLoweringPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    bool modified = false;

    std::vector<std::string> fork_nodes;
    std::vector<std::string> join_nodes;

    for (const auto& s : ir.states) {
        if (s.kind == StateKind::Fork) {
            fork_nodes.push_back(s.name);
        } else if (s.kind == StateKind::Join) {
            join_nodes.push_back(s.name);
        }
    }

    if (fork_nodes.empty() && join_nodes.empty()) {
        return false;
    }

    // 1. Lower Fork pseudostates
    for (const auto& fork_name : fork_nodes) {
        std::vector<TransitionEdge*> incoming;
        std::vector<TransitionEdge> outgoing;

        for (auto& t : ir.transitions) {
            if (t.target == fork_name) {
                incoming.push_back(&t);
            }
        }
        for (const auto& t : ir.transitions) {
            if (t.source == fork_name) {
                outgoing.push_back(t);
            }
        }

        if (incoming.empty() || outgoing.empty()) {
            continue;
        }

        // Collect all target states of the fork
        std::vector<std::string> target_states;
        std::vector<std::string> target_ids;
        std::vector<ActionAssignment> fork_branch_assignments;
        std::vector<ActionAstNode> fork_branch_instructions;
        std::vector<std::string> fork_branch_resets;

        for (const auto& out : outgoing) {
            target_states.push_back(out.target);
            target_ids.push_back(out.target_id.empty() ? compute_deterministic_id(out.target) : out.target_id);

            if (out.condition_action.has_value()) {
                for (const auto& a : out.condition_action->assignments) {
                    fork_branch_assignments.push_back(a);
                }
                for (const auto& inst : out.condition_action->instructions) {
                    fork_branch_instructions.push_back(inst);
                }
            }
            if (out.transition_action.has_value()) {
                for (const auto& a : out.transition_action->assignments) {
                    fork_branch_assignments.push_back(a);
                }
                for (const auto& inst : out.transition_action->instructions) {
                    fork_branch_instructions.push_back(inst);
                }
            }
            for (const auto& cr : out.clock_resets) {
                if (std::find(fork_branch_resets.begin(), fork_branch_resets.end(), cr) == fork_branch_resets.end()) {
                    fork_branch_resets.push_back(cr);
                }
            }
        }

        // Retarget each incoming transition to the multi-target fork endpoints
        for (auto* in : incoming) {
            in->target_ids = target_states;
            in->multi_target_ids = target_ids;
            if (!target_states.empty()) {
                in->target = target_states.front();
                in->target_id = target_ids.front();
            }

            if (!fork_branch_assignments.empty() || !fork_branch_instructions.empty()) {
                if (!in->transition_action.has_value()) {
                    in->transition_action = ActionSignature("ForkFusedAction");
                }
                for (const auto& a : fork_branch_assignments) {
                    in->transition_action->assignments.push_back(a);
                }
                for (const auto& inst : fork_branch_instructions) {
                    in->transition_action->instructions.push_back(inst);
                }
            }
            for (const auto& cr : fork_branch_resets) {
                if (std::find(in->clock_resets.begin(), in->clock_resets.end(), cr) == in->clock_resets.end()) {
                    in->clock_resets.push_back(cr);
                }
            }
            modified = true;
        }

        // Remove outgoing edges from the fork
        ir.transitions.erase(std::remove_if(ir.transitions.begin(), ir.transitions.end(),
                                            [&](const TransitionEdge& t) { return t.source == fork_name; }),
                             ir.transitions.end());
    }

    // 2. Lower Join pseudostates
    for (const auto& join_name : join_nodes) {
        std::vector<TransitionEdge> incoming;
        std::vector<TransitionEdge*> outgoing;

        for (const auto& t : ir.transitions) {
            if (t.target == join_name) {
                incoming.push_back(t);
            }
        }
        for (auto& t : ir.transitions) {
            if (t.source == join_name) {
                outgoing.push_back(&t);
            }
        }

        if (incoming.empty() || outgoing.empty()) {
            continue;
        }

        std::vector<std::string> source_states;
        std::vector<std::string> source_ids;
        std::vector<ActionAssignment> join_branch_assignments;
        std::vector<ActionAstNode> join_branch_instructions;
        std::vector<std::string> join_branch_resets;

        for (const auto& in : incoming) {
            source_states.push_back(in.source);
            source_ids.push_back(in.source_id.empty() ? compute_deterministic_id(in.source) : in.source_id);

            if (in.condition_action.has_value()) {
                for (const auto& a : in.condition_action->assignments) {
                    join_branch_assignments.push_back(a);
                }
                for (const auto& inst : in.condition_action->instructions) {
                    join_branch_instructions.push_back(inst);
                }
            }
            if (in.transition_action.has_value()) {
                for (const auto& a : in.transition_action->assignments) {
                    join_branch_assignments.push_back(a);
                }
                for (const auto& inst : in.transition_action->instructions) {
                    join_branch_instructions.push_back(inst);
                }
            }
            for (const auto& cr : in.clock_resets) {
                if (std::find(join_branch_resets.begin(), join_branch_resets.end(), cr) == join_branch_resets.end()) {
                    join_branch_resets.push_back(cr);
                }
            }
        }

        // Retarget each outgoing transition to have multi-source join endpoints
        for (auto* out : outgoing) {
            out->source_ids = source_states;
            out->multi_source_ids = source_ids;
            if (!source_states.empty()) {
                out->source = source_states.front();
                out->source_id = source_ids.front();
            }

            if (!join_branch_assignments.empty() || !join_branch_instructions.empty()) {
                ActionSignature fused("JoinFusedAction");
                fused.assignments = join_branch_assignments;
                fused.instructions = join_branch_instructions;
                if (out->transition_action.has_value()) {
                    for (const auto& a : out->transition_action->assignments) {
                        fused.assignments.push_back(a);
                    }
                    for (const auto& inst : out->transition_action->instructions) {
                        fused.instructions.push_back(inst);
                    }
                    if (!out->transition_action->name.empty()) {
                        fused.name = out->transition_action->name;
                    }
                    if (!out->transition_action->invocation.empty()) {
                        fused.invocation = out->transition_action->invocation;
                    }
                }
                out->transition_action = std::move(fused);
            }
            for (const auto& cr : join_branch_resets) {
                if (std::find(out->clock_resets.begin(), out->clock_resets.end(), cr) == out->clock_resets.end()) {
                    out->clock_resets.push_back(cr);
                }
            }
            modified = true;
        }

        // Remove incoming edges to the join
        ir.transitions.erase(std::remove_if(ir.transitions.begin(), ir.transitions.end(),
                                            [&](const TransitionEdge& t) { return t.target == join_name; }),
                             ir.transitions.end());
    }

    // 3. Remove Fork and Join state nodes from ir.states
    ir.states.erase(
        std::remove_if(ir.states.begin(), ir.states.end(),
                       [](const StateNode& s) { return s.kind == StateKind::Fork || s.kind == StateKind::Join; }),
        ir.states.end());

    diag.report(Diagnostic::info("I_FORK_JOIN_LOWERED",
                                 "Successfully lowered Fork and Join pseudostates into multi-endpoint transitions."));
    return modified;
}

}  // namespace fsm::middleend::passes

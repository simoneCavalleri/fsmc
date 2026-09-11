/**
 * @file transition_fusion_pass.cpp
 * @brief Implementation of TransitionFusionPass.
 */

#include "fsm/middleend/passes/transition_fusion_pass.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace fsm::middleend::passes {

namespace {

bool is_immediate_trigger(const ir::TriggerVariant& trigger) {
    if (std::holds_alternative<ir::AnonymousTrigger>(trigger)) {
        return true;
    }
    if (std::holds_alternative<ir::SignalTrigger>(trigger)) {
        return std::get<ir::SignalTrigger>(trigger).signal_name.empty();
    }
    return false;
}

std::optional<ir::GuardAstNode> compose_guard_asts(const std::optional<ir::GuardAstNode>& g1,
                                                   const std::optional<ir::GuardAstNode>& g2) {
    if (g1 && g2) {
        return ir::GuardAstNode(ir::GuardOp::And, {*g1, *g2});
    }
    if (g1)
        return g1;
    if (g2)
        return g2;
    return std::nullopt;
}

std::optional<std::string> compose_guard_strings(const std::optional<std::string>& g1,
                                                 const std::optional<std::string>& g2) {
    if (g1 && g2) {
        return "(" + *g1 + ") && (" + *g2 + ")";
    }
    if (g1)
        return g1;
    if (g2)
        return g2;
    return std::nullopt;
}

ir::ActionSignature compose_actions(const std::optional<ir::ActionSignature>& a1,
                                    const std::optional<ir::ActionSignature>& a2) {
    ir::ActionSignature res;
    if (a1) {
        res.instructions.insert(res.instructions.end(), a1->instructions.begin(), a1->instructions.end());
        res.assignments.insert(res.assignments.end(), a1->assignments.begin(), a1->assignments.end());
        if (!a1->name.empty()) {
            res.name = a1->name;
        }
    }
    if (a2) {
        res.instructions.insert(res.instructions.end(), a2->instructions.begin(), a2->instructions.end());
        res.assignments.insert(res.assignments.end(), a2->assignments.begin(), a2->assignments.end());
        if (!a2->name.empty()) {
            if (res.name.empty()) {
                res.name = a2->name;
            } else {
                res.name += "_" + a2->name;
            }
        }
    }
    return res;
}

ir::ActionSignature flatten_actions(const std::vector<ir::ActionSignature>& acts) {
    ir::ActionSignature res;
    for (const auto& a : acts) {
        res = compose_actions(res, a);
    }
    return res;
}

}  // namespace

bool TransitionFusionPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    bool overall_fused = false;
    bool iterate = true;

    while (iterate) {
        iterate = false;

        for (auto state_it = ir.states.begin(); state_it != ir.states.end(); ++state_it) {
            const auto& state = *state_it;

            // Cannot fuse initial state, terminate state, or state with children
            if (state.id == ir.initial_state_id || state.name == ir.initial_state_id || !state.children_ids.empty() ||
                state.kind == ir::StateKind::Terminate || !state.invariants.empty()) {
                continue;
            }

            // Find all incoming and outgoing transitions
            std::vector<std::size_t> incoming_indices;
            std::vector<std::size_t> outgoing_indices;

            for (std::size_t i = 0; i < ir.transitions.size(); ++i) {
                const auto& edge = ir.transitions[i];
                if (edge.target_id == state.id || edge.target == state.name || edge.target_id == state.name) {
                    incoming_indices.push_back(i);
                }
                if (edge.source_id == state.id || edge.source == state.name || edge.source_id == state.name) {
                    outgoing_indices.push_back(i);
                }
            }

            // Candidate state must have at least 1 incoming transition and exactly 1 outgoing transition
            // where the outgoing transition is immediate (NoTrigger)
            if (incoming_indices.empty() || outgoing_indices.size() != 1) {
                continue;
            }

            const auto& out_edge = ir.transitions[outgoing_indices[0]];
            if (!is_immediate_trigger(out_edge.trigger)) {
                continue;
            }

            // Avoid self-loops on candidate state
            if (out_edge.target_id == state.id || out_edge.target == state.name || out_edge.target_id == state.name) {
                continue;
            }

            std::string mid_state_id = state.id;
            auto mid_entry = flatten_actions(state.entry_actions);
            auto mid_exit = flatten_actions(state.exit_actions);

            // Fuse each incoming transition with the outgoing transition
            std::vector<ir::TransitionEdge> new_fused_edges;
            new_fused_edges.reserve(incoming_indices.size());

            for (std::size_t in_idx : incoming_indices) {
                const auto& in_edge = ir.transitions[in_idx];

                ir::TransitionEdge fused;
                fused.source_id = in_edge.source_id;
                fused.source = in_edge.source;
                fused.target_id = out_edge.target_id;
                fused.target = out_edge.target;
                fused.trigger = in_edge.trigger;
                fused.event = in_edge.event;
                fused.guard = compose_guard_strings(in_edge.guard, out_edge.guard);
                fused.guard_ast = compose_guard_asts(in_edge.guard_ast, out_edge.guard_ast);

                // Chain actions: in_action + mid_entry + mid_exit + out_action
                auto act1 = compose_actions(in_edge.transition_action,
                                            mid_entry.empty() ? std::nullopt : std::make_optional(mid_entry));
                auto act2 = compose_actions(mid_exit.empty() ? std::nullopt : std::make_optional(mid_exit),
                                            out_edge.transition_action);
                auto fused_act = compose_actions(act1, act2);
                if (!fused_act.empty()) {
                    fused.transition_action = std::move(fused_act);
                }

                // Merge clock resets
                fused.clock_resets = in_edge.clock_resets;
                for (const auto& cr : out_edge.clock_resets) {
                    if (std::find(fused.clock_resets.begin(), fused.clock_resets.end(), cr) ==
                        fused.clock_resets.end()) {
                        fused.clock_resets.push_back(cr);
                    }
                }

                new_fused_edges.push_back(std::move(fused));
            }

            // Remove old edges (both incoming and outgoing)
            std::vector<std::size_t> to_remove = incoming_indices;
            to_remove.push_back(outgoing_indices[0]);
            std::sort(to_remove.rbegin(), to_remove.rend());

            for (std::size_t idx : to_remove) {
                ir.transitions.erase(ir.transitions.begin() + static_cast<std::ptrdiff_t>(idx));
            }

            // Insert new fused transitions
            for (auto& fe : new_fused_edges) {
                ir.add_transition(std::move(fe));
            }

            // Remove mid state
            ir.states.erase(state_it);

            diag.report(diagnostic::Diagnostic::info(
                "TransitionFusion", "Fused transient state '" + mid_state_id + "' into direct transition(s)."));
            overall_fused = true;
            iterate = true;
            break;  // Restart loop after modifying ir.states
        }
    }

    if (overall_fused) {
        ir.canonicalize();
    }

    return true;
}

}  // namespace fsm::middleend::passes

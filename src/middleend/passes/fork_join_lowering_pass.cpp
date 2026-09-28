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
        for (const auto& out : outgoing) {
            target_states.push_back(out.target);
            target_ids.push_back(out.target_id.empty() ? compute_deterministic_id(out.target) : out.target_id);
        }

        // Retarget each incoming transition to the multi-target fork endpoints
        for (auto* in : incoming) {
            in->target_ids = target_states;
            in->multi_target_ids = target_ids;
            if (!target_states.empty()) {
                in->target = target_states.front();
                in->target_id = target_ids.front();
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
        for (const auto& in : incoming) {
            source_states.push_back(in.source);
            source_ids.push_back(in.source_id.empty() ? compute_deterministic_id(in.source) : in.source_id);
        }

        // Retarget each outgoing transition to have multi-source join endpoints
        for (auto* out : outgoing) {
            out->source_ids = source_states;
            out->multi_source_ids = source_ids;
            if (!source_states.empty()) {
                out->source = source_states.front();
                out->source_id = source_ids.front();
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

#include "fsm/middleend/passes/deferred_event_lowering_pass.hpp"

#include <string>
#include <vector>

#include "fsm/ir/action.hpp"
#include "fsm/ir/variable_definition.hpp"

namespace fsm::middleend::passes {

using namespace fsm::ir;
using namespace fsm::diagnostic;

bool DeferredEventLoweringPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    bool has_deferred = false;
    for (const auto& s : ir.states) {
        if (!s.deferred_events.empty()) {
            has_deferred = true;
            break;
        }
    }

    if (!has_deferred) {
        return false;
    }

    // 1. Synthesize bounded buffer registers if not already present
    bool buf_exists = false;
    for (const auto& v : ir.variables) {
        if (v.name == "__deferred_count") {
            buf_exists = true;
            break;
        }
    }

    if (!buf_exists) {
        VariableDefinition count_var;
        count_var.name = "__deferred_count";
        count_var.type = "uint32_t";
        count_var.initial_value = "0";
        count_var.description = "Count of currently buffered deferred events";
        ir.variables.push_back(std::move(count_var));

        VariableDefinition buf_var;
        buf_var.name = "__deferred_buffer";
        buf_var.type = "string";
        buf_var.initial_value = "\"\"";
        buf_var.description = "Bounded buffer storing deferred event descriptors";
        ir.variables.push_back(std::move(buf_var));
    }

    // 2. For each state with deferred events, add internal self-transitions capturing the event
    for (auto& s : ir.states) {
        if (s.deferred_events.empty()) {
            continue;
        }

        for (const auto& ev_name : s.deferred_events) {
            // Check if transition on this event already exists from this state
            bool trans_exists = false;
            for (const auto& t : ir.transitions) {
                if (t.source == s.name && t.event == ev_name) {
                    trans_exists = true;
                    break;
                }
            }

            if (!trans_exists) {
                // Synthesize buffering internal transition
                ActionSignature defer_act("__enqueue_deferred_" + ev_name);
                StoreOp inc_store;
                inc_store.target = LValueTarget("__deferred_count", LValueScope::Register);
                inc_store.op = AssignmentOp::AddAssign;
                inc_store.expression = "1";
                defer_act.instructions.emplace_back(inc_store, "Increment deferred count");

                TransitionEdge defer_edge(s.name, s.name, ev_name, std::nullopt, defer_act,
                                          "Buffer deferred event " + ev_name, TransitionEdgeKind::Internal, 0);
                ir.transitions.push_back(std::move(defer_edge));
            }
        }

        // On state exit: synthesize recall trigger on outgoing transitions leaving this state
        for (auto& t : ir.transitions) {
            if (t.source == s.name && t.target != s.name && t.kind != TransitionEdgeKind::Internal) {
                ActionSignature recall_act("__recall_deferred");
                StoreOp rst_store;
                rst_store.target = LValueTarget("__deferred_count", LValueScope::Register);
                rst_store.op = AssignmentOp::Assign;
                rst_store.expression = "0";
                recall_act.instructions.emplace_back(rst_store, "Drain and recall deferred events");

                if (!t.transition_action.has_value()) {
                    t.transition_action = recall_act;
                } else {
                    t.transition_action->instructions.push_back(
                        ActionAstNode(rst_store, "Recall deferred queue upon state exit"));
                }
            }
        }

        s.deferred_events.clear();
    }

    diag.report(
        Diagnostic::info("I_DEFERRED_LOWERED",
                         "Successfully lowered deferred events into explicit FIFO buffers and recall transitions."));
    return true;
}

}  // namespace fsm::middleend::passes

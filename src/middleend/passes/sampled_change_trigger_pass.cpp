#include "fsm/middleend/passes/sampled_change_trigger_pass.hpp"

#include <string>

namespace fsm::middleend::passes {

using namespace fsm::ir;
using namespace fsm::diagnostic;

bool SampledChangeTriggerPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    bool modified = false;
    std::size_t lowered_count = 0;

    for (std::size_t i = 0; i < ir.transitions.size(); ++i) {
        auto& t = ir.transitions[i];
        if (!std::holds_alternative<ChangeTrigger>(t.trigger)) {
            continue;
        }

        const auto& ct = std::get<ChangeTrigger>(t.trigger);
        std::string raw_expr = ct.raw_expression;
        if (raw_expr.empty()) {
            t.trigger = AnonymousTrigger{};
            modified = true;
            continue;
        }

        // 1. Create a unique shadow register variable to track previous predicate state
        std::string shadow_var = "__change_" + t.source + "_" + std::to_string(i) + "_prev";
        if (ir.find_variable(shadow_var) == nullptr) {
            VariableDefinition var_def(shadow_var, DataType::boolean(), "false");
            var_def.description = "Synthesized shadow register tracking previous state of '" + raw_expr + "'";
            ir.variables.push_back(var_def);
        }

        // 2. Add entry action to source state so shadow register captures initial state upon entry
        StateNode* src_node = ir.find_state_by_name(t.source);
        if (!src_node) {
            src_node = ir.find_state_by_id(t.source);
        }
        if (src_node) {
            ActionAssignment entry_assign;
            entry_assign.target = shadow_var;
            entry_assign.expression = raw_expr;
            ActionSignature entry_act("Init_" + shadow_var);
            entry_act.assignments.push_back(entry_assign);
            src_node->entry_actions.push_back(entry_act);
        }

        // 3. Synthesize edge-detection guard: (!prev && curr) for rising edge
        std::string edge_guard;
        if (ct.active_on_true) {
            edge_guard = "(!" + shadow_var + " && (" + raw_expr + "))";
        } else {
            edge_guard = "(" + shadow_var + " && !(" + raw_expr + "))";
        }

        if (t.guard.has_value() && !t.guard->empty() && *t.guard != "true") {
            t.set_guard("(" + edge_guard + " && (" + *t.guard + "))");
        } else {
            t.set_guard(edge_guard);
        }

        // 4. Synthesize or update action to record the change
        std::string act_name = "Update_" + shadow_var;
        ActionAssignment assign;
        assign.target = shadow_var;
        assign.expression = raw_expr;

        ActionSignature new_act(act_name);
        new_act.assignments.push_back(assign);

        if (t.transition_action.has_value() && !t.transition_action->name.empty()) {
            t.transition_action->assignments.push_back(assign);
        } else {
            t.transition_action = new_act;
            ir.actions.push_back(ActionModel(act_name, "Update shadow register for change trigger"));
        }

        // 5. Lower trigger to anonymous sampled transition
        t.trigger = AnonymousTrigger{};
        t.event = "";
        lowered_count++;
        modified = true;
    }

    if (lowered_count > 0) {
        diag.report(Diagnostic::info(
            "I_SAMPLED_CHANGE_TRIGGER",
            "Lowered " + std::to_string(lowered_count) + " change trigger(s) into sampled edge-detector transitions."));
    }

    return modified;
}

}  // namespace fsm::middleend::passes

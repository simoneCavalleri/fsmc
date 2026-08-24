#pragma once

#include <sstream>
#include <string>
#include <vector>

#include "fsm/frontend/guard_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

class Sysml2Serializer {
  public:
    static std::string serialize(const FsmIr& model) {
        std::ostringstream out;
        std::string model_name = model.name.empty() ? "GeneratedStateMachine" : model.name;
        out << "state def " << model_name << " {\n";

        // Initial state
        if (!model.initial_state.empty()) {
            out << "    initial state " << model.initial_state << ";\n\n";
        }

        // Emit top-level states
        for (const auto& state : model.states) {
            if (state.parent_state.empty()) {
                emit_state(out, state, model, 1);
            }
        }

        // Emit transitions
        out << "\n";
        for (const auto& trans : model.transitions) {
            std::string clean_target = trans.target;
            if (trans.target_is_history) {
                clean_target += trans.target_is_deep_history ? "[H*]" : "[H]";
            }
            out << "    transition from " << trans.source;
            if (!trans.event.empty()) {
                out << " accept " << trans.event;
            }
            if (trans.guard && !trans.guard->empty()) {
                out << " if " << GuardExpressionParser::to_diagram_string(*trans.guard);
            }
            if (trans.action && !trans.action->empty()) {
                out << " do " << *trans.action;
            }
            out << " then " << clean_target << ";\n";
        }

        out << "}\n";
        return out.str();
    }

  private:
    static void emit_state(std::ostream& out, const StateNode& state, const FsmIr& model, size_t indent) {
        std::string pad(indent * 4, ' ');
        if (state.is_composite) {
            out << pad << "state " << state.name << " {\n";
            if (!state.initial_sub_state.empty()) {
                out << pad << "    initial state " << state.initial_sub_state << ";\n";
            }
            for (const auto& d_evt : state.deferred_events) {
                out << pad << "    defer " << d_evt << ";\n";
            }
            for (const auto& child : model.states) {
                if (child.parent_state == state.name) {
                    emit_state(out, child, model, indent + 1);
                }
            }
            out << pad << "}\n";
        } else {
            if (!state.deferred_events.empty()) {
                out << pad << "state " << state.name << " {\n";
                for (const auto& d_evt : state.deferred_events) {
                    out << pad << "    defer " << d_evt << ";\n";
                }
                out << pad << "}\n";
            } else {
                out << pad << "state " << state.name << ";\n";
            }
        }
    }
};

}  // namespace fsm::codegen

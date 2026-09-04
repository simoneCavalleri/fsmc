#pragma once

#include <sstream>
#include <string>
#include <vector>

#include "fsm/frontend/directive/guard_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

class Sysml2Serializer {
  public:
    static std::string serialize(const FsmIr& model) {
        std::ostringstream out;
        std::string model_name = model.name.empty() ? "GeneratedStateMachine" : model.name;
        out << "state def " << model_name << " {\n";

        // Native SysML v2 Enumerations
        for (const auto& en : model.enums) {
            out << "    enum def " << en.name;
            if (!en.underlying_type.empty() && en.underlying_type != "uint8_t") {
                out << " :> " << en.underlying_type;
            }
            out << " {\n";
            for (const auto& lit : en.literals) {
                out << "        enum " << lit.name;
                if (lit.value.has_value()) {
                    out << " = " << *lit.value;
                }
                out << ";\n";
            }
            out << "    }\n";
        }
        if (!model.enums.empty()) {
            out << "\n";
        }

        // Native SysML v2 Structs & Datatypes
        for (const auto& st : model.structs) {
            std::string def_keyword = st.is_datatype ? "datatype def " : "struct def ";
            out << "    " << def_keyword << st.name << " {\n";
            for (const auto& f : st.fields) {
                out << "        attribute " << f.name << " : " << map_cpp_type_to_sysml(f.type);
                if (!f.default_value.empty()) {
                    out << " = " << f.default_value;
                }
                out << ";\n";
            }
            out << "    }\n";
        }
        if (!model.structs.empty()) {
            out << "\n";
        }

        // Native SysML v2 Typed Ports (InPorts / OutPorts / InOutPorts)
        for (const auto& port : model.ports) {
            std::string dir_str =
                port.is_out() ? "out port " : (port.direction == PortDirection::InOut ? "inout port " : "in port ");
            out << "    " << dir_str << port.name << " : " << map_cpp_type_to_sysml(port.type);
            if (!port.constraint.empty()) {
                out << " { assert constraint { " << port.constraint << " } }";
            } else if (port.min_value.has_value() && port.max_value.has_value()) {
                out << " { assert constraint { self >= " << *port.min_value << " and self <= " << *port.max_value
                    << " } }";
            }
            out << ";\n";
        }
        if (!model.ports.empty()) {
            out << "\n";
        }

        // Native SysML v2 EFSM Variables
        for (const auto& var : model.variables) {
            out << "    attribute " << var.name << " : " << map_cpp_type_to_sysml(var.type);
            if (!var.initial_value.empty()) {
                out << " = " << var.initial_value;
            }
            out << ";\n";
        }
        if (!model.variables.empty()) {
            out << "\n";
        }

        // Native SysML v2 Signals & Item Definitions
        for (const auto& sig : model.signals) {
            if (sig.attributes.empty()) {
                out << "    event def " << sig.name << ";\n";
            } else {
                out << "    item def " << sig.name << " {\n";
                for (const auto& attr : sig.attributes) {
                    out << "        attribute " << attr.name << " : " << map_cpp_type_to_sysml(attr.type);
                    if (!attr.default_value.empty()) {
                        out << " = " << attr.default_value;
                    }
                    out << ";\n";
                }
                out << "    }\n";
            }
        }
        if (!model.signals.empty()) {
            out << "\n";
        }

        // Initial state
        if (!model.initial_state.empty()) {
            out << "    entry; then " << model.initial_state << ";\n\n";
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
            out << "    transition\n";
            if (trans.priority > 0) {
                out << "        priority " << trans.priority << "\n";
            }
            out << "        first " << trans.source << "\n";
            if (!trans.event.empty()) {
                out << "        accept " << trans.event << "\n";
            }
            if (trans.guard && !trans.guard->empty()) {
                out << "        if " << GuardExpressionParser::to_diagram_string(*trans.guard) << "\n";
            }
            if (trans.action && !trans.action->empty()) {
                out << "        do " << *trans.action << "\n";
            }
            out << "        then " << clean_target << ";\n";
        }

        out << "}\n";
        return out.str();
    }

  private:
    static std::string map_cpp_type_to_sysml(std::string_view cpp_type) {
        if (cpp_type == "uint32_t" || cpp_type == "int" || cpp_type == "uint64_t" || cpp_type == "int32_t") {
            return "Integer";
        }
        if (cpp_type == "float" || cpp_type == "double") {
            return "Real";
        }
        if (cpp_type == "bool") {
            return "Boolean";
        }
        if (cpp_type == "std::string" || cpp_type == "string") {
            return "String";
        }
        return std::string{cpp_type};
    }

    static void emit_state(std::ostream& out, const StateNode& state, const FsmIr& model, size_t indent) {
        std::string pad(indent * 4, ' ');

        if (state.kind == StateKind::EntryPoint) {
            out << pad << "entry point " << state.name << ";\n";
            return;
        }
        if (state.kind == StateKind::ExitPoint) {
            out << pad << "exit point " << state.name << ";\n";
            return;
        }

        bool has_body = !state.traceability_reqs.empty() || !state.entry_actions.empty() ||
                        (state.do_activity.has_value() && !state.do_activity->empty()) || !state.exit_actions.empty() ||
                        !state.deferred_events.empty() || !state.initial_sub_state.empty() ||
                        (state.time_invariant.has_value() && !state.time_invariant->empty());

        bool has_children = false;
        for (const auto& child : model.states) {
            if (child.parent_state == state.name) {
                has_children = true;
                break;
            }
        }

        if (!has_body && !has_children) {
            out << pad << "state " << state.name << ";\n";
            return;
        }

        out << pad << "state " << state.name << " {\n";

        // Invariant (stay duration)
        if (state.time_invariant.has_value() && !state.time_invariant->empty()) {
            out << pad << "    stay duration <= " << *state.time_invariant << ";\n";
        }

        // Requirements
        for (const auto& req : state.traceability_reqs) {
            out << pad << "    satisfy requirement " << req << ";\n";
        }

        // Entry Actions
        for (const auto& act : state.entry_actions) {
            out << pad << "    entry action " << act.name << ";\n";
        }

        // Do Activity
        if (state.do_activity.has_value() && !state.do_activity->empty()) {
            out << pad << "    do action " << *state.do_activity << ";\n";
        }

        // Exit Actions
        for (const auto& act : state.exit_actions) {
            out << pad << "    exit action " << act.name << ";\n";
        }

        // Deferred Events
        for (const auto& d_evt : state.deferred_events) {
            out << pad << "    defer " << d_evt << ";\n";
        }

        // Initial substate
        if (!state.initial_sub_state.empty()) {
            out << pad << "    entry; then " << state.initial_sub_state << ";\n";
        }

        // Nested children
        for (const auto& child : model.states) {
            if (child.parent_state == state.name) {
                emit_state(out, child, model, indent + 1);
            }
        }

        out << pad << "}\n";
    }
};

}  // namespace fsm::codegen

#include "fsm/backend/formal/sysml2_serializer.hpp"

#include <set>
#include <sstream>

#include "fsm/ir/guard.hpp"

namespace fsm::backend::formal {

using namespace fsm::ir;

namespace {

void emit_state(std::ostream& out, const StateNode& state, const FsmIr& model, size_t indent) {
    std::string pad(indent * 4, ' ');

    if (state.kind == StateKind::EntryPoint) {
        out << pad << "entry point " << state.name << ";\n";
        return;
    }
    if (state.kind == StateKind::ExitPoint) {
        out << pad << "exit point " << state.name << ";\n";
        return;
    }
    if (state.kind == StateKind::Fork) {
        out << pad << "fork " << state.name << ";\n";
        return;
    }
    if (state.kind == StateKind::Join) {
        out << pad << "join " << state.name << ";\n";
        return;
    }
    if (state.kind == StateKind::Choice) {
        out << pad << "choice " << state.name << ";\n";
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

    std::string state_kw = (state.kind == StateKind::Parallel) ? "parallel state " : "state ";

    if (!has_body && !has_children) {
        out << pad << state_kw << state.name << ";\n";
        return;
    }

    out << pad << state_kw << state.name << " {\n";

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
        if (!act.assignments.empty()) {
            out << pad << "    entry action { " << fsm::ir::format_action_expression(act) << "; }\n";
        } else {
            out << pad << "    entry action " << act.name << ";\n";
        }
    }

    // Do Activity
    if (state.do_activity.has_value() && !state.do_activity->empty()) {
        out << pad << "    do action " << *state.do_activity << ";\n";
    }

    // Exit Actions
    for (const auto& act : state.exit_actions) {
        if (!act.assignments.empty()) {
            out << pad << "    exit action { " << fsm::ir::format_action_expression(act) << "; }\n";
        } else {
            out << pad << "    exit action " << act.name << ";\n";
        }
    }

    // Deferred Events
    for (const auto& d_evt : state.deferred_events) {
        out << pad << "    defer " << d_evt << ";\n";
    }

    // Initial substate (only for exclusive/OR states, not parallel orthogonal states)
    if (!state.initial_sub_state.empty() && state.kind != StateKind::Parallel) {
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

}  // namespace

std::string Sysml2Serializer::serialize(const FsmIr& model) {
    std::ostringstream out;
    std::string model_name = model.name.empty() ? "GeneratedStateMachine" : model.name;
    out << "state def " << model_name << " {\n";

    // Native SysML v2 Enumerations
    bool has_enums = false;
    for (const auto& en : model.custom_types) {
        if (!en.is_enum())
            continue;
        has_enums = true;
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
    if (has_enums) {
        out << "\n";
    }

    // Native SysML v2 Structs & Datatypes
    bool has_structs = false;
    for (const auto& st : model.custom_types) {
        if (!st.is_struct())
            continue;
        has_structs = true;
        std::string def_keyword = st.is_datatype ? "datatype def " : "struct def ";
        out << "    " << def_keyword << st.name << " {\n";
        for (const auto& f : st.fields) {
            out << "        attribute " << f.name << " : " << f.type.to_sysml_type();
            if (!f.default_value.empty()) {
                out << " = " << f.default_value;
            }
            out << ";\n";
        }
        out << "    }\n";
    }
    if (has_structs) {
        out << "\n";
    }

    // Native SysML v2 Typed Ports (InPorts / OutPorts / InOutPorts)
    for (const auto& port : model.ports) {
        std::string dir_str =
            port.is_out() ? "out port " : (port.direction == PortDirection::InOut ? "inout port " : "in port ");
        out << "    " << dir_str << port.name << " : " << port.type.to_sysml_type();
        if (!port.constraint.empty()) {
            out << " { assert constraint { " << port.constraint << " } }";
        } else if (port.min_value.has_value() && port.max_value.has_value()) {
            out << " { assert constraint { self >= " << *port.min_value << " and self <= " << *port.max_value << " } }";
        }
        out << ";\n";
    }
    if (!model.ports.empty()) {
        out << "\n";
    }

    // Native SysML v2 EFSM Variables
    for (const auto& var : model.variables) {
        out << "    attribute " << var.name << " : " << var.type.to_sysml_type();
        if (!var.initial_value.empty()) {
            out << " = " << var.initial_value;
        }
        out << ";\n";
    }
    if (!model.variables.empty()) {
        out << "\n";
    }

    // Collect synthetic event names used solely by TimeTriggers so we don't emit redundant event defs
    std::set<std::string> time_trigger_events;
    std::set<std::string> non_time_events;
    for (const auto& trans : model.transitions) {
        if (std::holds_alternative<ir::TimeTrigger>(trans.trigger)) {
            if (!trans.event.empty()) {
                time_trigger_events.insert(trans.event);
            }
        } else if (!trans.event.empty()) {
            non_time_events.insert(trans.event);
        }
    }

    // Native SysML v2 Signals & Item Definitions
    for (const auto& sig : model.signals) {
        if (sig.attributes.empty() && time_trigger_events.count(sig.name) > 0 && non_time_events.count(sig.name) == 0) {
            continue;
        }
        if (sig.attributes.empty() && sig.validators.empty()) {
            out << "    event def " << sig.name << ";\n";
        } else {
            out << "    item def " << sig.name << " {\n";
            for (const auto& attr : sig.attributes) {
                out << "        attribute " << attr.name << " : " << attr.type.to_sysml_type();
                if (!attr.default_value.empty()) {
                    out << " = " << attr.default_value;
                }
                out << ";\n";
            }
            for (const auto& val : sig.validators) {
                out << "        assert constraint { " << val << " }\n";
            }
            out << "    }\n";
        }
    }
    if (!model.signals.empty()) {
        out << "\n";
    }

    // Native SysML v2 Temporal Logic Properties
    for (const auto& prop : model.properties) {
        if (!prop.name.empty() && !prop.raw_formula.empty()) {
            if (prop.kind != PropertyKind::Safety || !prop.traceability_req.empty() || !prop.description.empty()) {
                out << "    // @fsm:property name=" << prop.name << " kind=" << property_kind_to_string(prop.kind)
                    << " formula=\"" << prop.raw_formula << "\"";
                if (!prop.traceability_req.empty()) {
                    out << " req=\"" << prop.traceability_req << "\"";
                }
                if (!prop.description.empty()) {
                    out << " desc=\"" << prop.description << "\"";
                }
                out << "\n";
            }
            out << "    assert property " << prop.name << " : " << prop.raw_formula << ";\n";
        }
    }
    if (!model.properties.empty()) {
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

    // Emit choice pseudostates not already present in states
    for (const auto& choice : model.choice_nodes) {
        if (model.find_state(choice.name) == nullptr) {
            out << "    choice " << choice.name << ";\n";
        }
    }

    // Emit transitions
    out << "\n";
    for (const auto& trans : model.transitions) {
        std::string clean_target = trans.target;
        if (trans.target_is_history) {
            clean_target += trans.target_is_deep_history ? "[H*]" : "[H]";
        }
        out << (trans.kind == TransitionEdgeKind::Local ? "    local transition\n" : "    transition\n");
        if (trans.priority > 0) {
            out << "        priority " << trans.priority << "\n";
        }
        out << "        first " << trans.source << "\n";
        if (const auto* tt = std::get_if<ir::TimeTrigger>(&trans.trigger)) {
            const std::string_view unit_str = ir::time_unit_to_string(tt->unit);
            if (tt->kind == ir::TimeTriggerKind::Every) {
                out << "        accept every(" << tt->duration_value << unit_str << ")\n";
            } else if (tt->kind == ir::TimeTriggerKind::At) {
                out << "        accept at(" << tt->duration_value << unit_str << ")\n";
            } else {
                out << "        accept after(" << tt->duration_value << unit_str << ")\n";
            }
        } else if (const auto* ct = std::get_if<ir::ChangeTrigger>(&trans.trigger)) {
            if (!ct->raw_expression.empty()) {
                out << "        accept when (" << ct->raw_expression << ")\n";
            }
        } else if (!trans.event.empty()) {
            out << "        accept " << trans.event << "\n";
        }
        if (trans.guard && !trans.guard->empty()) {
            out << "        if " << ir::GuardExpressionParser::to_diagram_string(*trans.guard, model.guards) << "\n";
        }
        if (trans.transition_action.has_value() && !trans.transition_action->assignments.empty()) {
            out << "        do action { " << fsm::ir::format_action_expression(*trans.transition_action) << "; }\n";
        } else {
            std::string act = trans.get_action();
            if (!act.empty()) {
                out << "        do " << act << "\n";
            }
        }
        if (trans.kind == TransitionEdgeKind::Internal && trans.source == trans.target && !trans.event.empty()) {
            out << "        ;\n";
        } else {
            out << "        then " << clean_target << ";\n";
        }
    }

    out << "}\n";
    return out.str();
}

}  // namespace fsm::backend::formal

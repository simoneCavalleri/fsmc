#include "fsm/backend/formal/sysml2_serializer.hpp"

#include <set>
#include <sstream>

#include "fsm/ir/guard.hpp"

namespace fsm::backend::formal {

using namespace fsm::ir;

namespace {

std::string to_sysml2_boolean_syntax(std::string_view expr) {
    std::string out;
    out.reserve(expr.size() + 8);
    for (std::size_t i = 0; i < expr.size(); ++i) {
        if (expr[i] == '&' && i + 1 < expr.size() && expr[i + 1] == '&') {
            if (!out.empty() && out.back() != ' ' && out.back() != '(') {
                out.push_back(' ');
            }
            out += "and";
            if (i + 2 < expr.size() && expr[i + 2] != ' ') {
                out.push_back(' ');
            }
            ++i;
            continue;
        }
        if (expr[i] == '|' && i + 1 < expr.size() && expr[i + 1] == '|') {
            if (!out.empty() && out.back() != ' ' && out.back() != '(') {
                out.push_back(' ');
            }
            out += "or";
            if (i + 2 < expr.size() && expr[i + 2] != ' ') {
                out.push_back(' ');
            }
            ++i;
            continue;
        }
        if (expr[i] == '!' && (i + 1 >= expr.size() || expr[i + 1] != '=')) {
            out += "not ";
            continue;
        }
        out.push_back(expr[i]);
    }
    return out;
}

std::string format_sysml2_action_block(const ActionSignature& act, const FsmIr& model) {
    std::ostringstream ss;
    bool first = true;
    for (const auto& a : act.assignments) {
        if (!first) {
            ss << " ";
        }
        first = false;
        const std::string lhs = a.target.full_path();
        if (a.op == AssignmentOp::Assign) {
            ss << "assign " << lhs << " := " << a.expression << ";";
        } else {
            const std::string_view op_sv = assignment_op_to_string(a.op);
            const char bin_op = op_sv.empty() ? '+' : op_sv.front();
            ss << "assign " << lhs << " := " << lhs << " " << bin_op << " " << a.expression << ";";
        }
    }
    std::size_t msg_idx = 0;
    for (const auto& inst : act.instructions) {
        if (const auto* call = std::get_if<ActionCallOp>(&inst.op)) {
            if (!first) {
                ss << " ";
            }
            first = false;
            if (call->result_target.has_value() && !call->result_target->name.empty()) {
                ss << "assign " << call->result_target->full_path() << " := ";
            }
            ss << call->function_name << "(";
            for (std::size_t i = 0; i < call->arguments.size(); ++i) {
                if (i > 0) {
                    ss << ", ";
                }
                ss << call->arguments[i];
            }
            ss << ");";
        } else if (const auto* em = std::get_if<SignalEmitOp>(&inst.op)) {
            if (!first) {
                ss << " ";
            }
            first = false;
            const std::string port =
                (em->target_port.has_value() && !em->target_port->empty()) ? *em->target_port : "out_port";
            if (em->arguments.empty()) {
                ss << "send new " << em->signal_name << "() via " << port << ";";
            } else {
                const std::string msg_var = "msg_" + std::to_string(++msg_idx);
                ss << "item " << msg_var << " : " << em->signal_name << " { ";
                const auto* sig_def = model.find_signal(em->signal_name);
                for (std::size_t i = 0; i < em->arguments.size(); ++i) {
                    const std::string attr_name = (sig_def != nullptr && i < sig_def->attributes.size())
                                                      ? sig_def->attributes[i].name
                                                      : ("arg_" + std::to_string(i));
                    ss << "attribute :>> " << attr_name << " = " << em->arguments[i] << "; ";
                }
                ss << "} send " << msg_var << " via " << port << ";";
            }
        }
    }
    return ss.str();
}

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
        out << pad << "decide " << state.name << ";\n";
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

    const bool is_parallel = (state.kind == StateKind::Parallel);

    if (!has_body && !has_children && !is_parallel) {
        out << pad << "state " << state.name << ";\n";
        return;
    }

    out << pad << "state " << state.name << (is_parallel ? " parallel" : "") << " {\n";

    // Residence time invariant preserved via valid SysML v2 structured comment directive
    if (state.time_invariant.has_value() && !state.time_invariant->empty()) {
        out << pad << "    // @fsm:state time_invariant=\"" << *state.time_invariant << "\"\n";
    }

    // Requirements
    for (const auto& req : state.traceability_reqs) {
        out << pad << "    satisfy requirement " << req << ";\n";
    }

    // Entry Actions
    for (const auto& act : state.entry_actions) {
        if (!act.assignments.empty() || !act.instructions.empty()) {
            out << pad << "    entry action { " << format_sysml2_action_block(act, model) << " }\n";
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
        if (!act.assignments.empty() || !act.instructions.empty()) {
            out << pad << "    exit action { " << format_sysml2_action_block(act, model) << " }\n";
        } else {
            out << pad << "    exit action " << act.name << ";\n";
        }
    }

    // Deferred Events (preserved via structured comment since 'defer' is not in OMG SysML v2.0 Xtext)
    for (const auto& d_evt : state.deferred_events) {
        out << pad << "    // @fsm:defer " << d_evt << "\n";
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
    out << "private import ScalarValues::*;\n";
    bool has_time_triggers = false;
    for (const auto& trans : model.transitions) {
        if (std::holds_alternative<ir::TimeTrigger>(trans.trigger)) {
            has_time_triggers = true;
            break;
        }
    }
    if (has_time_triggers) {
        out << "private import SI::*;\n";
    }
    out << "\n";
    for (const auto& svc : model.services) {
        out << "calc def " << svc.name << " {\n";
        for (const auto& p : svc.parameters) {
            out << "    in " << p.name << " : " << p.type.to_sysml_type() << ";\n";
        }
        if (!svc.is_void_return()) {
            out << "    return : " << svc.return_type.to_sysml_type() << ";\n";
        }
        out << "}\n\n";
    }
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

    // Native OMG SysML v2.0 Directional Parameters (In / Out / InOut)
    for (const auto& port : model.ports) {
        std::string dir_str = port.is_out() ? "out " : (port.direction == PortDirection::InOut ? "inout " : "in ");
        out << "    " << dir_str << port.name << " : " << port.type.to_sysml_type();
        if (!port.constraint.empty()) {
            std::string clean_c = port.constraint;
            while (!clean_c.empty() && (clean_c.back() == ';' || clean_c.back() == ' ')) {
                clean_c.pop_back();
            }
            out << " { assert constraint { " << clean_c << " } }\n";
        } else if (port.min_value.has_value() && port.max_value.has_value()) {
            out << " { assert constraint { " << port.name << " >= " << *port.min_value << " and " << port.name
                << " <= " << *port.max_value << " } }\n";
        } else {
            out << ";\n";
        }
    }
    if (!model.ports.empty()) {
        out << "\n";
    }

    // Native OMG SysML v2.0 EFSM Attributes
    for (const auto& var : model.variables) {
        out << "    attribute " << var.name << " : " << var.type.to_sysml_type();
        if (!var.initial_value.empty()) {
            out << " := " << var.initial_value;
        }
        out << ";\n";
    }
    if (!model.variables.empty()) {
        out << "\n";
    }

    // Collect synthetic event names used solely by TimeTriggers so we don't emit redundant item defs
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

    // Native OMG SysML v2.0 Item Definitions (Triggers & Payloads)
    for (const auto& sig : model.signals) {
        if (sig.attributes.empty() && time_trigger_events.count(sig.name) > 0 && non_time_events.count(sig.name) == 0) {
            continue;
        }
        if (sig.attributes.empty() && sig.validators.empty()) {
            out << "    item def " << sig.name << ";\n";
        } else {
            out << "    item def " << sig.name << " {\n";
            for (const auto& attr : sig.attributes) {
                out << "        attribute " << attr.name << " : " << attr.type.to_sysml_type();
                if (!attr.default_value.empty()) {
                    out << " := " << attr.default_value;
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

    // Temporal Logic Properties (emitted as structured comments to preserve 100% OMG SysML v2.0 grammar validity)
    for (const auto& prop : model.properties) {
        if (!prop.name.empty() && !prop.raw_formula.empty()) {
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

    // Emit decision/choice pseudostates not already present in states
    for (const auto& choice : model.choice_nodes) {
        if (model.find_state(choice.name) == nullptr) {
            out << "    decide " << choice.name << ";\n";
        }
    }

    // Emit transitions
    out << "\n";
    for (const auto& trans : model.transitions) {
        std::string clean_target = trans.target;
        if (trans.target_is_history) {
            clean_target += trans.target_is_deep_history ? "[H*]" : "[H]";
        }
        if (trans.priority > 0) {
            out << "    // @fsm:priority " << trans.priority << "\n";
        }
        out << (trans.kind == TransitionEdgeKind::Local ? "    local transition\n" : "    transition\n");
        out << "        first " << trans.source << "\n";
        if (const auto* tt = std::get_if<ir::TimeTrigger>(&trans.trigger)) {
            const std::string_view unit_str = ir::time_unit_to_string(tt->unit);
            if (tt->kind == ir::TimeTriggerKind::Every) {
                out << "        accept every " << tt->duration_value << " [" << unit_str << "]\n";
            } else if (tt->kind == ir::TimeTriggerKind::At) {
                out << "        accept at " << tt->duration_value << " [" << unit_str << "]\n";
            } else {
                out << "        accept after " << tt->duration_value << " [" << unit_str << "]\n";
            }
        } else if (const auto* ct = std::get_if<ir::ChangeTrigger>(&trans.trigger)) {
            if (!ct->raw_expression.empty()) {
                out << "        accept when (" << to_sysml2_boolean_syntax(ct->raw_expression) << ")\n";
            }
        } else if (!trans.event.empty()) {
            out << "        accept " << trans.event << "\n";
        }
        if (trans.guard && !trans.guard->empty()) {
            out << "        if "
                << to_sysml2_boolean_syntax(ir::GuardExpressionParser::to_diagram_string(*trans.guard, model.guards))
                << "\n";
        }
        if (trans.transition_action.has_value() &&
            (!trans.transition_action->assignments.empty() || !trans.transition_action->instructions.empty())) {
            out << "        do action { " << format_sysml2_action_block(*trans.transition_action, model) << " }\n";
        } else {
            std::string act = trans.get_action();
            if (!act.empty()) {
                out << "        do action " << act << "\n";
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

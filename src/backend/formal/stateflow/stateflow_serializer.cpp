#include "fsm/backend/formal/stateflow_serializer.hpp"

#include <sstream>
#include <string_view>
#include <unordered_map>
#include <variant>

#include "fsm/ir/guard.hpp"
#include "fsm/ir/type_definition.hpp"

namespace fsm::backend::formal {

using namespace fsm::ir;
using ir::GuardExpressionParser;

static std::string escape_xml(std::string_view input) {
    std::string res;
    res.reserve(input.size() * 2);
    for (char ch : input) {
        switch (ch) {
            case '&':
                res += "&amp;";
                break;
            case '<':
                res += "&lt;";
                break;
            case '>':
                res += "&gt;";
                break;
            case '"':
                res += "&quot;";
                break;
            case '\'':
                res += "&apos;";
                break;
            case '\n':
                res += "&#10;";
                break;
            default:
                res += ch;
                break;
        }
    }
    return res;
}

static std::string map_cpp_type_to_stateflow(std::string_view cpp_type) {
    if (cpp_type == "double") {
        return "double";
    }
    if (cpp_type == "float") {
        return "single";
    }
    if (cpp_type == "bool") {
        return "boolean";
    }
    if (cpp_type == "int8_t") {
        return "int8";
    }
    if (cpp_type == "uint8_t") {
        return "uint8";
    }
    if (cpp_type == "int16_t") {
        return "int16";
    }
    if (cpp_type == "uint16_t") {
        return "uint16";
    }
    if (cpp_type == "int32_t" || cpp_type == "int") {
        return "int32";
    }
    if (cpp_type == "uint32_t") {
        return "uint32";
    }
    return std::string(cpp_type);
}

static std::string format_action_for_stateflow(const ActionSignature& act) {
    if (!act.assignments.empty()) {
        std::string expr;
        for (std::size_t i = 0; i < act.assignments.size(); ++i) {
            if (i > 0) {
                expr += "; ";
            }
            const auto& a = act.assignments[i];
            expr += a.target.full_path() + " " + std::string(assignment_op_to_string(a.op)) + " " + a.expression;
        }
        return expr;
    }
    return act.name;
}

static std::string build_state_label(const StateNode& state) {
    std::string label = state.name;
    if (!state.entry_actions.empty()) {
        std::string entry_str;
        for (const auto& act : state.entry_actions) {
            if (!entry_str.empty()) {
                entry_str += "; ";
            }
            entry_str += format_action_for_stateflow(act);
        }
        label += "\nentry: " + entry_str;
    }
    if (state.do_activity.has_value() && !state.do_activity->empty()) {
        label += "\nduring: " + *state.do_activity;
    }
    if (!state.exit_actions.empty()) {
        std::string exit_str;
        for (const auto& act : state.exit_actions) {
            if (!exit_str.empty()) {
                exit_str += "; ";
            }
            exit_str += format_action_for_stateflow(act);
        }
        label += "\nexit: " + exit_str;
    }
    return label;
}

static void emit_state(std::ostream& out, const StateNode& state, const FsmIr& model, size_t indent,
                       const std::unordered_map<std::string, size_t>& name_to_ssid, size_t& next_ssid,
                       bool parent_is_parallel = false, size_t and_exec_order = 0) {
    std::string pad(indent * 2, ' ');
    auto it = name_to_ssid.find(state.name);
    size_t ssid = (it != name_to_ssid.end()) ? it->second : (next_ssid++);

    out << pad << "<state SSID=\"" << ssid << "\">\n";
    out << pad << "  <P Name=\"labelString\">" << escape_xml(build_state_label(state)) << "</P>\n";
    out << pad << "  <P Name=\"type\">" << (parent_is_parallel ? "AND_STATE" : "OR_STATE") << "</P>\n";
    if (state.kind == StateKind::Parallel) {
        out << pad << "  <P Name=\"decomposition\">SET_STATE</P>\n";
    } else {
        out << pad << "  <P Name=\"decomposition\">CLUSTER_STATE</P>\n";
    }
    if (parent_is_parallel && and_exec_order > 0) {
        out << pad << "  <P Name=\"executionOrder\">" << and_exec_order << "</P>\n";
    }

    // Preserve non-Stateflow MBSE annotations inside Simulink's standard <P Name="description"> property
    std::string desc_meta;
    if (!state.deferred_events.empty()) {
        desc_meta += "@fsm:defer=";
        for (std::size_t i = 0; i < state.deferred_events.size(); ++i) {
            if (i > 0) {
                desc_meta += ",";
            }
            desc_meta += state.deferred_events[i];
        }
    }
    if (!state.traceability_reqs.empty()) {
        if (!desc_meta.empty()) {
            desc_meta += "; ";
        }
        desc_meta += "@fsm:satisfies=";
        for (std::size_t i = 0; i < state.traceability_reqs.size(); ++i) {
            if (i > 0) {
                desc_meta += ",";
            }
            desc_meta += state.traceability_reqs[i];
        }
    }
    if (state.time_invariant.has_value() && !state.time_invariant->empty()) {
        if (!desc_meta.empty()) {
            desc_meta += "; ";
        }
        desc_meta += "@fsm:invariant=" + state.time_invariant->raw_expression;
    }
    if (!desc_meta.empty()) {
        out << pad << "  <P Name=\"description\">" << escape_xml(desc_meta) << "</P>\n";
    }

    std::vector<const StateNode*> children;
    for (const auto& s : model.states) {
        if (s.parent_state == state.name) {
            children.push_back(&s);
        }
    }

    bool has_children = !children.empty() || state.has_history;
    if (has_children) {
        out << pad << "  <Children>\n";
        if (state.has_history) {
            out << pad << "    <junction SSID=\"" << (next_ssid++) << "\">\n";
            out << pad << "      <P Name=\"type\">HISTORY_JUNCTION</P>\n";
            if (state.has_deep_history) {
                out << pad << "      <P Name=\"description\">@fsm:history=deep</P>\n";
            }
            out << pad << "    </junction>\n";
        }
        const bool is_par = (state.kind == StateKind::Parallel);
        for (std::size_t ci = 0; ci < children.size(); ++ci) {
            emit_state(out, *children[ci], model, indent + 2, name_to_ssid, next_ssid, is_par, is_par ? (ci + 1) : 0);
        }
        if (!is_par && !state.initial_sub_state.empty()) {
            auto init_it = name_to_ssid.find(state.initial_sub_state);
            if (init_it != name_to_ssid.end()) {
                out << pad << "    <transition SSID=\"" << (next_ssid++) << "\">\n";
                out << pad << "      <P Name=\"executionOrder\">1</P>\n";
                out << pad << "      <dst><P Name=\"SSID\">" << init_it->second << "</P></dst>\n";
                out << pad << "    </transition>\n";
            }
        }
        out << pad << "  </Children>\n";
    }

    out << pad << "</state>\n";
}

static std::string format_transition_label(const TransitionEdge& trans, const FsmIr& model) {
    std::string label;
    if (std::holds_alternative<TimeTrigger>(trans.trigger)) {
        const auto& tt = std::get<TimeTrigger>(trans.trigger);
        if (tt.kind == TimeTriggerKind::After) {
            label = "after(" + std::to_string(tt.duration_ms) + ", msec)";
        } else if (tt.kind == TimeTriggerKind::Every) {
            label = "every(" + std::to_string(tt.duration_ms) + ", msec)";
        } else if (tt.kind == TimeTriggerKind::At) {
            label = "at(" + std::to_string(tt.duration_ms) + ", msec)";
        }
    } else if (const auto* ct = std::get_if<ChangeTrigger>(&trans.trigger);
               ct != nullptr && !ct->raw_expression.empty()) {
        label = "when(" + ct->raw_expression + ")";
    } else if (!trans.event.empty() && trans.event != "Anonymous") {
        label = trans.event;
    }

    if (trans.guard && !trans.guard->empty()) {
        std::string readable_guard = GuardExpressionParser::to_diagram_string(*trans.guard, model.guards);
        if (!label.empty()) {
            label += " ";
        }
        label += "[" + readable_guard + "]";
    }

    if (trans.condition_action.has_value() && !trans.condition_action->name.empty()) {
        if (!label.empty()) {
            label += " ";
        }
        label += "{ " + format_action_for_stateflow(*trans.condition_action) + " }";
    }

    if (trans.transition_action.has_value() && !trans.transition_action->name.empty()) {
        if (!label.empty()) {
            label += " ";
        }
        label += "/ { " + format_action_for_stateflow(*trans.transition_action) + " }";
    }

    return label;
}

std::string StateflowSerializer::serialize(const FsmIr& model) {
    std::ostringstream out;
    std::string chart_name = model.name.empty() ? "StateflowChart" : model.name;

    // Pre-assign deterministic SSIDs to all states and choice junctions
    std::unordered_map<std::string, size_t> name_to_ssid;
    size_t next_ssid = 10;
    for (const auto& state : model.states) {
        name_to_ssid[state.name] = next_ssid++;
    }
    for (const auto& choice : model.choice_nodes) {
        if (name_to_ssid.find(choice.name) == name_to_ssid.end()) {
            name_to_ssid[choice.name] = next_ssid++;
        }
    }

    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<Stateflow>\n";
    out << "  <machine id=\"1\">\n";
    out << "    <P Name=\"name\">" << escape_xml(chart_name) << "Machine</P>\n";

    // Formal @fsm directives preserved in standard XML comments
    for (const auto& prop : model.properties) {
        out << "    <!-- @fsm:property name=" << prop.name << " kind=" << property_kind_to_string(prop.kind)
            << " ltl=\"" << prop.raw_formula << "\"";
        if (!prop.traceability_req.empty()) {
            out << " req=\"" << prop.traceability_req << "\"";
        }
        if (!prop.description.empty()) {
            out << " desc=\"" << prop.description << "\"";
        }
        out << " -->\n";
    }

    for (const auto& port : model.ports) {
        out << "    <!-- @fsm:port name=" << port.name << " type=" << port.type
            << " dir=" << (port.is_out() ? "out" : (port.direction == PortDirection::InOut ? "inout" : "in"));
        if (port.physical_unit.has_value()) {
            out << " unit=\"" << *port.physical_unit << "\"";
        }
        if (port.min_value.has_value()) {
            out << " min=" << *port.min_value;
        }
        if (port.max_value.has_value()) {
            out << " max=" << *port.max_value;
        }
        if (!port.constraint.empty()) {
            out << " constraint=\"" << port.constraint << "\"";
        }
        if (!port.description.empty()) {
            out << " desc=\"" << port.description << "\"";
        }
        out << " -->\n";
    }

    for (const auto& var : model.variables) {
        out << "    <!-- @fsm:var name=" << var.name << " type=" << var.type;
        if (var.physical_unit.has_value()) {
            out << " unit=\"" << *var.physical_unit << "\"";
        }
        if (!var.initial_value.empty()) {
            out << " init=" << var.initial_value;
        }
        if (var.min_value.has_value()) {
            out << " min=" << *var.min_value;
        }
        if (var.max_value.has_value()) {
            out << " max=" << *var.max_value;
        }
        if (!var.description.empty()) {
            out << " desc=\"" << var.description << "\"";
        }
        out << " -->\n";
    }

    for (const auto& sig : model.signals) {
        out << "    <!-- @fsm:signal " << sig.name;
        if (!sig.attributes.empty()) {
            out << "{";
            for (size_t i = 0; i < sig.attributes.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << sig.attributes[i].type << " " << sig.attributes[i].name;
            }
            out << "}";
        }
        if (!sig.validators.empty()) {
            out << " validator=\"" << sig.validators.front() << "\"";
        }
        out << " -->\n";
    }

    for (const auto& ct : model.custom_types) {
        if (ct.is_enum()) {
            out << "    <!-- @fsm:enum " << ir::format_enum_directive(ct) << " -->\n";
        } else if (ct.is_struct()) {
            out << "    <!-- @fsm:struct " << ir::format_struct_directive(ct) << " -->\n";
        }
    }

    out << "    <Children>\n";
    out << "      <chart id=\"2\">\n";
    out << "        <P Name=\"name\">" << escape_xml(chart_name) << "</P>\n";
    out << "        <P Name=\"decomposition\">CLUSTER_CHART</P>\n";
    out << "        <Children>\n";

    // Top-level states recursively
    for (const auto& state : model.states) {
        if (state.parent_state.empty()) {
            emit_state(out, state, model, 5, name_to_ssid, next_ssid);
        }
    }

    // Choice pseudostates as standard Stateflow connective junctions
    for (const auto& choice : model.choice_nodes) {
        out << "          <junction SSID=\"" << name_to_ssid[choice.name] << "\">\n";
        out << "            <P Name=\"type\">CONNECTIVE_JUNCTION</P>\n";
        out << "            <P Name=\"description\">@fsm:choice=" << escape_xml(choice.name) << "</P>\n";
        out << "          </junction>\n";
    }

    // Native Stateflow <data> declarations for ports and local variables
    for (const auto& port : model.ports) {
        const char* sf_scope = port.is_out()
                                   ? "OUTPUT_DATA"
                                   : (port.direction == PortDirection::InOut ? "DATA_STORE_MEMORY_DATA" : "INPUT_DATA");
        out << "          <data SSID=\"" << (next_ssid++) << "\" name=\"" << escape_xml(port.name) << "\">\n";
        out << "            <P Name=\"scope\">" << sf_scope << "</P>\n";
        out << "            <P Name=\"dataType\">" << escape_xml(map_cpp_type_to_stateflow(port.type.to_cpp_type()))
            << "</P>\n";
        out << "          </data>\n";
    }
    for (const auto& var : model.variables) {
        out << "          <data SSID=\"" << (next_ssid++) << "\" name=\"" << escape_xml(var.name) << "\">\n";
        out << "            <P Name=\"scope\">LOCAL_DATA</P>\n";
        out << "            <P Name=\"dataType\">" << escape_xml(map_cpp_type_to_stateflow(var.type.to_cpp_type()))
            << "</P>\n";
        out << "          </data>\n";
    }

    // Native Stateflow <event> declarations for input signals/events
    for (const auto& sig : model.signals) {
        if (!sig.name.empty() && sig.name != "Anonymous") {
            out << "          <event SSID=\"" << (next_ssid++) << "\" name=\"" << escape_xml(sig.name) << "\">\n";
            out << "            <P Name=\"scope\">INPUT_EVENT</P>\n";
            out << "          </event>\n";
        }
    }

    // Default transition to initial state (standard Stateflow: <dst> without <src>)
    if (!model.initial_state.empty()) {
        auto init_it = name_to_ssid.find(model.initial_state);
        if (init_it != name_to_ssid.end()) {
            out << "          <transition SSID=\"" << (next_ssid++) << "\">\n";
            out << "            <P Name=\"executionOrder\">1</P>\n";
            out << "            <dst><P Name=\"SSID\">" << init_it->second << "</P></dst>\n";
            out << "          </transition>\n";
        }
    }

    // Standard Stateflow transitions with <P Name="labelString">, <P Name="executionOrder">, and <src>/<dst> SSID refs
    std::unordered_map<std::string, size_t> auto_exec_order;
    for (const auto& trans : model.transitions) {
        out << "          <transition SSID=\"" << (next_ssid++) << "\">\n";
        std::string label = format_transition_label(trans, model);
        if (!label.empty()) {
            out << "            <P Name=\"labelString\">" << escape_xml(label) << "</P>\n";
        }
        const size_t exec_ord = (trans.priority > 0) ? trans.priority : (++auto_exec_order[trans.source]);
        out << "            <P Name=\"executionOrder\">" << exec_ord << "</P>\n";
        std::string trans_desc;
        if (trans.kind == TransitionEdgeKind::Internal) {
            trans_desc = "@fsm:kind=internal";
        } else if (trans.kind == TransitionEdgeKind::Local) {
            trans_desc = "@fsm:kind=local";
        }
        if (trans.priority > 0) {
            if (!trans_desc.empty()) {
                trans_desc += "; ";
            }
            trans_desc += "@fsm:priority=" + std::to_string(trans.priority);
        }
        if (!trans_desc.empty()) {
            out << "            <P Name=\"description\">" << escape_xml(trans_desc) << "</P>\n";
        }
        auto src_it = name_to_ssid.find(trans.source);
        if (src_it != name_to_ssid.end()) {
            out << "            <src><P Name=\"SSID\">" << src_it->second << "</P></src>\n";
        }
        auto dst_it = name_to_ssid.find(trans.target);
        if (dst_it != name_to_ssid.end()) {
            out << "            <dst><P Name=\"SSID\">" << dst_it->second << "</P></dst>\n";
        }
        out << "          </transition>\n";
    }

    out << "        </Children>\n";
    out << "      </chart>\n";
    out << "    </Children>\n";
    out << "  </machine>\n";
    out << "</Stateflow>\n";

    return out.str();
}

}  // namespace fsm::backend::formal

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
                       const std::unordered_map<std::string, size_t>& name_to_ssid, size_t& next_ssid) {
    std::string pad(indent * 2, ' ');
    auto it = name_to_ssid.find(state.name);
    size_t ssid = (it != name_to_ssid.end()) ? it->second : (next_ssid++);

    out << pad << "<state SSID=\"" << ssid << "\">\n";
    out << pad << "  <P Name=\"labelString\">" << escape_xml(build_state_label(state)) << "</P>\n";
    if (state.kind == StateKind::Parallel) {
        out << pad << "  <P Name=\"decomposition\">SET_STATE</P>\n";
    } else {
        out << pad << "  <P Name=\"decomposition\">CLUSTER_STATE</P>\n";
    }
    if (!state.initial_sub_state.empty()) {
        out << pad << "  <P Name=\"initial\">" << escape_xml(state.initial_sub_state) << "</P>\n";
    }
    if (!state.deferred_events.empty()) {
        std::string defer_str;
        for (std::size_t i = 0; i < state.deferred_events.size(); ++i) {
            if (i > 0) {
                defer_str += ", ";
            }
            defer_str += state.deferred_events[i];
        }
        out << pad << "  <P Name=\"defer\">" << escape_xml(defer_str) << "</P>\n";
    }
    if (!state.traceability_reqs.empty()) {
        std::string req_str;
        for (std::size_t i = 0; i < state.traceability_reqs.size(); ++i) {
            if (i > 0) {
                req_str += ", ";
            }
            req_str += state.traceability_reqs[i];
        }
        out << pad << "  <P Name=\"satisfies\">" << escape_xml(req_str) << "</P>\n";
    }
    if (state.time_invariant.has_value() && !state.time_invariant->empty()) {
        out << pad << "  <P Name=\"invariant\">" << escape_xml(state.time_invariant->raw_expression) << "</P>\n";
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
            if (state.has_deep_history) {
                out << pad << "      <P Name=\"type\">HISTORY_DEEP</P>\n";
            } else {
                out << pad << "      <P Name=\"type\">HISTORY_JUNCTION</P>\n";
            }
            out << pad << "    </junction>\n";
        }
        for (const auto* child : children) {
            emit_state(out, *child, model, indent + 2, name_to_ssid, next_ssid);
        }
        if (!state.initial_sub_state.empty()) {
            auto init_it = name_to_ssid.find(state.initial_sub_state);
            if (init_it != name_to_ssid.end()) {
                out << pad << "    <transition SSID=\"" << (next_ssid++) << "\">\n";
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
        out << "            <P Name=\"name\">" << escape_xml(choice.name) << "</P>\n";
        out << "            <P Name=\"type\">CONNECTIVE_JUNCTION</P>\n";
        out << "          </junction>\n";
    }

    // Native Stateflow <data> declarations for ports and local variables
    for (const auto& port : model.ports) {
        out << "          <data SSID=\"" << (next_ssid++) << "\" name=\"" << escape_xml(port.name) << "\">\n";
        out << "            <P Name=\"scope\">" << (port.is_out() ? "OUTPUT_DATA" : "INPUT_DATA") << "</P>\n";
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

    // Default transition to initial state (standard Stateflow: <dst> without <src>)
    if (!model.initial_state.empty()) {
        auto init_it = name_to_ssid.find(model.initial_state);
        if (init_it != name_to_ssid.end()) {
            out << "          <transition SSID=\"" << (next_ssid++) << "\">\n";
            out << "            <dst><P Name=\"SSID\">" << init_it->second << "</P></dst>\n";
            out << "          </transition>\n";
        }
    }

    // Standard Stateflow transitions with <P Name="labelString"> and <src>/<dst> SSID refs
    for (const auto& trans : model.transitions) {
        out << "          <transition SSID=\"" << (next_ssid++) << "\">\n";
        std::string label = format_transition_label(trans, model);
        if (!label.empty()) {
            out << "            <P Name=\"labelString\">" << escape_xml(label) << "</P>\n";
        }
        if (trans.kind == TransitionEdgeKind::Internal) {
            out << "            <P Name=\"type\">internal</P>\n";
        } else if (trans.kind == TransitionEdgeKind::Local) {
            out << "            <P Name=\"type\">local</P>\n";
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

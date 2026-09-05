#pragma once

#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/frontend/directive/guard_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::formal {

/**
 * @brief Serializer for MathWorks Simulink Stateflow XML chart models.
 *
 * Emits standard Stateflow XML structure (<Stateflow><machine><chart>...</chart></machine></Stateflow>)
 * preserving state hierarchy, parallel decompositions, temporal logic triggers (after(N, msec)),
 * transition labels, history junctions, and formal MBSE directives in XML comments.
 */
class StateflowSerializer {
  public:
    static std::string serialize(const FsmIr& model) {
        std::ostringstream out;
        std::string chart_name = model.name.empty() ? "StateflowChart" : model.name;

        out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        out << "<Stateflow>\n";
        out << "  <machine name=\"" << escape_xml(chart_name) << "Machine\">\n";

        // Properties
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

        // Ports
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

        // Variables
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

        // Signals
        for (const auto& sig : model.signals) {
            out << "    <!-- @fsm:signal " << sig.name;
            if (!sig.attributes.empty()) {
                out << "{";
                for (size_t i = 0; i < sig.attributes.size(); ++i) {
                    if (i > 0)
                        out << ", ";
                    out << sig.attributes[i].type << " " << sig.attributes[i].name;
                }
                out << "}";
            }
            out << " -->\n";
        }

        // Custom compound types (Enums and Structs)
        for (const auto& ct : model.custom_types) {
            if (ct.is_enum()) {
                out << "    <!-- @fsm:enum " << DirectiveParser::format_enum_directive(ct) << " -->\n";
            } else if (ct.is_struct()) {
                out << "    <!-- @fsm:struct " << DirectiveParser::format_struct_directive(ct) << " -->\n";
            }
        }

        out << "    <chart id=\"1\" name=\"" << escape_xml(chart_name) << "\"";
        if (!model.initial_state.empty()) {
            out << " initial=\"" << escape_xml(model.initial_state) << "\"";
        }
        out << ">\n";

        size_t trans_id = 100;
        // Default transition to initial state
        if (!model.initial_state.empty()) {
            out << "      <transition id=\"" << (trans_id++) << "\" dst=\"" << escape_xml(model.initial_state)
                << "\"/>\n";
        }

        // Emit top-level states recursively
        size_t state_id = 10;
        for (const auto& state : model.states) {
            if (state.parent_state.empty()) {
                emit_state(out, state, model, 3, state_id);
            }
        }

        // Emit transitions
        for (const auto& trans : model.transitions) {
            out << "      <transition id=\"" << (trans_id++) << "\" src=\"" << escape_xml(trans.source)
                << "\" dst=\"" << escape_xml(trans.target) << "\"";
            std::string label = format_transition_label(trans);
            if (!label.empty()) {
                out << " labelString=\"" << escape_xml(label) << "\"";
            }
            out << "/>\n";
        }

        out << "    </chart>\n";
        out << "  </machine>\n";
        out << "</Stateflow>\n";

        return out.str();
    }

  private:
    static void emit_state(std::ostream& out, const StateNode& state, const FsmIr& model, size_t indent,
                           size_t& state_id) {
        std::string pad(indent * 2, ' ');
        out << pad << "<state id=\"" << (state_id++) << "\" name=\"" << escape_xml(state.name) << "\"";
        if (state.kind == StateKind::Parallel) {
            out << " decomposition=\"PARALLEL_AND\"";
        }
        if (state.do_activity.has_value() && !state.do_activity->empty()) {
            out << " during=\"" << escape_xml(*state.do_activity) << "\"";
        }

        std::vector<const StateNode*> children;
        for (const auto& s : model.states) {
            if (s.parent_state == state.name) {
                children.push_back(&s);
            }
        }

        bool has_body = !children.empty() || state.has_history;
        if (!has_body) {
            out << "/>\n";
            return;
        }

        out << ">\n";

        if (state.has_history) {
            if (state.has_deep_history) {
                out << pad << "  <junction type=\"HISTORY_DEEP\"/>\n";
            } else {
                out << pad << "  <junction type=\"HISTORY\"/>\n";
            }
        }

        for (const auto* child : children) {
            emit_state(out, *child, model, indent + 1, state_id);
        }

        out << pad << "</state>\n";
    }

    static std::string format_transition_label(const TransitionEdge& trans) {
        std::string label;
        if (std::holds_alternative<TimeTrigger>(trans.trigger)) {
            const auto& tt = std::get<TimeTrigger>(trans.trigger);
            if (tt.kind == TimeTriggerKind::After) {
                label = "after(" + std::to_string(tt.duration_ms) + ", msec)";
            }
        } else if (!trans.event.empty() && trans.event != "Anonymous") {
            label = trans.event;
        }

        if (trans.guard && !trans.guard->empty()) {
            std::string readable_guard = GuardExpressionParser::to_diagram_string(*trans.guard);
            if (!label.empty()) {
                label += " ";
            }
            label += "[" + readable_guard + "]";
        }

        if (trans.action && !trans.action->empty()) {
            if (!label.empty()) {
                label += " ";
            }
            label += "/ { " + *trans.action + " }";
        }

        return label;
    }

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
                default:
                    res += ch;
                    break;
            }
        }
        return res;
    }
};

}  // namespace fsm::backend::formal

namespace fsm::backend {
using formal::StateflowSerializer;
}  // namespace fsm::backend

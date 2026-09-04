#pragma once

#include <sstream>
#include <string>
#include <vector>

#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/frontend/directive/guard_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

class ScxmlSerializer {
  public:
    static std::string serialize(const FsmIr& model) {
        std::ostringstream out;
        out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        out << "<scxml xmlns=\"http://www.w3.org/2005/07/scxml\" version=\"1.0\" initial=\"" << model.initial_state
            << "\" name=\"" << (model.name.empty() ? "GeneratedFSM" : model.name) << "\">\n";

        // Native SCXML Datamodel
        if (!model.variables.empty() || !model.ports.empty()) {
            out << "  <datamodel>\n";
            for (const auto& port : model.ports) {
                out << "    <data id=\"" << escape_xml(port.name) << "\" type=\"" << escape_xml(port.type)
                    << "\" port=\"true\" dir=\""
                    << (port.is_out() ? "out" : (port.direction == PortDirection::InOut ? "inout" : "in")) << "\"";
                if (port.min_value.has_value()) {
                    out << " min=\"" << *port.min_value << "\"";
                }
                if (port.max_value.has_value()) {
                    out << " max=\"" << *port.max_value << "\"";
                }
                if (!port.constraint.empty()) {
                    out << " constraint=\"" << escape_xml(port.constraint) << "\"";
                }
                out << "/>\n";
            }
            for (const auto& var : model.variables) {
                out << "    <data id=\"" << escape_xml(var.name) << "\"";
                if (!var.initial_value.empty()) {
                    out << " expr=\"" << escape_xml(var.initial_value) << "\"";
                }
                if (!var.type.empty()) {
                    out << " type=\"" << escape_xml(var.type) << "\"";
                }
                out << "/>\n";
            }
            out << "  </datamodel>\n";
        }

        // Emit declared signals
        for (const auto& sig : model.signals) {
            out << "  <!-- @fsm:signal " << sig.name << " -->\n";
        }

        // Emit declared enums
        for (const auto& en : model.enums) {
            out << "  <!-- @fsm:enum " << DirectiveParser::format_enum_directive(en) << " -->\n";
        }

        // Emit declared structs
        for (const auto& st : model.structs) {
            out << "  <!-- @fsm:struct " << DirectiveParser::format_struct_directive(st) << " -->\n";
        }

        // Emit top-level states recursively
        for (const auto& state : model.states) {
            if (state.parent_state.empty()) {
                emit_state(out, state, model, 2);
            }
        }

        out << "</scxml>\n";
        return out.str();
    }

  private:
    static void emit_state(std::ostream& out, const StateNode& state, const FsmIr& model, size_t indent) {
        std::string pad(indent, ' ');
        out << pad << "<state id=\"" << escape_xml(state.name) << "\"";
        if (!state.initial_sub_state.empty()) {
            out << " initial=\"" << escape_xml(state.initial_sub_state) << "\"";
        }
        if (!state.traceability_reqs.empty()) {
            out << " satisfies=\"" << escape_xml(state.traceability_reqs.front()) << "\"";
        }
        out << ">\n";

        // OnEntry actions and variable assignments
        for (const auto& entry_act : state.entry_actions) {
            out << pad << "  <onentry>\n";
            for (const auto& assign : entry_act.assignments) {
                out << pad << "    <assign location=\"" << escape_xml(assign.target_variable) << "\" expr=\""
                    << escape_xml(assign.expression) << "\"/>\n";
            }
            if (!entry_act.name.empty() && entry_act.name.rfind("entry_", 0) != 0) {
                out << pad << "    <send event=\"" << escape_xml(entry_act.name) << "\"/>\n";
            }
            out << pad << "  </onentry>\n";
        }

        // OnExit actions
        for (const auto& exit_act : state.exit_actions) {
            out << pad << "  <onexit>\n";
            for (const auto& assign : exit_act.assignments) {
                out << pad << "    <assign location=\"" << escape_xml(assign.target_variable) << "\" expr=\""
                    << escape_xml(assign.expression) << "\"/>\n";
            }
            if (!exit_act.name.empty() && exit_act.name.rfind("exit_", 0) != 0) {
                out << pad << "    <send event=\"" << escape_xml(exit_act.name) << "\"/>\n";
            }
            out << pad << "  </onexit>\n";
        }

        // Do Activity (Async/Background activity via SCXML <invoke>)
        if (state.do_activity.has_value() && !state.do_activity->empty()) {
            out << pad << "  <invoke id=\"" << escape_xml(*state.do_activity) << "\" src=\""
                << escape_xml(*state.do_activity) << "\"/>\n";
        }

        // History
        if (state.has_history) {
            out << pad << "  <history id=\"" << escape_xml(state.name) << "_hist\" type=\""
                << (state.has_deep_history ? "deep" : "shallow") << "\"/>\n";
        }

        // Deferred events
        for (const auto& d_evt : state.deferred_events) {
            out << pad << "  <defer event=\"" << escape_xml(d_evt) << "\"/>\n";
        }

        // Child states if composite
        for (const auto& child : model.states) {
            if (child.parent_state == state.name) {
                emit_state(out, child, model, indent + 2);
            }
        }

        // Transitions originating from this state
        for (const auto& trans : model.transitions) {
            if (trans.source == state.name) {
                out << pad << "  <transition";
                if (trans.priority > 0) {
                    out << " priority=\"" << trans.priority << "\"";
                }
                if (!trans.event.empty() && trans.event != "Anonymous") {
                    out << " event=\"" << escape_xml(trans.event) << "\"";
                }
                if (trans.guard && !trans.guard->empty()) {
                    std::string readable_guard = GuardExpressionParser::to_diagram_string(*trans.guard);
                    out << " cond=\"" << escape_xml(readable_guard) << "\"";
                }
                if (!trans.target.empty() && trans.kind != TransitionEdgeKind::Internal) {
                    out << " target=\"" << escape_xml(trans.target) << "\"";
                }

                bool has_assignments = trans.action_sig.has_value() && !trans.action_sig->assignments.empty();
                bool has_named_action = trans.action && !trans.action->empty() && trans.action->rfind("act_", 0) != 0;

                if (!has_assignments && !has_named_action) {
                    out << "/>\n";
                } else {
                    out << ">\n";
                    if (has_assignments) {
                        for (const auto& a : trans.action_sig->assignments) {
                            out << pad << "    <assign location=\"" << escape_xml(a.target_variable) << "\" expr=\""
                                << escape_xml(a.expression) << "\"/>\n";
                        }
                    }
                    if (has_named_action) {
                        out << pad << "    <send event=\"" << escape_xml(*trans.action) << "\"/>\n";
                    }
                    out << pad << "  </transition>\n";
                }
            }
        }

        out << pad << "</state>\n";
    }

    static std::string escape_xml(const std::string& input) {
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

}  // namespace fsm::codegen

#include "fsm/backend/formal/scxml_serializer.hpp"

#include <sstream>

#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/ir/guard.hpp"

namespace fsm::backend::formal {

using namespace fsm::ir;

namespace {

std::string escape_xml(const std::string& input) {
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

void emit_state(std::ostream& out, const StateNode& state, const FsmIr& model, size_t indent) {
    std::string pad(indent, ' ');
    const bool is_parallel = (state.kind == StateKind::Parallel || !state.orthogonal_regions.empty());
    const std::string tag_name = is_parallel ? "parallel" : "state";
    out << pad << "<" << tag_name << " id=\"" << escape_xml(state.name) << "\"";
    if (!is_parallel && !state.initial_sub_state.empty()) {
        out << " initial=\"" << escape_xml(state.initial_sub_state) << "\"";
    }
    if (!state.traceability_reqs.empty()) {
        out << " satisfies=\"" << escape_xml(state.traceability_reqs.front()) << "\"";
    }
    out << ">\n";

    auto emit_action_instructions = [&](const ActionSignature& act, const std::string& inner_pad) {
        bool emitted_instruction = false;
        for (const auto& inst : act.instructions) {
            if (const auto* emit_op = std::get_if<SignalEmitOp>(&inst.op)) {
                out << inner_pad << "<send event=\"" << escape_xml(emit_op->signal_name) << "\"/>\n";
                emitted_instruction = true;
            } else if (const auto* call_op = std::get_if<ActionCallOp>(&inst.op)) {
                out << inner_pad << "<send event=\"" << escape_xml(call_op->function_name) << "\"/>\n";
                emitted_instruction = true;
            }
        }
        if (!emitted_instruction && act.assignments.empty() && !act.name.empty()) {
            out << inner_pad << "<send event=\"" << escape_xml(act.name) << "\"/>\n";
        }
    };

    // OnEntry actions and variable assignments
    for (const auto& entry_act : state.entry_actions) {
        out << pad << "  <onentry>\n";
        for (const auto& assign : entry_act.assignments) {
            out << pad << "    <assign location=\"" << escape_xml(assign.target.full_path()) << "\" expr=\""
                << escape_xml(assign.expression) << "\"/>\n";
        }
        emit_action_instructions(entry_act, pad + "    ");
        out << pad << "  </onentry>\n";
    }

    // OnExit actions
    for (const auto& exit_act : state.exit_actions) {
        out << pad << "  <onexit>\n";
        for (const auto& assign : exit_act.assignments) {
            out << pad << "    <assign location=\"" << escape_xml(assign.target.full_path()) << "\" expr=\""
                << escape_xml(assign.expression) << "\"/>\n";
        }
        emit_action_instructions(exit_act, pad + "    ");
        out << pad << "  </onexit>\n";
    }

    // Do Activity (Async/Background activity via SCXML <invoke>)
    if (state.do_activity.has_value() && !state.do_activity->empty()) {
        out << pad << "  <invoke id=\"" << escape_xml(*state.do_activity) << "\" src=\""
            << escape_xml(*state.do_activity) << "\"/>\n";
    }

    // History
    bool state_has_history = state.has_history;
    bool state_has_deep_history = state.has_deep_history;
    if (!state_has_history) {
        for (const auto& t : model.transitions) {
            if (t.target == state.name && (t.target_is_history || t.target_is_deep_history)) {
                state_has_history = true;
                if (t.target_is_deep_history) {
                    state_has_deep_history = true;
                }
            }
        }
    }
    if (state_has_history) {
        out << pad << "  <history id=\"" << escape_xml(state.name) << "_hist\" type=\""
            << (state_has_deep_history ? "deep" : "shallow") << "\"/>\n";
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
                std::string readable_guard = ir::GuardExpressionParser::to_diagram_string(*trans.guard, model.guards);
                out << " cond=\"" << escape_xml(readable_guard) << "\"";
            }
            if (!trans.target.empty() && trans.kind != TransitionEdgeKind::Internal) {
                if (trans.target_is_history || trans.target_is_deep_history) {
                    out << " target=\"" << escape_xml(trans.target) << "_hist\"";
                } else {
                    out << " target=\"" << escape_xml(trans.target) << "\"";
                }
            }

            bool has_assignments =
                (trans.condition_action.has_value() && !trans.condition_action->assignments.empty()) ||
                (trans.transition_action.has_value() && !trans.transition_action->assignments.empty());
            bool has_instructions =
                (trans.condition_action.has_value() && !trans.condition_action->instructions.empty()) ||
                (trans.transition_action.has_value() && !trans.transition_action->instructions.empty());
            std::string act_name = trans.get_action();
            bool has_named_action =
                !has_assignments && !has_instructions && !act_name.empty() && act_name.rfind("act_", 0) != 0;

            if (!has_assignments && !has_instructions && !has_named_action) {
                out << "/>\n";
            } else {
                out << ">\n";
                if (trans.condition_action.has_value()) {
                    for (const auto& a : trans.condition_action->assignments) {
                        out << pad << "    <assign location=\"" << escape_xml(a.target.full_path()) << "\" expr=\""
                            << escape_xml(a.expression) << "\"/>\n";
                    }
                    emit_action_instructions(*trans.condition_action, pad + "    ");
                }
                if (trans.transition_action.has_value()) {
                    for (const auto& a : trans.transition_action->assignments) {
                        out << pad << "    <assign location=\"" << escape_xml(a.target.full_path()) << "\" expr=\""
                            << escape_xml(a.expression) << "\"/>\n";
                    }
                    emit_action_instructions(*trans.transition_action, pad + "    ");
                } else if (has_named_action) {
                    out << pad << "    <send event=\"" << escape_xml(act_name) << "\"/>\n";
                }
                out << pad << "  </transition>\n";
            }
        }
    }

    out << pad << "</" << tag_name << ">\n";
}

}  // namespace

std::string ScxmlSerializer::serialize(const FsmIr& model) {
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<scxml xmlns=\"http://www.w3.org/2005/07/scxml\" version=\"1.0\" initial=\"" << model.initial_state
        << "\" name=\"" << (model.name.empty() ? "GeneratedFSM" : model.name) << "\">\n";

    // Native SCXML Datamodel
    if (!model.variables.empty() || !model.ports.empty()) {
        out << "  <datamodel>\n";
        for (const auto& port : model.ports) {
            out << "    <data id=\"" << escape_xml(port.name) << "\" type=\""
                << escape_xml(port.type.to_canonical_string()) << "\" port=\"true\" dir=\""
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
            out << " type=\"" << escape_xml(var.type.to_canonical_string()) << "\"";
            out << "/>\n";
        }
        out << "  </datamodel>\n";
    }

    // Emit declared signals
    for (const auto& sig : model.signals) {
        out << "  <!-- @fsm:signal " << sig.name << " -->\n";
    }

    // Emit declared custom types (enums and structs)
    for (const auto& ct : model.custom_types) {
        if (ct.is_enum()) {
            out << "  <!-- @fsm:enum " << frontend::directive::DirectiveParser::format_enum_directive(ct) << " -->\n";
        } else if (ct.is_struct()) {
            out << "  <!-- @fsm:struct " << frontend::directive::DirectiveParser::format_struct_directive(ct)
                << " -->\n";
        }
    }

    // Emit top-level states recursively
    for (const auto& state : model.states) {
        if (state.parent_state.empty()) {
            emit_state(out, state, model, 2);
        }
    }

    // Emit choice pseudostates
    for (const auto& choice : model.choice_nodes) {
        StateNode dummy_choice(choice.name);
        dummy_choice.kind = StateKind::Choice;
        emit_state(out, dummy_choice, model, 2);
    }

    out << "</scxml>\n";
    return out.str();
}

}  // namespace fsm::backend::formal

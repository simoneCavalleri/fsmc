#include "fsm/backend/formal/scxml_serializer.hpp"

#include <sstream>

#include "fsm/ir/guard.hpp"
#include "fsm/ir/type_definition.hpp"

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
    const bool is_final = (state.kind == StateKind::Final);
    const std::string tag_name = is_parallel ? "parallel" : (is_final ? "final" : "state");
    out << pad << "<" << tag_name << " id=\"" << escape_xml(state.name) << "\"";
    if (state.kind == StateKind::Fork || state.kind == StateKind::Join || state.kind == StateKind::EntryPoint ||
        state.kind == StateKind::ExitPoint || state.kind == StateKind::Choice) {
        out << " fsm:kind=\"" << state_kind_to_string(state.kind) << "\"";
    }
    if (!is_parallel && !is_final && !state.initial_sub_state.empty()) {
        out << " initial=\"" << escape_xml(state.initial_sub_state) << "\"";
    }
    if (!state.traceability_reqs.empty()) {
        out << " fsm:satisfies=\"";
        for (size_t r = 0; r < state.traceability_reqs.size(); ++r) {
            if (r > 0)
                out << ", ";
            out << escape_xml(state.traceability_reqs[r]);
        }
        out << "\"";
    }
    if (state.time_invariant.has_value() && !state.time_invariant->empty()) {
        out << " fsm:invariant=\"" << escape_xml(*state.time_invariant) << "\"";
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

    // Collect W3C SCXML delayed-<send> timers for outgoing After TimeTriggers on this state
    struct DelayedSendTimer {
        std::string send_id;
        std::string event_name;
        std::string delay_literal;
    };
    std::vector<DelayedSendTimer> state_timers;
    if (!is_final) {
        size_t timer_idx = 0;
        for (const auto& trans : model.transitions) {
            if (trans.source == state.name && trans.event.empty()) {
                if (const auto* tt = std::get_if<TimeTrigger>(&trans.trigger);
                    tt != nullptr && tt->kind == TimeTriggerKind::After) {
                    ++timer_idx;
                    const std::string unit_s(time_unit_to_string(tt->unit));
                    state_timers.push_back({"_timer_" + state.name + "_" + std::to_string(timer_idx),
                                            "after_" + std::to_string(tt->duration_value) + unit_s,
                                            std::to_string(tt->duration_value) + unit_s});
                }
            }
        }
    }

    // OnEntry actions, variable assignments, and W3C SCXML timer arming
    for (const auto& entry_act : state.entry_actions) {
        if (is_final && entry_act.name.rfind("emit_done_state_", 0) == 0) {
            continue;
        }
        out << pad << "  <onentry>\n";
        for (const auto& assign : entry_act.assignments) {
            out << pad << "    <assign location=\"" << escape_xml(assign.target.full_path()) << "\" expr=\""
                << escape_xml(assign.expression) << "\"/>\n";
        }
        emit_action_instructions(entry_act, pad + "    ");
        out << pad << "  </onentry>\n";
    }
    if (!state_timers.empty()) {
        out << pad << "  <onentry>\n";
        for (const auto& tmr : state_timers) {
            out << pad << "    <send id=\"" << escape_xml(tmr.send_id) << "\" event=\"" << escape_xml(tmr.event_name)
                << "\" delay=\"" << escape_xml(tmr.delay_literal) << "\"/>\n";
        }
        out << pad << "  </onentry>\n";
    }

    // OnExit actions and W3C SCXML timer cancellation
    for (const auto& exit_act : state.exit_actions) {
        out << pad << "  <onexit>\n";
        for (const auto& assign : exit_act.assignments) {
            out << pad << "    <assign location=\"" << escape_xml(assign.target.full_path()) << "\" expr=\""
                << escape_xml(assign.expression) << "\"/>\n";
        }
        emit_action_instructions(exit_act, pad + "    ");
        out << pad << "  </onexit>\n";
    }
    if (!state_timers.empty()) {
        out << pad << "  <onexit>\n";
        for (const auto& tmr : state_timers) {
            out << pad << "    <cancel sendid=\"" << escape_xml(tmr.send_id) << "\"/>\n";
        }
        out << pad << "  </onexit>\n";
    }

    // W3C SCXML 1.0 §3.7: <final> only allows <onentry>, <onexit>, and <donedata>
    if (is_final) {
        out << pad << "</" << tag_name << ">\n";
        return;
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

    // Deferred events (namespaced extension outside W3C SCXML 1.0 core)
    for (const auto& d_evt : state.deferred_events) {
        out << pad << "  <fsm:defer event=\"" << escape_xml(d_evt) << "\"/>\n";
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
                out << " fsm:priority=\"" << trans.priority << "\"";
            }
            if (const auto* ct = std::get_if<ChangeTrigger>(&trans.trigger);
                ct != nullptr && !ct->raw_expression.empty()) {
                out << " event=\"when(" << escape_xml(ct->raw_expression) << ")\"";
            } else if (const auto* tt = std::get_if<TimeTrigger>(&trans.trigger);
                       tt != nullptr && trans.event.empty()) {
                out << " event=\"" << (tt->kind == TimeTriggerKind::Every ? "every_" : "after_") << tt->duration_value
                    << time_unit_to_string(tt->unit) << "\"";
            } else if (!trans.event.empty() && trans.event != "Anonymous") {
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
            if (trans.kind == TransitionEdgeKind::Internal) {
                out << " type=\"internal\"";
            } else if (trans.kind == TransitionEdgeKind::Local) {
                // W3C SCXML 1.0 §3.5: TransitionType.datatype only allows "internal" | "external".
                // In SCXML, type="internal" with a descendant target implements UML local transition semantics.
                out << " type=\"internal\" fsm:local=\"true\"";
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
    out << "<scxml xmlns=\"http://www.w3.org/2005/07/scxml\" "
           "xmlns:fsm=\"https://github.com/simoneCavalleri/fsmc/scxml\" "
           "version=\"1.0\"";
    if (!model.initial_state.empty()) {
        out << " initial=\"" << escape_xml(model.initial_state) << "\"";
    }
    out << " name=\"" << escape_xml(model.name.empty() ? "GeneratedFSM" : model.name) << "\">\n";

    // Native SCXML Datamodel (W3C <data id="..." expr="..."/> + fsm:* metadata attributes)
    if (!model.variables.empty() || !model.ports.empty()) {
        out << "  <datamodel>\n";
        for (const auto& port : model.ports) {
            out << "    <data id=\"" << escape_xml(port.name) << "\" fsm:type=\""
                << escape_xml(port.type.to_canonical_string()) << "\" fsm:port=\"true\" fsm:dir=\""
                << (port.is_out() ? "out" : (port.direction == PortDirection::InOut ? "inout" : "in")) << "\"";
            if (port.min_value.has_value()) {
                out << " fsm:min=\"" << *port.min_value << "\"";
            }
            if (port.max_value.has_value()) {
                out << " fsm:max=\"" << *port.max_value << "\"";
            }
            if (!port.constraint.empty()) {
                out << " fsm:constraint=\"" << escape_xml(port.constraint) << "\"";
            }
            out << "/>\n";
        }
        for (const auto& var : model.variables) {
            out << "    <data id=\"" << escape_xml(var.name) << "\"";
            if (!var.initial_value.empty()) {
                out << " expr=\"" << escape_xml(var.initial_value) << "\"";
            }
            out << " fsm:type=\"" << escape_xml(var.type.to_canonical_string()) << "\"";
            out << "/>\n";
        }
        out << "  </datamodel>\n";
    }

    // Emit declared properties
    for (const auto& prop : model.properties) {
        out << "  <!-- @fsm:property name=" << prop.name << " kind=" << property_kind_to_string(prop.kind) << " ltl=\""
            << prop.raw_formula << "\"";
        if (!prop.traceability_req.empty()) {
            out << " req=\"" << prop.traceability_req << "\"";
        }
        if (!prop.description.empty()) {
            out << " desc=\"" << prop.description << "\"";
        }
        out << " -->\n";
    }

    // Emit declared signals
    for (const auto& sig : model.signals) {
        if (sig.attributes.empty()) {
            out << "  <!-- @fsm:signal " << sig.name << " -->\n";
        } else {
            out << "  <!-- @fsm:signal " << sig.name << "{";
            for (size_t i = 0; i < sig.attributes.size(); ++i) {
                if (i > 0)
                    out << ", ";
                out << sig.attributes[i].type << " " << sig.attributes[i].name;
            }
            out << "}";
            if (!sig.validators.empty()) {
                out << " validator=\"" << sig.validators.front() << "\"";
            }
            out << " -->\n";
        }
    }

    // Emit declared custom types (enums and structs)
    for (const auto& ct : model.custom_types) {
        if (ct.is_enum()) {
            out << "  <!-- @fsm:enum " << ir::format_enum_directive(ct) << " -->\n";
        } else if (ct.is_struct()) {
            out << "  <!-- @fsm:struct " << ir::format_struct_directive(ct) << " -->\n";
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

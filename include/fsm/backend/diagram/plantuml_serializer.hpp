#pragma once

#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "fsm/frontend/directive/guard_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

class PlantUmlSerializer {
  public:
    static std::string serialize(const FsmIr& model) {
        std::ostringstream out;
        if (!model.name.empty() && model.name != "MyStateMachine") {
            out << "@startuml " << model.name << "\n";
        } else {
            out << "@startuml\n";
        }

        // Properties
        for (const auto& prop : model.properties) {
            out << "' @fsm:property name=" << prop.name << " kind=" << property_kind_to_string(prop.kind) << " ltl=\""
                << prop.raw_formula << "\"";
            if (!prop.traceability_req.empty()) {
                out << " req=\"" << prop.traceability_req << "\"";
            }
            if (!prop.description.empty()) {
                out << " desc=\"" << prop.description << "\"";
            }
            out << "\n";
        }

        // Ports
        for (const auto& port : model.ports) {
            out << "' @fsm:port name=" << port.name << " type=" << port.type
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
            out << "\n";
        }

        // Variables
        for (const auto& var : model.variables) {
            out << "' @fsm:var name=" << var.name << " type=" << var.type;
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
            out << "\n";
        }

        // Signals
        for (const auto& sig : model.signals) {
            if (sig.attributes.empty()) {
                out << "' @fsm:signal " << sig.name << "\n";
            } else {
                out << "' @fsm:signal " << sig.name << "{";
                for (size_t i = 0; i < sig.attributes.size(); ++i) {
                    out << sig.attributes[i].type << " " << sig.attributes[i].name;
                    if (i + 1 < sig.attributes.size()) {
                        out << ", ";
                    }
                }
                out << "}";
                if (!sig.validators.empty()) {
                    out << " validator=\"" << sig.validators.front() << "\"";
                }
                out << "\n";
            }
        }

        if (!model.properties.empty() || !model.variables.empty() || !model.signals.empty()) {
            out << "\n";
        }

        // Map each state to its parent for fast lookup
        std::map<std::string, std::string> parent_map;
        for (const auto& s : model.states) {
            parent_map[s.name] = s.parent_state;
        }

        // Track emitted transitions
        std::set<size_t> emitted_transitions;

        // Initial transition
        if (!model.initial_state.empty()) {
            out << "[*] --> " << model.initial_state << "\n";
        }

        // Emit top-level states in declaration order
        for (const auto& state : model.states) {
            if (state.parent_state.empty()) {
                if (state.is_composite) {
                    emit_state(out, state, model, parent_map, emitted_transitions, 0);
                } else {
                    emit_leaf_state(out, state, "");
                }
            }
        }

        // Emit remaining root / cross-boundary transitions
        for (size_t i = 0; i < model.transitions.size(); ++i) {
            if (emitted_transitions.find(i) != emitted_transitions.end()) {
                continue;
            }
            const auto& trans = model.transitions[i];
            std::string clean_target = trans.target;
            if (trans.target_is_history) {
                clean_target += trans.target_is_deep_history ? "[H*]" : "[H]";
            }
            out << trans.source << " --> " << clean_target;
            std::string label = build_label(trans);
            if (!label.empty()) {
                out << " : " << label;
            }
            out << "\n";
        }

        out << "@enduml\n";
        return out.str();
    }

  private:
    static void emit_leaf_state(std::ostream& out, const StateNode& state, const std::string& pad) {
        if (state.kind == StateKind::EntryPoint) {
            out << pad << "state " << state.name << " <<entryPoint>>\n";
        } else if (state.kind == StateKind::ExitPoint) {
            out << pad << "state " << state.name << " <<exitPoint>>\n";
        } else if (state.kind == StateKind::Fork) {
            out << pad << "state " << state.name << " <<fork>>\n";
        } else if (state.kind == StateKind::Join) {
            out << pad << "state " << state.name << " <<join>>\n";
        } else {
            out << pad << "state " << state.name << "\n";
        }
        if (!state.traceability_reqs.empty()) {
            out << pad << "' @fsm:state name=" << state.name << " satisfies=[";
            for (size_t r = 0; r < state.traceability_reqs.size(); ++r) {
                if (r > 0)
                    out << ", ";
                out << "\"" << state.traceability_reqs[r] << "\"";
            }
            out << "]\n";
        }
        if (state.time_invariant.has_value() && !state.time_invariant->empty()) {
            out << pad << state.name << " : invariant " << *state.time_invariant << "\n";
        }
        for (const auto& act : state.entry_actions) {
            out << pad << state.name << " : entry / " << act.name << "\n";
        }
        if (state.do_activity.has_value() && !state.do_activity->empty()) {
            out << pad << state.name << " : do / " << *state.do_activity << "\n";
        }
        for (const auto& act : state.exit_actions) {
            out << pad << state.name << " : exit / " << act.name << "\n";
        }
        for (const auto& d_evt : state.deferred_events) {
            out << pad << state.name << " : defer " << d_evt << "\n";
        }
    }

    static void emit_state(std::ostream& out, const StateNode& state, const FsmIr& model,
                           const std::map<std::string, std::string>& parent_map, std::set<size_t>& emitted_transitions,
                           size_t indent) {
        std::string pad(indent * 2, ' ');
        out << pad << "state " << state.name << " {\n";

        if (!state.traceability_reqs.empty()) {
            out << pad << "  ' @fsm:state name=" << state.name << " satisfies=[";
            for (size_t r = 0; r < state.traceability_reqs.size(); ++r) {
                if (r > 0)
                    out << ", ";
                out << "\"" << state.traceability_reqs[r] << "\"";
            }
            out << "]\n";
        }

        if (!state.initial_sub_state.empty()) {
            out << pad << "  [*] --> " << state.initial_sub_state << "\n";
        }
        if (state.has_history) {
            out << pad << "  " << (state.has_deep_history ? "[H*]" : "[H]") << "\n";
        }

        if (state.time_invariant.has_value() && !state.time_invariant->empty()) {
            out << pad << "  " << state.name << " : invariant " << *state.time_invariant << "\n";
        }
        for (const auto& act : state.entry_actions) {
            out << pad << "  " << state.name << " : entry / " << act.name << "\n";
        }
        if (state.do_activity.has_value() && !state.do_activity->empty()) {
            out << pad << "  " << state.name << " : do / " << *state.do_activity << "\n";
        }
        for (const auto& act : state.exit_actions) {
            out << pad << "  " << state.name << " : exit / " << act.name << "\n";
        }
        for (const auto& d_evt : state.deferred_events) {
            out << pad << "  " << state.name << " : defer " << d_evt << "\n";
        }

        // Child states in declaration order
        for (const auto& child : model.states) {
            if (child.parent_state == state.name) {
                if (child.is_composite) {
                    emit_state(out, child, model, parent_map, emitted_transitions, indent + 1);
                } else {
                    emit_leaf_state(out, child, pad + "  ");
                }
            }
        }

        // 3. Local transitions within this composite state scope
        for (size_t i = 0; i < model.transitions.size(); ++i) {
            if (emitted_transitions.find(i) != emitted_transitions.end()) {
                continue;
            }
            const auto& trans = model.transitions[i];
            auto src_it = parent_map.find(trans.source);
            std::string src_parent = (src_it != parent_map.end()) ? src_it->second : "";
            if (src_parent == state.name) {
                emitted_transitions.insert(i);
                std::string clean_target = trans.target;
                if (trans.target_is_history) {
                    clean_target += trans.target_is_deep_history ? "[H*]" : "[H]";
                }
                out << pad << "  " << trans.source << " --> " << clean_target;
                std::string label = build_label(trans);
                if (!label.empty()) {
                    out << " : " << label;
                }
                out << "\n";
            }
        }

        out << pad << "}\n";
    }

    static std::string build_label(const TransitionEdge& trans) {
        std::string label;
        if (trans.priority > 0) {
            label += "(prio=" + std::to_string(trans.priority) + ")";
        }
        if (!trans.event.empty()) {
            if (!label.empty()) {
                label += " ";
            }
            label += trans.event;
        }
        if (trans.guard && !trans.guard->empty()) {
            if (!label.empty()) {
                label += " ";
            }
            label += "[" + GuardExpressionParser::to_diagram_string(*trans.guard) + "]";
        }
        if (trans.action && !trans.action->empty()) {
            if (!label.empty()) {
                label += " ";
            }
            label += "/ " + *trans.action;
        }
        return label;
    }
};

}  // namespace fsm::codegen

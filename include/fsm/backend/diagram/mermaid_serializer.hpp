#pragma once

#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "fsm/frontend/directive/guard_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

class MermaidSerializer {
  public:
    static std::string serialize(const FsmIr& model) {
        std::ostringstream out;
        if (!model.name.empty() && model.name != "GeneratedFSM" && model.name != "MyStateMachine") {
            out << "---\ntitle: " << model.name << "\n---\n";
            out << "%% @fsm:name " << model.name << "\n";
        }
        out << "stateDiagram-v2\n";

        // Properties
        for (const auto& prop : model.properties) {
            out << "%% @fsm:property name=" << prop.name << " kind=" << property_kind_to_string(prop.kind) << " ltl=\""
                << prop.raw_formula << "\"";
            if (!prop.traceability_req.empty()) {
                out << " req=\"" << prop.traceability_req << "\"";
            }
            if (!prop.description.empty()) {
                out << " desc=\"" << prop.description << "\"";
            }
            out << "\n";
        }

        // Variables
        for (const auto& var : model.variables) {
            out << "%% @fsm:var name=" << var.name << " type=" << var.type;
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
                out << "%% @fsm:signal " << sig.name << "\n";
            } else {
                out << "%% @fsm:signal " << sig.name << "{";
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

        // State requirements
        for (const auto& st : model.states) {
            if (!st.traceability_reqs.empty()) {
                out << "%% @fsm:state name=" << st.name << " satisfies=[";
                for (size_t r = 0; r < st.traceability_reqs.size(); ++r) {
                    if (r > 0)
                        out << ", ";
                    out << "\"" << st.traceability_reqs[r] << "\"";
                }
                out << "]\n";
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

        // Build a stable source-state order index based on model.states order
        std::map<std::string, size_t> state_order;
        for (size_t i = 0; i < model.states.size(); ++i) {
            state_order[model.states[i].name] = i;
        }

        // Sort transitions by source state order, preserving relative order within same source
        std::vector<size_t> trans_order(model.transitions.size());
        for (size_t i = 0; i < trans_order.size(); ++i)
            trans_order[i] = i;
        std::stable_sort(trans_order.begin(), trans_order.end(), [&](size_t a, size_t b) {
            const auto& ta = model.transitions[a];
            const auto& tb = model.transitions[b];
            auto oa = state_order.count(ta.source) ? state_order.at(ta.source) : SIZE_MAX;
            auto ob = state_order.count(tb.source) ? state_order.at(tb.source) : SIZE_MAX;
            return oa < ob;
        });

        // Track emitted transitions so we don't duplicate
        std::set<size_t> emitted_transitions;

        // Initial transition
        if (!model.initial_state.empty()) {
            out << "    [*] --> " << model.initial_state << "\n";
        }

        // Emit top-level states in declaration order
        for (const auto& state : model.states) {
            if (state.parent_state.empty()) {
                if (state.is_composite) {
                    emit_state(out, state, model, parent_map, emitted_transitions, trans_order, 1);
                } else {
                    emit_leaf_state(out, state, "    ");
                }
            }
        }

        // Emit remaining outer / cross-boundary transitions in stable source-state order
        for (size_t idx : trans_order) {
            if (emitted_transitions.find(idx) != emitted_transitions.end()) {
                continue;
            }
            const auto& trans = model.transitions[idx];
            std::string target = trans.target;
            std::string label = build_label(trans);

            // Check if source is a descendant of target
            std::string curr = trans.source;
            bool is_descendant = false;
            while (parent_map.count(curr) && !parent_map.at(curr).empty()) {
                curr = parent_map.at(curr);
                if (curr == trans.target) {
                    is_descendant = true;
                    break;
                }
            }
            if (is_descendant) {
                // Find initial sub-state of target
                for (const auto& s : model.states) {
                    if (s.name == trans.target && !s.initial_sub_state.empty()) {
                        target = s.initial_sub_state;
                        break;
                    }
                }
                if (trans.target_is_history && label.find("[H]") == std::string::npos) {
                    label = label.empty() ? "[H]" : label + " [H]";
                }
            }

            out << "    " << trans.source << " --> " << target;
            if (!label.empty()) {
                out << " : " << label;
            }
            out << "\n";
        }

        return out.str();
    }

  private:
    static void emit_leaf_state(std::ostream& out, const StateNode& state, const std::string& pad) {
        if (state.kind == StateKind::EntryPoint) {
            out << pad << "state " << state.name << " <<entryPoint>>\n";
            return;
        }
        if (state.kind == StateKind::ExitPoint) {
            out << pad << "state " << state.name << " <<exitPoint>>\n";
            return;
        }
        if (state.kind == StateKind::Fork) {
            out << pad << "state " << state.name << " <<fork>>\n";
            return;
        }
        if (state.kind == StateKind::Join) {
            out << pad << "state " << state.name << " <<join>>\n";
            return;
        }
        if (state.kind == StateKind::Choice) {
            out << pad << "state " << state.name << " <<choice>>\n";
            return;
        }

        bool has_actions = !state.entry_actions.empty() || !state.exit_actions.empty() ||
                           !state.deferred_events.empty() ||
                           (state.do_activity.has_value() && !state.do_activity->empty());
        if (!has_actions) {
            out << pad << "state " << state.name << "\n";
            return;
        }

        std::string label = "<b>" + state.name + "</b><hr/>";
        bool first = true;
        for (const auto& act : state.entry_actions) {
            if (!first)
                label += "<br/>";
            label += "entry / " + act.name;
            first = false;
        }
        if (state.do_activity.has_value() && !state.do_activity->empty()) {
            if (!first)
                label += "<br/>";
            label += "do / " + *state.do_activity;
            first = false;
        }
        for (const auto& act : state.exit_actions) {
            if (!first)
                label += "<br/>";
            label += "exit / " + act.name;
            first = false;
        }
        for (const auto& d_evt : state.deferred_events) {
            if (!first)
                label += "<br/>";
            label += "defer " + d_evt;
            first = false;
        }

        out << pad << "state \"" << label << "\" as " << state.name << "\n";
    }

    static void emit_state(std::ostream& out, const StateNode& state, const FsmIr& model,
                           const std::map<std::string, std::string>& parent_map, std::set<size_t>& emitted_transitions,
                           const std::vector<size_t>& trans_order, size_t indent) {
        std::string pad(indent * 4, ' ');
        out << pad << "state " << state.name << " {\n";

        if (!state.initial_sub_state.empty()) {
            out << pad << "    [*] --> " << state.initial_sub_state << "\n";
        }

        // Emit child states in declaration order
        for (const auto& child : model.states) {
            if (child.parent_state == state.name) {
                if (child.is_composite) {
                    emit_state(out, child, model, parent_map, emitted_transitions, trans_order, indent + 1);
                } else {
                    emit_leaf_state(out, child, pad + "    ");
                }
            }
        }

        // 3. Emit local transitions in stable source-state order
        for (size_t idx : trans_order) {
            if (emitted_transitions.find(idx) != emitted_transitions.end()) {
                continue;
            }
            const auto& trans = model.transitions[idx];
            auto src_it = parent_map.find(trans.source);
            std::string src_parent = (src_it != parent_map.end()) ? src_it->second : "";
            if (src_parent == state.name) {
                emitted_transitions.insert(idx);
                std::string target = trans.target;
                std::string label = build_label(trans);
                if (target == state.name) {
                    if (!state.initial_sub_state.empty()) {
                        target = state.initial_sub_state;
                    } else {
                        target = "[*]";
                    }
                    if (trans.target_is_history && label.find("[H]") == std::string::npos) {
                        label = label.empty() ? "[H]" : label + " [H]";
                    }
                }
                out << pad << "    " << trans.source << " --> " << target;
                if (!label.empty()) {
                    out << " : " << label;
                }
                out << "\n";
            }
        }

        out << pad << "}\n";

        // Composite state lifecycle notes (Mermaid stateDiagram-v2 single-line note syntax)
        bool has_comp_actions = !state.entry_actions.empty() || !state.exit_actions.empty() ||
                                !state.deferred_events.empty() ||
                                (state.do_activity.has_value() && !state.do_activity->empty());
        if (has_comp_actions) {
            std::string note_content;
            for (const auto& act : state.entry_actions) {
                if (!note_content.empty())
                    note_content += ", ";
                note_content += "entry / " + act.name;
            }
            if (state.do_activity.has_value() && !state.do_activity->empty()) {
                if (!note_content.empty())
                    note_content += ", ";
                note_content += "do / " + *state.do_activity;
            }
            for (const auto& act : state.exit_actions) {
                if (!note_content.empty())
                    note_content += ", ";
                note_content += "exit / " + act.name;
            }
            for (const auto& d_evt : state.deferred_events) {
                if (!note_content.empty())
                    note_content += ", ";
                note_content += "defer " + d_evt;
            }
            out << pad << "note right of " << state.name << " : " << note_content << "\n";
        }
    }

    static std::string build_label(const TransitionEdge& trans) {
        std::string label;
        if (trans.priority > 0) {
            label += "(prio=" + std::to_string(trans.priority) + ")";
        }
        if (!trans.event.empty() && trans.event != "Anonymous" && trans.event != "AnonymousEvent" &&
            trans.event != "anonymous") {
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

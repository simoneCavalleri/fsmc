#pragma once

#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "fsm/frontend/guard_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

class MermaidSerializer {
  public:
    static std::string serialize(const FsmIr& model) {
        std::ostringstream out;
        out << "stateDiagram-v2\n";

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

        // Emit composite states and their contents
        for (const auto& state : model.states) {
            if (state.is_composite && state.parent_state.empty()) {
                emit_state(out, state, model, parent_map, emitted_transitions, trans_order, 1);
            }
        }

        // Emit top-level non-composite states with deferred events
        for (const auto& state : model.states) {
            if (!state.is_composite && state.parent_state.empty()) {
                for (const auto& d_evt : state.deferred_events) {
                    out << "    " << state.name << " : defer " << d_evt << "\n";
                }
            }
        }

        // Emit remaining outer / cross-boundary transitions in stable source-state order
        for (size_t idx : trans_order) {
            if (emitted_transitions.find(idx) != emitted_transitions.end()) {
                continue;
            }
            const auto& trans = model.transitions[idx];
            std::string clean_target = trans.target;
            if (trans.target_is_history) {
                clean_target += trans.target_is_deep_history ? "[H*]" : "[H]";
            }
            out << "    " << trans.source << " --> " << clean_target;
            std::string label = build_label(trans);
            if (!label.empty()) {
                out << " : " << label;
            }
            out << "\n";
        }

        return out.str();
    }

  private:
    static void emit_state(std::ostream& out, const StateNode& state, const FsmIr& model,
                           const std::map<std::string, std::string>& parent_map, std::set<size_t>& emitted_transitions,
                           const std::vector<size_t>& trans_order, size_t indent) {
        std::string pad(indent * 4, ' ');
        out << pad << "state " << state.name << " {\n";

        if (!state.initial_sub_state.empty()) {
            out << pad << "    [*] --> " << state.initial_sub_state << "\n";
        }
        if (state.has_history) {
            out << pad << "    " << (state.has_deep_history ? "[H*]" : "[H]") << "\n";
        }

        for (const auto& d_evt : state.deferred_events) {
            out << pad << "    " << state.name << " : defer " << d_evt << "\n";
        }

        // 1. Emit child composite states first
        for (const auto& child : model.states) {
            if (child.parent_state == state.name && child.is_composite) {
                emit_state(out, child, model, parent_map, emitted_transitions, trans_order, indent + 1);
            }
        }

        // 2. Emit non-composite child states and their deferred events
        for (const auto& child : model.states) {
            if (child.parent_state == state.name && !child.is_composite) {
                out << pad << "    state " << child.name << "\n";
                for (const auto& d_evt : child.deferred_events) {
                    out << pad << "    " << child.name << " : defer " << d_evt << "\n";
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
                std::string clean_target = trans.target;
                if (trans.target_is_history) {
                    clean_target += trans.target_is_deep_history ? "[H*]" : "[H]";
                }
                out << pad << "    " << trans.source << " --> " << clean_target;
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
        if (!trans.event.empty()) {
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

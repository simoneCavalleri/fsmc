#pragma once

#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/frontend/directive/guard_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::diagram {

class DotSerializer {
  public:
    static std::string serialize(const FsmIr& model) {
        std::ostringstream out;
        std::string graph_name = model.name.empty() ? "GeneratedFSM" : model.name;

        out << "digraph " << escape_id(graph_name) << " {\n";
        out << "    fontname=\"Helvetica\";\n";
        out << "    fontsize=11;\n";
        out << "    compound=true;\n";
        out << "    rankdir=TB;\n";
        out << "    node [fontname=\"Helvetica\", fontsize=10, shape=box, style=\"rounded,filled\", "
               "fillcolor=\"#f8fafc\", color=\"#334155\", penwidth=1.2];\n";
        out << "    edge [fontname=\"Helvetica\", fontsize=9, color=\"#475569\", arrowsize=0.8];\n\n";

        // Properties, Ports, Variables, Signals directives
        for (const auto& prop : model.properties) {
            out << "    // @fsm:property name=" << prop.name << " kind=" << property_kind_to_string(prop.kind)
                << " ltl=\"" << prop.raw_formula << "\"\n";
        }
        for (const auto& port : model.ports) {
            out << "    // @fsm:port name=" << port.name << " type=" << port.type
                << " dir=" << (port.is_out() ? "out" : (port.direction == PortDirection::InOut ? "inout" : "in"))
                << "\n";
        }
        for (const auto& var : model.variables) {
            out << "    // @fsm:var name=" << var.name << " type=" << var.type << " init=" << var.initial_value << "\n";
        }
        for (const auto& sig : model.signals) {
            out << "    // @fsm:signal " << sig.name << "\n";
        }
        for (const auto& ct : model.custom_types) {
            if (ct.is_enum()) {
                out << "    // @fsm:enum " << DirectiveParser::format_enum_directive(ct) << "\n";
            } else if (ct.is_struct()) {
                out << "    // @fsm:struct " << DirectiveParser::format_struct_directive(ct) << "\n";
            }
        }
        if (!model.properties.empty() || !model.ports.empty() || !model.variables.empty() || !model.signals.empty() ||
            !model.custom_types.empty()) {
            out << "\n";
        }

        // Map parent states
        std::map<std::string, std::string> parent_map;
        for (const auto& s : model.states) {
            parent_map[s.name] = s.parent_state;
        }

        // Choice pseudostates
        for (const auto& choice : model.choice_nodes) {
            out << "    " << escape_id(choice.name)
                << " [shape=diamond, style=filled, fillcolor=\"#fef08a\", color=\"#ca8a04\", label=\""
                << escape_label(choice.name) << "\"];\n";
        }

        // Root initial state
        out << "    __start__ [shape=circle, style=filled, fillcolor=\"#0f172a\", width=0.15, label=\"\"];\n";
        if (!model.initial_state.empty()) {
            out << "    __start__ -> " << escape_id(model.initial_state) << ";\n";
        }

        std::set<size_t> emitted_transitions;

        // Recursive cluster emitter
        std::function<void(const StateNode&, size_t)> emit_state_node = [&](const StateNode& state, size_t indent) {
            std::string pad(indent * 4, ' ');

            if (state.is_composite) {
                std::string comp_label = state.name;
                for (const auto& act : state.entry_actions) {
                    comp_label += "\\n(entry / " + act.name + ")";
                }
                if (state.do_activity.has_value() && !state.do_activity->empty()) {
                    comp_label += "\\n(do / " + *state.do_activity + ")";
                }
                for (const auto& act : state.exit_actions) {
                    comp_label += "\\n(exit / " + act.name + ")";
                }
                for (const auto& d_evt : state.deferred_events) {
                    comp_label += "\\n(defer " + d_evt + ")";
                }

                out << pad << "subgraph cluster_" << escape_id(state.name) << " {\n";
                out << pad << "    label=\"" << escape_label(comp_label) << "\";\n";
                out << pad << "    style=\"rounded,filled\";\n";
                out << pad << "    fillcolor=\"" << (indent % 2 == 1 ? "#f1f5f9" : "#e2e8f0") << "\";\n";
                out << pad << "    color=\"#64748b\";\n";
                out << pad << "    penwidth=1.5;\n\n";

                if (!state.initial_sub_state.empty()) {
                    std::string init_node = "__start_" + state.name + "__";
                    out << pad << "    " << init_node
                        << " [shape=circle, style=filled, fillcolor=\"#0f172a\", width=0.12, label=\"\"];\n";
                    out << pad << "    " << init_node << " -> " << escape_id(state.initial_sub_state) << ";\n";
                }

                if (state.has_history) {
                    std::string hist_node = "__hist_" + state.name + "__";
                    out << pad << "    " << hist_node
                        << " [shape=circle, style=\"filled,bold\", fillcolor=\"#e0e7ff\", color=\"#4f46e5\", "
                           "width=0.25, label=\""
                        << (state.has_deep_history ? "H*" : "H") << "\"];\n";
                }

                // Child states
                for (const auto& child : model.states) {
                    if (child.parent_state == state.name) {
                        emit_state_node(child, indent + 1);
                    }
                }

                // Local transitions inside composite state
                for (size_t i = 0; i < model.transitions.size(); ++i) {
                    if (emitted_transitions.count(i))
                        continue;
                    const auto& trans = model.transitions[i];
                    auto it_p = parent_map.find(trans.source);
                    std::string p_src = (it_p != parent_map.end()) ? it_p->second : "";
                    if (p_src == state.name) {
                        emitted_transitions.insert(i);
                        out << pad << "    " << escape_id(trans.source) << " -> " << escape_id(trans.target);
                        std::string label = build_label(trans);
                        if (!label.empty()) {
                            out << " [label=\"" << escape_label(label) << "\"]";
                        }
                        out << ";\n";
                    }
                }

                out << pad << "}\n";
            } else {
                std::string label = state.name;
                if (state.time_invariant.has_value() && !state.time_invariant->empty()) {
                    label += "\\n(stay <= " + *state.time_invariant + ")";
                }
                for (const auto& act : state.entry_actions) {
                    label += "\\n(entry / " + act.name + ")";
                }
                if (state.do_activity.has_value() && !state.do_activity->empty()) {
                    label += "\\n(do / " + *state.do_activity + ")";
                }
                for (const auto& act : state.exit_actions) {
                    label += "\\n(exit / " + act.name + ")";
                }
                for (const auto& d_evt : state.deferred_events) {
                    label += "\\n(defer " + d_evt + ")";
                }
                out << pad << escape_id(state.name) << " [label=\"" << escape_label(label) << "\"];\n";
            }
        };

        // Emit top-level states
        for (const auto& state : model.states) {
            if (state.parent_state.empty()) {
                emit_state_node(state, 1);
            }
        }

        // Remaining Outer / Cross-boundary transitions
        for (size_t i = 0; i < model.transitions.size(); ++i) {
            if (emitted_transitions.count(i))
                continue;
            const auto& trans = model.transitions[i];
            out << "    " << escape_id(trans.source) << " -> " << escape_id(trans.target);
            std::string label = build_label(trans);
            if (!label.empty()) {
                out << " [label=\"" << escape_label(label) << "\"]";
            }
            out << ";\n";
        }

        out << "}\n";
        return out.str();
    }

  private:
    static std::string escape_id(const std::string& input) {
        std::string res;
        for (char c : input) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                res += c;
            } else {
                res += '_';
            }
        }
        return res.empty() ? "node" : res;
    }

    static std::string escape_label(const std::string& input) {
        std::string res;
        for (char c : input) {
            if (c == '"') {
                res += "\\\"";
            } else if (c == '\\') {
                res += "\\\\";
            } else {
                res += c;
            }
        }
        return res;
    }

    static std::string build_label(const TransitionEdge& trans) {
        std::string label;
        if (trans.priority > 0) {
            label += "(prio=" + std::to_string(trans.priority) + ")";
        }
        if (!trans.event.empty() && trans.event != "Anonymous") {
            if (!label.empty())
                label += " ";
            label += trans.event;
        }
        if (trans.guard && !trans.guard->empty()) {
            if (!label.empty())
                label += " ";
            label += "[" + GuardExpressionParser::to_diagram_string(*trans.guard) + "]";
        }
        if (trans.action && !trans.action->empty()) {
            if (!label.empty())
                label += " ";
            label += "/ " + *trans.action;
        }
        return label;
    }
};

}  // namespace fsm::backend::diagram

namespace fsm::backend {
using diagram::DotSerializer;
}  // namespace fsm::backend

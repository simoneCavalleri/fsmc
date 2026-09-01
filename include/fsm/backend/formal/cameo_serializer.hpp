#pragma once

#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include "fsm/frontend/guard_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

class CameoSerializer {
  public:
    static std::string serialize(const FsmIr& model) {
        std::ostringstream out;
        std::string model_name = model.name.empty() ? "GeneratedFSM" : model.name;

        out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        out << "<xmi:XMI xmi:version=\"2.1\" xmlns:uml=\"http://www.omg.org/spec/UML/20090901\" "
               "xmlns:xmi=\"http://schema.omg.org/spec/XMI/2.1\">\n";
        out << "  <uml:Model xmi:id=\"_m1\" name=\"" << escape_xml(model_name) << "Model\">\n";
        out << "    <packagedElement xmi:type=\"uml:StateMachine\" xmi:id=\"_sm1\" name=\"" << escape_xml(model_name)
            << "\">\n";
        out << "      <region xmi:id=\"_r1\">\n";

        std::map<std::string, std::string> id_map;
        size_t id_counter = 1;
        for (const auto& s : model.states) {
            id_map[s.name] = "_s" + std::to_string(id_counter++);
        }
        for (const auto& c : model.choice_nodes) {
            id_map[c.name] = "_ps_choice_" + std::to_string(id_counter++);
        }

        // Map parent states
        std::map<std::string, std::string> parent_map;
        for (const auto& s : model.states) {
            parent_map[s.name] = s.parent_state;
        }

        // Root initial pseudostate
        out << "        <subvertex xmi:type=\"uml:Pseudostate\" xmi:id=\"_ps_root\" kind=\"initial\"/>\n";

        // Emit choice pseudostates
        for (const auto& choice : model.choice_nodes) {
            out << "        <subvertex xmi:type=\"uml:Pseudostate\" xmi:id=\"" << id_map[choice.name] << "\" name=\""
                << escape_xml(choice.name) << "\" kind=\"choice\"/>\n";
        }

        std::set<size_t> emitted_transitions;

        // Recursive state emission
        std::function<void(const StateNode&, size_t)> emit_state_node = [&](const StateNode& state, size_t indent) {
            std::string pad(indent * 2, ' ');
            std::string st_id = id_map[state.name];

            if (state.kind == StateKind::EntryPoint) {
                out << pad << "<subvertex xmi:type=\"uml:Pseudostate\" xmi:id=\"" << st_id << "\" name=\""
                    << escape_xml(state.name) << "\" kind=\"entryPoint\"/>\n";
                return;
            }
            if (state.kind == StateKind::ExitPoint) {
                out << pad << "<subvertex xmi:type=\"uml:Pseudostate\" xmi:id=\"" << st_id << "\" name=\""
                    << escape_xml(state.name) << "\" kind=\"exitPoint\"/>\n";
                return;
            }

            out << pad << "<subvertex xmi:type=\"uml:State\" xmi:id=\"" << st_id << "\" name=\""
                << escape_xml(state.name) << "\">\n";

            for (const auto& act : state.entry_actions) {
                out << pad << "  <entry xmi:type=\"uml:Activity\" name=\"" << escape_xml(act.name) << "\"/>\n";
            }
            if (state.do_activity.has_value() && !state.do_activity->empty()) {
                out << pad << "  <doActivity xmi:type=\"uml:Activity\" name=\"" << escape_xml(*state.do_activity)
                    << "\"/>\n";
            }
            for (const auto& act : state.exit_actions) {
                out << pad << "  <exit xmi:type=\"uml:Activity\" name=\"" << escape_xml(act.name) << "\"/>\n";
            }
            for (const auto& d_evt : state.deferred_events) {
                out << pad << "  <deferrableTrigger name=\"" << escape_xml(d_evt) << "\"/>\n";
            }

            if (state.is_composite) {
                std::string reg_id = "_r_" + st_id;
                out << pad << "  <region xmi:id=\"" << reg_id << "\">\n";

                if (!state.initial_sub_state.empty() && id_map.count(state.initial_sub_state)) {
                    std::string init_ps = "_ps_init_" + st_id;
                    out << pad << "    <subvertex xmi:type=\"uml:Pseudostate\" xmi:id=\"" << init_ps
                        << "\" kind=\"initial\"/>\n";
                    out << pad << "    <transition xmi:id=\"_t_init_" << st_id << "\" source=\"" << init_ps
                        << "\" target=\"" << id_map[state.initial_sub_state] << "\"/>\n";
                }

                if (state.has_history) {
                    std::string hist_ps = "_ps_hist_" + st_id;
                    std::string kind_str = state.has_deep_history ? "deepHistory" : "shallowHistory";
                    out << pad << "    <subvertex xmi:type=\"uml:Pseudostate\" xmi:id=\"" << hist_ps << "\" kind=\""
                        << kind_str << "\"/>\n";
                }

                // Child states
                for (const auto& child : model.states) {
                    if (child.parent_state == state.name) {
                        emit_state_node(child, indent + 2);
                    }
                }

                // Local transitions within this composite state
                for (size_t i = 0; i < model.transitions.size(); ++i) {
                    if (emitted_transitions.count(i))
                        continue;
                    const auto& trans = model.transitions[i];
                    auto it_p = parent_map.find(trans.source);
                    std::string p_src = (it_p != parent_map.end()) ? it_p->second : "";
                    if (p_src == state.name) {
                        emitted_transitions.insert(i);
                        std::string src_id = id_map.count(trans.source) ? id_map[trans.source] : trans.source;
                        std::string dst_id = id_map.count(trans.target) ? id_map[trans.target] : trans.target;
                        out << pad << "    <transition xmi:id=\"_t" << (i + 1) << "\" source=\"" << src_id
                            << "\" target=\"" << dst_id << "\"";
                        if (trans.priority > 0) {
                            out << " priority=\"" << trans.priority << "\"";
                        }
                        if (!trans.event.empty() && trans.event != "Anonymous") {
                            out << " trigger=\"" << escape_xml(trans.event) << "\"";
                        }
                        if (trans.guard && !trans.guard->empty()) {
                            std::string readable_guard = GuardExpressionParser::to_diagram_string(*trans.guard);
                            out << " guard=\"" << escape_xml(readable_guard) << "\"";
                        }
                        if (trans.action && !trans.action->empty()) {
                            out << " effect=\"" << escape_xml(*trans.action) << "\"";
                        }
                        out << "/>\n";
                    }
                }

                out << pad << "  </region>\n";
            }

            out << pad << "</subvertex>\n";
        };

        // Emit top-level states
        for (const auto& state : model.states) {
            if (state.parent_state.empty()) {
                emit_state_node(state, 4);
            }
        }

        // Root initial transition
        if (!model.initial_state.empty() && id_map.count(model.initial_state)) {
            out << "        <transition xmi:id=\"_t_root_init\" source=\"_ps_root\" target=\""
                << id_map[model.initial_state] << "\"/>\n";
        }

        // Outer / Cross-boundary transitions
        for (size_t i = 0; i < model.transitions.size(); ++i) {
            if (emitted_transitions.count(i))
                continue;
            const auto& trans = model.transitions[i];
            std::string src_id = id_map.count(trans.source) ? id_map[trans.source] : trans.source;
            std::string dst_id = id_map.count(trans.target) ? id_map[trans.target] : trans.target;
            out << "        <transition xmi:id=\"_t" << (i + 1) << "\" source=\"" << src_id << "\" target=\"" << dst_id
                << "\"";
            if (trans.priority > 0) {
                out << " priority=\"" << trans.priority << "\"";
            }
            if (!trans.event.empty() && trans.event != "Anonymous") {
                out << " trigger=\"" << escape_xml(trans.event) << "\"";
            }
            if (trans.guard && !trans.guard->empty()) {
                std::string readable_guard = GuardExpressionParser::to_diagram_string(*trans.guard);
                out << " guard=\"" << escape_xml(readable_guard) << "\"";
            }
            if (trans.action && !trans.action->empty()) {
                out << " effect=\"" << escape_xml(*trans.action) << "\"";
            }
            out << "/>\n";
        }

        out << "      </region>\n";
        out << "    </packagedElement>\n";
        out << "  </uml:Model>\n";
        out << "</xmi:XMI>\n";
        return out.str();
    }

  private:
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

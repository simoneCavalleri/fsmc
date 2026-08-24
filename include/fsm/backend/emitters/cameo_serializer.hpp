#pragma once

#include <map>
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
        out << "        <subvertex xmi:type=\"uml:Pseudostate\" xmi:id=\"_ps1\" kind=\"initial\"/>\n";

        std::map<std::string, std::string> id_map;
        size_t s_idx = 1;
        for (const auto& state : model.states) {
            std::string id = "_s" + std::to_string(s_idx++);
            id_map[state.name] = id;
            out << "        <subvertex xmi:type=\"uml:State\" xmi:id=\"" << id << "\" name=\"" << escape_xml(state.name)
                << "\"";
            if (state.deferred_events.empty()) {
                out << "/>\n";
            } else {
                out << ">\n";
                for (const auto& d_evt : state.deferred_events) {
                    out << "          <deferrableTrigger name=\"" << escape_xml(d_evt) << "\"/>\n";
                }
                out << "        </subvertex>\n";
            }
        }

        if (!model.initial_state.empty() && id_map.find(model.initial_state) != id_map.end()) {
            out << "        <transition xmi:id=\"_t0\" source=\"_ps1\" target=\"" << id_map[model.initial_state]
                << "\"/>\n";
        }

        size_t t_idx = 1;
        for (const auto& trans : model.transitions) {
            std::string src_id = id_map.count(trans.source) ? id_map[trans.source] : trans.source;
            std::string dst_id = id_map.count(trans.target) ? id_map[trans.target] : trans.target;
            out << "        <transition xmi:id=\"_t" << (t_idx++) << "\" source=\"" << src_id << "\" target=\""
                << dst_id << "\"";
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

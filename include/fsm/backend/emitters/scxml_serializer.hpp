#pragma once

#include <sstream>
#include <string>

#include "fsm/frontend/guard_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

class ScxmlSerializer {
  public:
    static std::string serialize(const FsmIr& model) {
        std::ostringstream out;
        out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        out << "<scxml xmlns=\"http://www.w3.org/2005/07/scxml\" version=\"1.0\" initial=\"" << model.initial_state
            << "\" name=\"" << (model.name.empty() ? "GeneratedFSM" : model.name) << "\">\n";

        for (const auto& state : model.states) {
            out << "  <state id=\"" << state.name << "\">\n";
            for (const auto& d_evt : state.deferred_events) {
                out << "    <defer event=\"" << escape_xml(d_evt) << "\"/>\n";
            }
            for (const auto& trans : model.transitions) {
                if (trans.source == state.name) {
                    out << "    <transition";
                    if (!trans.event.empty() && trans.event != "Anonymous") {
                        out << " event=\"" << escape_xml(trans.event) << "\"";
                    }
                    if (trans.guard && !trans.guard->empty()) {
                        std::string readable_guard = GuardExpressionParser::to_diagram_string(*trans.guard);
                        out << " cond=\"" << escape_xml(readable_guard) << "\"";
                    }
                    if (trans.action && !trans.action->empty()) {
                        out << " action=\"" << escape_xml(*trans.action) << "\"";
                    }
                    if (!trans.target.empty() && trans.kind != TransitionEdgeKind::Internal) {
                        out << " target=\"" << escape_xml(trans.target) << "\"";
                    }
                    out << "/>\n";
                }
            }
            out << "  </state>\n";
        }

        out << "</scxml>\n";
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

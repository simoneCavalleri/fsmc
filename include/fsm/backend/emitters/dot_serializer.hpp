#pragma once

#include <sstream>
#include <string>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

class DotSerializer {
  public:
    static std::string serialize(const FsmIr& model) {
        std::ostringstream out;
        out << "digraph " << (model.name.empty() ? "GeneratedFSM" : model.name) << " {\n";
        out << "    __start__ [shape=point];\n";
        if (!model.initial_state.empty()) {
            out << "    __start__ -> " << model.initial_state << ";\n";
        }

        for (const auto& trans : model.transitions) {
            std::string label = trans.event;
            if (trans.guard && !trans.guard->empty()) {
                label += " [" + *trans.guard + "]";
            }
            if (trans.action && !trans.action->empty()) {
                label += " / " + *trans.action;
            }

            out << "    " << trans.source << " -> " << trans.target;
            if (!label.empty() && label != "Anonymous") {
                out << " [label=\"" << label << "\"]";
            }
            out << ";\n";
        }

        out << "}\n";
        return out.str();
    }
};

}  // namespace fsm::codegen

#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/backend/diagram/dot_serializer.hpp"
#include "fsm/backend/diagram/json_serializer.hpp"
#include "fsm/backend/diagram/mermaid_serializer.hpp"
#include "fsm/backend/diagram/plantuml_serializer.hpp"
#include "fsm/backend/formal/cameo_serializer.hpp"
#include "fsm/backend/formal/scxml_serializer.hpp"
#include "fsm/backend/formal/smv_serializer.hpp"
#include "fsm/backend/formal/stateflow_serializer.hpp"
#include "fsm/backend/formal/sysml2_serializer.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"

namespace fsm::codegen {

class EmitterFactory {
  public:
    static std::string emit_diagram(const FsmIr& ir, std::string_view format) {
        if (format == "ir" || format == "fsm_ir" || format == "json_ir") {
            return FsmIrSerializer::serialize_json(ir);
        }
        if (format == "plantuml" || format == "puml") {
            return PlantUmlSerializer::serialize(ir);
        }
        if (format == "mermaid" || format == "mmd") {
            return MermaidSerializer::serialize(ir);
        }
        if (format == "sysml" || format == "sysml2") {
            return Sysml2Serializer::serialize(ir);
        }
        if (format == "json" || format == "xstate") {
            return JsonSerializer::serialize(ir);
        }
        if (format == "dot" || format == "gv" || format == "graphviz") {
            return DotSerializer::serialize(ir);
        }
        if (format == "scxml") {
            return ScxmlSerializer::serialize(ir);
        }
        if (format == "cameo" || format == "xmi" || format == "magicdraw") {
            return CameoSerializer::serialize(ir);
        }
        if (format == "smv" || format == "nuxmv") {
            return SmvSerializer::serialize(ir);
        }
        if (format == "stateflow" || format == "sfx" || format == "simulink") {
            return StateflowSerializer::serialize(ir);
        }
        return "";
    }

    static std::vector<std::string> supported_formats() {
        return {"ir", "plantuml", "mermaid", "sysml2", "json", "dot", "scxml", "cameo", "smv", "stateflow"};
    }
};

}  // namespace fsm::codegen

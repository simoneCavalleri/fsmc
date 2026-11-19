#include "fsm/backend/emitter_factory.hpp"

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/diagram/dot_serializer.hpp"
#include "fsm/backend/diagram/mermaid_serializer.hpp"
#include "fsm/backend/diagram/plantuml_serializer.hpp"
#include "fsm/backend/formal/cameo_serializer.hpp"
#include "fsm/backend/formal/scxml_serializer.hpp"
#include "fsm/backend/formal/smv_serializer.hpp"
#include "fsm/backend/formal/stateflow_serializer.hpp"
#include "fsm/backend/formal/sysml2_serializer.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"

namespace fsm::backend {

using namespace diagram;
using namespace formal;

std::string EmitterFactory::emit(const FsmIr& model, std::string_view format) {
    if (format == "json" || format == "ir" || format == "fsm_ir" || format == "json_ir") {
        return ir::FsmIrSerializer::serialize_json(model);
    }
    if (format == "plantuml" || format == "puml") {
        return PlantUmlSerializer::serialize(model);
    }
    if (format == "mermaid" || format == "mmd") {
        return MermaidSerializer::serialize(model);
    }
    if (format == "sysml" || format == "sysml2") {
        return Sysml2Serializer::serialize(model);
    }
    if (format == "dot" || format == "gv" || format == "graphviz") {
        return DotSerializer::serialize(model);
    }
    if (format == "scxml") {
        return ScxmlSerializer::serialize(model);
    }
    if (format == "cameo" || format == "xmi" || format == "magicdraw") {
        return CameoSerializer::serialize(model);
    }
    if (format == "smv" || format == "nuxmv" || format == "nusmv") {
        return SmvSerializer::serialize(model);
    }
    if (format == "stateflow" || format == "sfx" || format == "simulink") {
        return StateflowSerializer::serialize(model);
    }
    if (format == "cpp" || format == "c++" || format == "c++20" || format == "cpp20") {
        return cpp::CppGenerator::generate_header(model);
    }
    if (format == "c++17" || format == "cpp17") {
        cpp::GeneratorOptions opts{};
        opts.cpp_standard = cpp::CppStandard::Cpp17;
        return cpp::CppGenerator::generate_header(model, opts);
    }
    return "";
}

std::string EmitterFactory::emit_diagram(const FsmIr& ir, std::string_view format) {
    return emit(ir, format);
}

std::vector<std::string_view> EmitterFactory::supported_formats() noexcept {
    return {"ir",    "plantuml", "mermaid",   "sysml2", "json",  "dot",  "scxml",
            "cameo", "smv",      "stateflow", "cpp",    "c++17", "c++20"};
}

bool EmitterFactory::is_supported_format(std::string_view format_name) noexcept {
    return format_name == "json" || format_name == "ir" || format_name == "fsm_ir" || format_name == "json_ir" ||
           format_name == "plantuml" || format_name == "puml" || format_name == "mermaid" || format_name == "mmd" ||
           format_name == "sysml" || format_name == "sysml2" || format_name == "dot" || format_name == "gv" ||
           format_name == "graphviz" || format_name == "scxml" || format_name == "cameo" || format_name == "xmi" ||
           format_name == "magicdraw" || format_name == "smv" || format_name == "nuxmv" || format_name == "nusmv" ||
           format_name == "stateflow" || format_name == "sfx" || format_name == "simulink" || format_name == "cpp" ||
           format_name == "c++" || format_name == "c++20" || format_name == "cpp20" || format_name == "c++17" ||
           format_name == "cpp17";
}

}  // namespace fsm::backend

#include "fsm/backend/emitter_factory.hpp"

#include <functional>
#include <vector>

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

namespace {

struct EmitterDescriptor {
    std::string_view canonical_name;
    std::vector<std::string_view> aliases;
    std::function<std::string(const FsmIr&)> emit_fn;
};

const std::vector<EmitterDescriptor>& emitter_registry() {
    static const std::vector<EmitterDescriptor> kRegistry = {
        {"ir",
         {"json", "fsm_ir", "json_ir"},
         [](const FsmIr& m) { return ir::FsmIrSerializer::serialize_json(m); }},
        {"plantuml", {"puml"}, [](const FsmIr& m) { return PlantUmlSerializer::serialize(m); }},
        {"mermaid", {"mmd"}, [](const FsmIr& m) { return MermaidSerializer::serialize(m); }},
        {"sysml2", {"sysml"}, [](const FsmIr& m) { return Sysml2Serializer::serialize(m); }},
        {"dot", {"gv", "graphviz"}, [](const FsmIr& m) { return DotSerializer::serialize(m); }},
        {"scxml", {}, [](const FsmIr& m) { return ScxmlSerializer::serialize(m); }},
        {"cameo", {"xmi", "magicdraw"}, [](const FsmIr& m) { return CameoSerializer::serialize(m); }},
        {"smv", {"nuxmv", "nusmv"}, [](const FsmIr& m) { return SmvSerializer::serialize(m); }},
        {"stateflow", {"sfx", "simulink"}, [](const FsmIr& m) { return StateflowSerializer::serialize(m); }},
        {"cpp", {"c++", "c++20", "cpp20"}, [](const FsmIr& m) { return cpp::CppGenerator::generate_header(m); }},
        {"c++17",
         {"cpp17"},
         [](const FsmIr& m) {
             cpp::GeneratorOptions opts{};
             opts.cpp_standard = cpp::CppStandard::Cpp17;
             return cpp::CppGenerator::generate_header(m, opts);
         }},
    };
    return kRegistry;
}

const EmitterDescriptor* find_emitter(std::string_view format) noexcept {
    for (const auto& desc : emitter_registry()) {
        if (desc.canonical_name == format) {
            return &desc;
        }
        for (const auto& alias : desc.aliases) {
            if (alias == format) {
                return &desc;
            }
        }
    }
    return nullptr;
}

}  // namespace

std::string EmitterFactory::emit(const FsmIr& model, std::string_view format) {
    if (const auto* desc = find_emitter(format)) {
        return desc->emit_fn(model);
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
    return find_emitter(format_name) != nullptr;
}

}  // namespace fsm::backend

#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#include <emscripten/val.h>
#endif

#include <memory>
#include <sstream>
#include <string>

#include "codegen/cameo_xmi_parser.hpp"
#include "codegen/cpp_generator.hpp"
#include "codegen/dot_parser.hpp"
#include "codegen/fsm_model.hpp"
#include "codegen/fsm_validator.hpp"
#include "codegen/json_parser.hpp"
#include "codegen/mermaid_parser.hpp"
#include "codegen/mermaid_serializer.hpp"
#include "codegen/plantuml_parser.hpp"
#include "codegen/plantuml_serializer.hpp"
#include "codegen/scxml_parser.hpp"
#include "codegen/sysml2_parser.hpp"
#include "codegen/sysml2_serializer.hpp"

using namespace fsm::codegen;

namespace {

std::unique_ptr<IParser> get_parser_for_format(const std::string& fmt) {
    if (fmt == "cameo" || fmt == "xmi") {
        return std::make_unique<CameoXmiParser>();
    }
    if (fmt == "sysml" || fmt == "sysml2") {
        return std::make_unique<Sysml2Parser>();
    }
    if (fmt == "scxml") {
        return std::make_unique<ScxmlParser>();
    }
    if (fmt == "json") {
        return std::make_unique<JsonStateParser>();
    }
    if (fmt == "dot" || fmt == "gv") {
        return std::make_unique<DotParser>();
    }
    if (fmt == "plantuml" || fmt == "puml") {
        return std::make_unique<PlantUmlParser>();
    }
    return std::make_unique<MermaidParser>();
}

}  // namespace

std::string fsmc_wasm_compile(const std::string& source, const std::string& format, int standard, bool standalone) {
    auto parser = get_parser_for_format(format);
    FsmModel model;
    model.name = "WebPlaygroundFSM";
    model.ns = "fsm_playground";
    model.context_type = "PlaygroundContext";
    model.thread_safe = true;

    std::string err;
    if (!parser->parse(source, model, err)) {
        return "// [FSMC ERROR] Parsing failed:\n// " + err;
    }

    GeneratorOptions opts;
    opts.cpp_standard = (standard == 20) ? CppStandard::Cpp20 : CppStandard::Cpp17;
    opts.standalone = standalone;
    opts.include_stubs = true;
    opts.thread_safe = true;

    return CppGenerator::generate_header(model, opts);
}

std::string fsmc_wasm_export(const std::string& source, const std::string& from_format, const std::string& to_format) {
    auto parser = get_parser_for_format(from_format);
    FsmModel model;
    std::string err;
    if (!parser->parse(source, model, err)) {
        return "// [FSMC ERROR] Parsing failed: " + err;
    }

    if (to_format == "mermaid" || to_format == "mmd") {
        return MermaidSerializer::serialize(model);
    }
    if (to_format == "plantuml" || to_format == "puml") {
        return PlantUmlSerializer::serialize(model);
    }
    if (to_format == "sysml" || to_format == "sysml2") {
        return Sysml2Serializer::serialize(model);
    }

    return "// [FSMC ERROR] Unsupported target format: " + to_format;
}

std::string fsmc_wasm_verify(const std::string& source, const std::string& format) {
    auto parser = get_parser_for_format(format);
    FsmModel model;
    std::string err;
    if (!parser->parse(source, model, err)) {
        return "{\"is_valid\": false, \"errors\": [\"" + err + "\"], \"warnings\": []}";
    }

    const auto res = FsmValidator::validate(model);
    std::stringstream ss;
    ss << "{\n"
       << "  \"is_valid\": " << (res.is_valid ? "true" : "false") << ",\n"
       << "  \"state_count\": " << model.states.size() << ",\n"
       << "  \"event_count\": " << model.events.size() << ",\n"
       << "  \"transition_count\": " << model.transitions.size() << ",\n"
       << "  \"initial_state\": \"" << model.initial_state << "\",\n"
       << "  \"diagnostics\": [\n";

    for (size_t i = 0; i < res.diagnostics.size(); ++i) {
        const auto& d = res.diagnostics[i];
        std::string sev = (d.severity == DiagnosticSeverity::Error) ? "ERROR" :
                          (d.severity == DiagnosticSeverity::SafetyCritical) ? "SAFETY_CRITICAL" :
                          (d.severity == DiagnosticSeverity::Warning) ? "WARNING" : "INFO";
        ss << "    {\"severity\": \"" << sev << "\", \"category\": \"" << d.category << "\", \"message\": \"" << d.message << "\"}";
        if (i + 1 < res.diagnostics.size()) {
            ss << ",";
        }
        ss << "\n";
    }

    ss << "  ]\n}";
    return ss.str();
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_BINDINGS(fsmc_wasm_module) {
    emscripten::function("compile", &fsmc_wasm_compile);
    emscripten::function("exportDiagram", &fsmc_wasm_export);
    emscripten::function("verify", &fsmc_wasm_verify);
}
#endif

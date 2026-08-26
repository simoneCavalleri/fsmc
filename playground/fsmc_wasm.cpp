#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#include <emscripten/val.h>
#endif

#include <memory>
#include <sstream>
#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/emitter_factory.hpp"
#include "fsm/frontend/guard_parser.hpp"
#include "fsm/frontend/parser_factory.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/fsm_validator.hpp"
#include "fsm/middleend/pass_manager.hpp"

using namespace fsm;
using namespace fsm::codegen;

namespace {

bool parse_with_fallback(const std::string& source, const std::string& format, FsmIr& model, std::string& err) {
    auto parser = ParserFactory::create_by_format(format);
    if (parser != nullptr && parser->parse(source, model, err)) {
        return true;
    }
    std::string detected = ParserFactory::detect_format_from_content(source);
    if (!detected.empty() && detected != format) {
        std::string fallback_err;
        auto fallback_parser = ParserFactory::create_by_format(detected);
        if (fallback_parser != nullptr && fallback_parser->parse(source, model, fallback_err)) {
            err.clear();
            return true;
        }
    }
    if (err.empty()) {
        err = "Unknown or unsupported input format: " + format;
    }
    return false;
}

}  // namespace

std::string fsmc_wasm_compile(const std::string& source, const std::string& format, int standard, bool standalone) {
    FsmIr model;
    model.name = "WebPlaygroundFSM";
    model.ns = "fsm_playground";
    model.context_type = "no_context";
    model.thread_safe = true;

    std::string err;
    if (!parse_with_fallback(source, format, model, err)) {
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
    FsmIr model;
    std::string err;
    if (!parse_with_fallback(source, from_format, model, err)) {
        return "// [FSMC ERROR] Parsing failed: " + err;
    }

    std::string exported = EmitterFactory::emit_diagram(model, to_format);
    if (!exported.empty()) {
        return exported;
    }

    return "// [FSMC ERROR] Unsupported target format: " + to_format;
}

std::string fsmc_wasm_get_model(const std::string& source, const std::string& format) {
    FsmIr model;
    std::string err;
    if (!parse_with_fallback(source, format, model, err)) {
        return "{\"error\": \"" + err + "\"}";
    }

    std::stringstream ss;
    ss << "{\n"
       << "  \"name\": \"" << model.name << "\",\n"
       << "  \"initialState\": \"" << model.initial_state << "\",\n"
       << "  \"states\": [\n";

    for (size_t i = 0; i < model.states.size(); ++i) {
        const auto& s = model.states[i];
        ss << "    {\"name\": \"" << s.name << "\", "
           << "\"parent\": \"" << s.parent_state << "\", "
           << "\"is_composite\": " << (s.is_composite ? "true" : "false") << ", "
           << "\"initial_sub_state\": \"" << s.initial_sub_state << "\", "
           << "\"has_history\": " << (s.has_history ? "true" : "false") << ", "
           << "\"has_deep_history\": " << (s.has_deep_history ? "true" : "false") << ", "
           << "\"do_activity\": \"" << (s.do_activity.has_value() ? *s.do_activity : "") << "\", "
           << "\"entry_actions\": [";
        for (size_t j = 0; j < s.entry_actions.size(); ++j) {
            ss << "\"" << s.entry_actions[j].name << "\"" << (j + 1 < s.entry_actions.size() ? ", " : "");
        }
        ss << "], "
           << "\"exit_actions\": [";
        for (size_t j = 0; j < s.exit_actions.size(); ++j) {
            ss << "\"" << s.exit_actions[j].name << "\"" << (j + 1 < s.exit_actions.size() ? ", " : "");
        }
        ss << "], "
           << "\"deferred_events\": [";
        for (size_t j = 0; j < s.deferred_events.size(); ++j) {
            ss << "\"" << s.deferred_events[j] << "\"" << (j + 1 < s.deferred_events.size() ? ", " : "");
        }
        ss << "]}" << (i + 1 < model.states.size() ? "," : "") << "\n";
    }

    ss << "  ],\n  \"events\": [";
    for (size_t i = 0; i < model.events.size(); ++i) {
        ss << "\"" << model.events[i].name << "\"" << (i + 1 < model.events.size() ? ", " : "");
    }
    ss << "],\n  \"transitions\": [\n";

    for (size_t i = 0; i < model.transitions.size(); ++i) {
        const auto& t = model.transitions[i];
        bool is_internal = (t.kind == TransitionEdgeKind::Internal || t.source == t.target);
        std::string diagram_guard;
        if (t.guard && !t.guard->empty()) {
            diagram_guard = GuardExpressionParser::to_diagram_string(*t.guard);
        }
        ss << "    {\"source\": \"" << t.source << "\", "
           << "\"target\": \"" << t.target << "\", "
           << "\"event\": \"" << t.event << "\", "
           << "\"guard\": \"" << diagram_guard << "\", "
           << "\"action\": \"" << (t.action ? *t.action : "") << "\", "
           << "\"is_internal\": " << (is_internal ? "true" : "false") << ", "
           << "\"target_is_history\": " << (t.target_is_history ? "true" : "false") << ", "
           << "\"target_is_deep_history\": " << (t.target_is_deep_history ? "true" : "false") << "}"
           << (i + 1 < model.transitions.size() ? "," : "") << "\n";
    }

    ss << "  ]\n}";
    return ss.str();
}

std::string fsmc_wasm_verify(const std::string& source, const std::string& format) {
    FsmIr model;
    std::string err;
    if (!parse_with_fallback(source, format, model, err)) {
        return "{\"is_valid\": false, \"diagnostics\": [{\"severity\": \"ERROR\", \"category\": \"Parser\", "
               "\"message\": \"" +
               err + "\"}]}";
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
        std::string sev = (d.severity == DiagnosticSeverity::Error)            ? "ERROR"
                          : (d.severity == DiagnosticSeverity::SafetyCritical) ? "SAFETY_CRITICAL"
                          : (d.severity == DiagnosticSeverity::Warning)        ? "WARNING"
                                                                               : "INFO";
        ss << "    {\"severity\": \"" << sev << "\", \"category\": \"" << d.category << "\", \"message\": \""
           << d.message << "\"}";
        if (i + 1 < res.diagnostics.size()) {
            ss << ",";
        }
        ss << "\n";
    }

    ss << "  ]\n}";
    return ss.str();
}

std::string fsmc_wasm_optimize(const std::string& source, const std::string& format, const std::string& out_format) {
    FsmIr model;
    std::string err;
    if (!parse_with_fallback(source, format, model, err)) {
        return "// [FSMC ERROR] Parsing failed: " + err;
    }

    auto pm = PassManager::create_default_pipeline();
    DiagnosticEngine diag;
    pm.run(model, diag);

    std::string target_fmt = out_format.empty() ? format : out_format;
    std::string exported = EmitterFactory::emit_diagram(model, target_fmt);
    if (!exported.empty()) {
        return exported;
    }

    return "// [FSMC ERROR] Unsupported target format: " + target_fmt;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_BINDINGS(fsmc_wasm_module) {
    emscripten::function("compile", &fsmc_wasm_compile);
    emscripten::function("exportDiagram", &fsmc_wasm_export);
    emscripten::function("getModel", &fsmc_wasm_get_model);
    emscripten::function("verify", &fsmc_wasm_verify);
    emscripten::function("optimize", &fsmc_wasm_optimize);
}
#endif

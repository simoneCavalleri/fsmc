#ifdef __EMSCRIPTEN__
#include <emscripten/bind.h>
#include <emscripten/val.h>
#endif

#include <memory>
#include <sstream>
#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/emitter_factory.hpp"
#include "fsm/backend/rtm/rtm_emitter.hpp"
#include "fsm/backend/verification/mcdc_harness_generator.hpp"
#include "fsm/frontend/common/parser_factory.hpp"
#include "fsm/frontend/directive/guard_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/fsm_validator.hpp"
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
       << "  \"ports\": [\n";

    for (size_t i = 0; i < model.ports.size(); ++i) {
        const auto& p = model.ports[i];
        ss << "    {\"name\": \"" << p.name << "\", "
           << "\"type\": \"" << p.type << "\", "
           << "\"direction\": \"" << (p.is_in() ? "in" : "out") << "\", "
           << "\"min\": " << (p.min_value.has_value() ? std::to_string(*p.min_value) : "null") << ", "
           << "\"max\": " << (p.max_value.has_value() ? std::to_string(*p.max_value) : "null") << "}"
           << (i + 1 < model.ports.size() ? "," : "") << "\n";
    }

    ss << "  ],\n  \"variables\": [\n";
    for (size_t i = 0; i < model.variables.size(); ++i) {
        const auto& v = model.variables[i];
        ss << "    {\"name\": \"" << v.name << "\", "
           << "\"type\": \"" << v.type << "\", "
           << "\"initial\": \"" << v.initial_value << "\"}" << (i + 1 < model.variables.size() ? "," : "") << "\n";
    }

    ss << "  ],\n  \"enums\": [\n";
    for (size_t i = 0; i < model.enums.size(); ++i) {
        const auto& en = model.enums[i];
        ss << "    {\"name\": \"" << en.name << "\", \"underlying_type\": \"" << en.underlying_type
           << "\", \"literals\": [";
        for (size_t j = 0; j < en.literals.size(); ++j) {
            const auto& lit = en.literals[j];
            ss << "{\"name\": \"" << lit.name << "\"";
            if (lit.value.has_value()) {
                ss << ", \"value\": " << *lit.value;
            }
            if (!lit.description.empty()) {
                ss << ", \"description\": \"" << lit.description << "\"";
            }
            ss << "}" << (j + 1 < en.literals.size() ? ", " : "");
        }
        ss << "]}" << (i + 1 < model.enums.size() ? "," : "") << "\n";
    }
    ss << "  ],\n  \"structs\": [\n";
    for (size_t i = 0; i < model.structs.size(); ++i) {
        const auto& st = model.structs[i];
        ss << "    {\"name\": \"" << st.name << "\", \"description\": \"" << st.description << "\", \"fields\": [";
        for (size_t j = 0; j < st.fields.size(); ++j) {
            const auto& f = st.fields[j];
            ss << "{\"name\": \"" << f.name << "\", \"type\": \"" << f.type << "\", \"default_value\": \""
               << f.default_value << "\"}";
            if (j + 1 < st.fields.size())
                ss << ", ";
        }
        ss << "]}" << (i + 1 < model.structs.size() ? "," : "") << "\n";
    }
    ss << "  ],\n  \"states\": [\n";

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
    auto is_valid_ev = [](const std::string& name) {
        return !name.empty() && name != "Anonymous" && name != "AnonymousEvent" && name != "anonymous_event";
    };
    std::vector<std::string> all_events;
    std::set<std::string> seen_events;
    for (const auto& sig : model.signals) {
        if (is_valid_ev(sig.name) && seen_events.insert(sig.name).second) {
            all_events.push_back(sig.name);
        }
    }
    for (const auto& ev : model.events) {
        if (is_valid_ev(ev.name) && seen_events.insert(ev.name).second) {
            all_events.push_back(ev.name);
        }
    }
    for (const auto& t : model.transitions) {
        if (is_valid_ev(t.event) && seen_events.insert(t.event).second) {
            all_events.push_back(t.event);
        }
    }
    for (const auto& s : model.states) {
        for (const auto& d : s.deferred_events) {
            if (is_valid_ev(d) && seen_events.insert(d).second) {
                all_events.push_back(d);
            }
        }
    }
    for (size_t i = 0; i < all_events.size(); ++i) {
        ss << "\"" << all_events[i] << "\"" << (i + 1 < all_events.size() ? ", " : "");
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
    DiagnosticEngine diag;
    EFSMIntervalAnalyzer interval_analyzer(model);
    interval_analyzer.analyze(diag);

    bool overall_valid = res.is_valid && !diag.has_errors();

    std::stringstream ss;
    ss << "{\n"
       << "  \"is_valid\": " << (overall_valid ? "true" : "false") << ",\n"
       << "  \"port_count\": " << model.ports.size() << ",\n"
       << "  \"variable_count\": " << model.variables.size() << ",\n"
       << "  \"state_count\": " << model.states.size() << ",\n"
       << "  \"event_count\": " << model.events.size() << ",\n"
       << "  \"transition_count\": " << model.transitions.size() << ",\n"
       << "  \"initial_state\": \"" << model.initial_state << "\",\n"
       << "  \"diagnostics\": [\n";

    bool has_diag = false;
    for (size_t i = 0; i < res.diagnostics.size(); ++i) {
        const auto& d = res.diagnostics[i];
        std::string sev = (d.severity == DiagnosticSeverity::Error)            ? "ERROR"
                          : (d.severity == DiagnosticSeverity::SafetyCritical) ? "SAFETY_CRITICAL"
                          : (d.severity == DiagnosticSeverity::Warning)        ? "WARNING"
                                                                               : "INFO";
        if (has_diag)
            ss << ",\n";
        ss << "    {\"severity\": \"" << sev << "\", \"category\": \"" << d.category << "\", \"message\": \""
           << d.message << "\"}";
        has_diag = true;
    }

    for (const auto& d : diag.get_diagnostics()) {
        std::string sev = (d.severity == DiagnosticSeverity::Error)            ? "ERROR"
                          : (d.severity == DiagnosticSeverity::SafetyCritical) ? "SAFETY_CRITICAL"
                          : (d.severity == DiagnosticSeverity::Warning)        ? "WARNING"
                                                                               : "INFO";
        if (has_diag)
            ss << ",\n";
        ss << "    {\"severity\": \"" << sev << "\", \"category\": \"IntervalAnalysis\", \"message\": \"" << d.message
           << "\"}";
        has_diag = true;
    }

    ss << "\n  ]\n}";
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

std::string fsmc_wasm_generate_mcdc(const std::string& source, const std::string& format) {
    FsmIr model;
    std::string err;
    if (!parse_with_fallback(source, format, model, err)) {
        return "// [FSMC ERROR] Parsing failed:\n// " + err;
    }
    return McdcHarnessGenerator::generate_gtest_harness(model);
}

std::string fsmc_wasm_audit_rtm(const std::string& source, const std::string& format) {
    FsmIr model;
    std::string err;
    if (!parse_with_fallback(source, format, model, err)) {
        return "{\"is_compliant\": false, \"untraced_states\": [\"" + err + "\"]}";
    }
    DiagnosticEngine diag;
    return RtmEmitter::audit_traceability(model, diag);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_BINDINGS(fsmc_wasm_module) {
    emscripten::function("compile", &fsmc_wasm_compile);
    emscripten::function("exportDiagram", &fsmc_wasm_export);
    emscripten::function("getModel", &fsmc_wasm_get_model);
    emscripten::function("verify", &fsmc_wasm_verify);
    emscripten::function("optimize", &fsmc_wasm_optimize);
    emscripten::function("generateMcdc", &fsmc_wasm_generate_mcdc);
    emscripten::function("auditRtm", &fsmc_wasm_audit_rtm);
}
#endif

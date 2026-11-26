#include "fsm/backend/cpp/cpp_generator.hpp"

#include <cctype>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include "fsm/backend/cpp/cpp_backend_validator.hpp"
#include "fsm/backend/cpp/cpp_model_emitter.hpp"
#include "fsm/backend/cpp/cpp_standalone_runtime.hpp"
#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"

namespace fsm::backend::cpp {

namespace {

std::string sanitize_comment(std::string_view text) {
    std::string res;
    res.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '*' && i + 1 < text.size() && text[i + 1] == '/') {
            res += "* /";
            ++i;
        } else if (text[i] == '\r' || text[i] == '\n') {
            res.push_back(' ');
        } else {
            res.push_back(text[i]);
        }
    }
    return res;
}

std::string sanitize_namespace(std::string_view raw_ns) {
    if (raw_ns.empty()) {
        return {};
    }
    std::string res;
    std::size_t pos = 0;
    while (pos < raw_ns.size()) {
        const std::size_t sep = raw_ns.find("::", pos);
        const std::string_view part =
            (sep == std::string_view::npos) ? raw_ns.substr(pos) : raw_ns.substr(pos, sep - pos);
        if (!part.empty()) {
            std::string clean_part;
            clean_part.reserve(part.size() + 1);
            for (char ch : part) {
                const unsigned char uch = static_cast<unsigned char>(ch);
                if (std::isalnum(uch) != 0 || ch == '_') {
                    clean_part.push_back(ch);
                } else {
                    clean_part.push_back('_');
                }
            }
            if (!clean_part.empty() && std::isdigit(static_cast<unsigned char>(clean_part.front())) != 0) {
                clean_part.insert(clean_part.begin(), '_');
            }
            if (!res.empty()) {
                res += "::";
            }
            res += clean_part;
        }
        if (sep == std::string_view::npos) {
            break;
        }
        pos = sep + 2;
    }
    return res;
}

void emit_runtime_preamble(std::ostream& out, const GeneratorOptions& effective_opts) {
    if (effective_opts.standalone) {
        out << "#include <array>\n";
        out << "#include <cstddef>\n";
        out << "#include <cstdint>\n";
        if (!effective_opts.freestanding) {
            out << "#include <string>\n";
        }
        out << "#include <string_view>\n";
        out << "#include <type_traits>\n";
        out << "#include <utility>\n\n";
        out << "#ifndef FSMC_EMBEDDED_RUNTIME_INCLUDED\n";
        out << "#define FSMC_EMBEDDED_RUNTIME_INCLUDED\n";
        CppStandaloneRuntime::emit(out, effective_opts);
        out << "#endif // FSMC_EMBEDDED_RUNTIME_INCLUDED\n\n";
    } else {
        std::string header_include = effective_opts.runtime_header;
        if (header_include.empty()) {
            header_include = "fsm.hpp";
        }
        if (header_include.front() == '<' || header_include.front() == '"') {
            out << "#include " << header_include << "\n";
        } else {
            out << "#include \"" << header_include << "\"\n";
        }
        out << "#include <array>\n";
        out << "#include <cstddef>\n";
        out << "#include <cstdint>\n";
        out << "#include <string_view>\n";
        out << "#include <type_traits>\n";
        out << "#include <utility>\n\n";
    }
}

void fuse_orthogonal_skip_actions(ir::FsmIr& model) {
    auto append_act = [](ir::ActionSignature& dst, const ir::ActionSignature& src) {
        const bool has_structured = !src.instructions.empty() || !src.assignments.empty();
        for (const auto& inst : src.instructions) {
            dst.instructions.push_back(inst);
        }
        for (const auto& a : src.assignments) {
            dst.assignments.push_back(a);
        }
        if (!has_structured && (!src.invocation.empty() || !src.name.empty())) {
            ir::ActionCallOp call;
            call.function_name = !src.invocation.empty() ? src.invocation : src.name;
            dst.instructions.emplace_back(std::move(call), "Fused callback " + src.name);
        }
    };

    std::unordered_set<std::string> used_names;
    for (const auto& a : model.actions) {
        used_names.insert(a.name);
    }

    std::size_t fused_idx = 0U;
    for (auto& t : model.transitions) {
        if (t.kind == ir::TransitionEdgeKind::Internal ||
            (t.skip_exit_actions.empty() && t.skip_entry_actions.empty())) {
            continue;
        }
        const auto* src_st = model.find_state(t.source);
        const auto* dst_st = model.find_state(t.target);
        if (src_st == nullptr || dst_st == nullptr || src_st->parent_state != dst_st->parent_state) {
            continue;
        }
        const std::unordered_set<std::string> skip_exit(t.skip_exit_actions.begin(), t.skip_exit_actions.end());
        const std::unordered_set<std::string> skip_entry(t.skip_entry_actions.begin(), t.skip_entry_actions.end());

        std::string base_name = "fused_" + t.source + "_to_" + t.target;
        if (t.transition_action.has_value() && !t.transition_action->name.empty()) {
            base_name += "_" + t.transition_action->name;
        }
        std::string unique_name = base_name;
        while (used_names.count(unique_name) != 0U) {
            unique_name = base_name + "_" + std::to_string(++fused_idx);
        }
        used_names.insert(unique_name);

        ir::ActionSignature fused(unique_name);
        for (const auto& act : src_st->exit_actions) {
            if (!act.name.empty() && skip_exit.count(act.name) != 0U) {
                continue;
            }
            append_act(fused, act);
        }
        if (t.transition_action.has_value()) {
            append_act(fused, *t.transition_action);
        }
        for (const auto& act : dst_st->entry_actions) {
            if (!act.name.empty() && skip_entry.count(act.name) != 0U) {
                continue;
            }
            append_act(fused, act);
        }

        if (!fused.assignments.empty() || !fused.instructions.empty()) {
            model.add_action(fused.name);
            t.transition_action = std::move(fused);
        } else {
            t.transition_action.reset();
        }
    }
}

}  // namespace

std::string CppGenerator::generate_header(const FsmIr& model, const GeneratorOptions& options) {
    FsmIr processed_model = model;
    processed_model.canonicalize();
    fuse_orthogonal_skip_actions(processed_model);

    diagnostic::DiagnosticEngine backend_diagnostics;
    if (!CppBackendValidator::validate_model(processed_model, backend_diagnostics)) {
        throw std::invalid_argument(backend_diagnostics.render_to_string());
    }

    std::ostringstream out;
    const std::string safe_model_comment = sanitize_comment(processed_model.name);
    const std::string ir_sha256 = ir::FsmIrSerializer::compute_sha256(processed_model);

    GeneratorOptions effective_opts = options;
    if (effective_opts.freestanding) {
        effective_opts.thread_safe = false;
    }

    out << "/**\n";
    out << " * @file " << safe_model_comment << ".hpp\n";
    out << " * @brief Auto-generated State Machine definition for '" << safe_model_comment << "'\n";
    out << " * @note Generated by fsmc (Finite State Machine Compiler). Target standard: C++17.\n";
    out << " * @note IR-SHA256: " << ir_sha256 << "\n";
    if (effective_opts.standalone) {
        out << " * @note Mode: Standalone Self-Contained Header (Zero External Dependencies)\n";
    }
    if (effective_opts.freestanding) {
        out << " * @note Profile: Freestanding / Zero-STL (-fno-exceptions -fno-rtti compatible)\n";
    }
    out << " */\n\n";

    out << "#pragma once\n\n";

    if (effective_opts.freestanding) {
        out << "#ifndef FSMC_FREESTANDING\n";
        out << "#define FSMC_FREESTANDING 1\n";
        out << "#endif\n\n";
    }

    emit_runtime_preamble(out, effective_opts);
    CppModelEmitter::emit_external_includes(out, processed_model);

    std::string effective_ns = effective_opts.target_namespace;
    if (effective_ns.empty()) {
        effective_ns = !processed_model.package.empty() ? processed_model.package : "fsm_generated";
    }
    effective_ns = sanitize_namespace(effective_ns);

    if (!effective_ns.empty()) {
        out << "namespace " << effective_ns << " {\n\n";
    }

    out << "/** @brief Deterministic SHA-256 digest of the canonical FsmIr specification. */\n";
    out << "inline constexpr std::string_view " << sanitize_namespace(processed_model.name) << "IrSha256 = \""
        << ir_sha256 << "\";\n\n";

    CppModelEmitter::emit_model(out, processed_model, effective_opts);

    if (!effective_ns.empty()) {
        out << "\n} // namespace " << effective_ns << "\n";
    }

    return out.str();
}

GeneratedCppBundle CppGenerator::generate_bundle(const FsmIr& model, const GeneratorOptions& options,
                                                 std::string_view ports_header_include) {
    FsmIr processed_model = model;
    processed_model.canonicalize();
    fuse_orthogonal_skip_actions(processed_model);

    diagnostic::DiagnosticEngine backend_diagnostics;
    if (!CppBackendValidator::validate_model(processed_model, backend_diagnostics)) {
        throw std::invalid_argument(backend_diagnostics.render_to_string());
    }

    const std::string safe_model_comment = sanitize_comment(processed_model.name);
    const std::string ir_sha256 = ir::FsmIrSerializer::compute_sha256(processed_model);

    GeneratorOptions effective_opts = options;
    if (effective_opts.freestanding) {
        effective_opts.thread_safe = false;
    }

    std::string effective_ns = effective_opts.target_namespace;
    if (effective_ns.empty()) {
        effective_ns = !processed_model.package.empty() ? processed_model.package : "fsm_generated";
    }
    effective_ns = sanitize_namespace(effective_ns);

    const std::string ports_inc =
        ports_header_include.empty() ? (processed_model.name + "_ports.hpp") : std::string(ports_header_include);

    // 1. Pure interface header (*_ports.hpp) — zero dependency on fsm.hpp
    std::ostringstream ports_out;
    ports_out << "/**\n";
    ports_out << " * @file " << sanitize_comment(ports_inc) << "\n";
    ports_out << " * @brief Auto-generated I/O Ports, Types, Events & Services Interface for '" << safe_model_comment
              << "'\n";
    ports_out << " * @note Generated by fsmc (Finite State Machine Compiler). Zero fsm.hpp dependency.\n";
    if (effective_opts.freestanding) {
        ports_out << " * @note Profile: Freestanding / Zero-STL (-fno-exceptions -fno-rtti compatible)\n";
    }
    ports_out << " */\n\n";
    ports_out << "#pragma once\n\n";
    ports_out << "#include <array>\n";
    ports_out << "#include <cstddef>\n";
    ports_out << "#include <cstdint>\n";
    if (!effective_opts.freestanding) {
        ports_out << "#include <string>\n";
    }
    ports_out << "#include <string_view>\n";
    ports_out << "#include <type_traits>\n";
    ports_out << "#include <utility>\n\n";

    CppModelEmitter::emit_external_includes(ports_out, processed_model);

    if (!effective_ns.empty()) {
        ports_out << "namespace " << effective_ns << " {\n\n";
    }
    CppModelEmitter::emit_ports_interface(ports_out, processed_model);
    if (!effective_ns.empty()) {
        ports_out << "} // namespace " << effective_ns << "\n";
    }

    // 2. State machine engine header (*_fsm.hpp)
    std::ostringstream fsm_out;
    fsm_out << "/**\n";
    fsm_out << " * @file " << safe_model_comment << "_fsm.hpp\n";
    fsm_out << " * @brief Auto-generated State Machine Engine for '" << safe_model_comment << "'\n";
    fsm_out << " * @note Generated by fsmc (Finite State Machine Compiler). Target standard: C++17.\n";
    fsm_out << " * @note IR-SHA256: " << ir_sha256 << "\n";
    if (effective_opts.freestanding) {
        fsm_out << " * @note Profile: Freestanding / Zero-STL (-fno-exceptions -fno-rtti compatible)\n";
    }
    fsm_out << " */\n\n";
    fsm_out << "#pragma once\n\n";

    if (effective_opts.freestanding) {
        fsm_out << "#ifndef FSMC_FREESTANDING\n";
        fsm_out << "#define FSMC_FREESTANDING 1\n";
        fsm_out << "#endif\n\n";
    }

    if (!ports_inc.empty() && (ports_inc.front() == '<' || ports_inc.front() == '"')) {
        fsm_out << "#include " << ports_inc << "\n";
    } else {
        fsm_out << "#include \"" << ports_inc << "\"\n";
    }
    emit_runtime_preamble(fsm_out, effective_opts);

    if (!effective_ns.empty()) {
        fsm_out << "namespace " << effective_ns << " {\n\n";
    }

    fsm_out << "/** @brief Deterministic SHA-256 digest of the canonical FsmIr specification. */\n";
    fsm_out << "inline constexpr std::string_view " << sanitize_namespace(processed_model.name) << "IrSha256 = \""
            << ir_sha256 << "\";\n\n";

    CppModelEmitter::emit_fsm_engine(fsm_out, processed_model, effective_opts);
    if (!effective_ns.empty()) {
        fsm_out << "\n} // namespace " << effective_ns << "\n";
    }

    return GeneratedCppBundle{ports_out.str(), fsm_out.str()};
}

}  // namespace fsm::backend::cpp

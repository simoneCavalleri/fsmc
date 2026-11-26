#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include "fsm/backend/c99/c99_generator.hpp"
#include "fsm/backend/codegen_backend.hpp"
#include "fsm/backend/cpp/c_abi_generator.hpp"
#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/cpp/runtime_exporter.hpp"
#include "fsm/backend/diagram/companion_manifest_emitter.hpp"
#include "fsm/backend/emitter_factory.hpp"
#include "fsm/backend/manifest_emitter.hpp"
#include "fsm/backend/rtm/rtm_emitter.hpp"
#include "fsm/backend/verification/mcdc_harness_generator.hpp"
#include "fsm/ir/ir_simulator.hpp"
#include "fsm/middleend/analysis/event_queue_bound_pass.hpp"
#include "fsm/middleend/analysis/model_checker.hpp"
#include "fsm/middleend/passes/wcet_analysis_pass.hpp"
#include "tools/common/file_utils.hpp"
#include "tools/fsmc/driver_internal.hpp"

namespace fsm::tools::driver_detail {

using namespace fsm::ir;
using namespace fsm::diagnostic;
using namespace fsm::frontend;
using namespace fsm::middleend;
using namespace fsm::middleend::analysis;
using namespace fsm::middleend::passes;
using namespace fsm::backend;
using namespace fsm::backend::cpp;
using namespace fsm::backend::diagram;
using namespace fsm::backend::rtm;

std::optional<int> handle_auxiliary_modes(const FsmcOptions& opts, const fsm::ir::FsmIr& model,
                                          const DiagnosticEngine& middleend_diag) {
    if (opts.simulate_mode) {
        std::string trace_content;
        if (!opts.trace_file.empty()) {
            std::string trace_read_err;
            trace_content = read_file_content(opts.trace_file, trace_read_err);
            if (!trace_read_err.empty()) {
                std::cerr << "Error: " << trace_read_err << "\n";
                return 1;
            }
        } else {
            std::ostringstream ss;
            ss << std::cin.rdbuf();
            trace_content = ss.str();
        }
        SimulationOptions sim_opts;
        sim_opts.strict_guards = opts.strict_determinism;
        FsmIr sim_model = model;
        {
            PassManager sim_pm;
            sim_pm.add_pass(PassManager::create_pass_by_name("connective-junction-chaining"));
            sim_pm.add_pass(PassManager::create_pass_by_name("choice-inlining"));
            sim_pm.add_pass(PassManager::create_pass_by_name("fork-join-lowering"));
            sim_pm.add_pass(PassManager::create_pass_by_name("orthogonal-product"));
            DiagnosticEngine sim_diag;
            sim_pm.run(sim_model, sim_diag);
        }
        auto report = IrSimulator::simulate_detailed(sim_model, trace_content, std::cout, sim_opts);
        if (!opts.trace_out_file.empty()) {
            std::string write_err;
            if (!write_file_content(opts.trace_out_file, report.to_json(), write_err)) {
                std::cerr << "Error: " << write_err << "\n";
                return 1;
            }
        }
        return 0;
    }

    if (opts.verify_mode) {
        if (!opts.output_file.empty()) {
            std::cerr << "note: '-o " << opts.output_file
                      << "' is ignored in '--verify' mode (verification does not emit code).\n";
        }
        if (opts.verify_engine == "nuxmv" || (opts.verify_engine == "auto" && is_nuxmv_available())) {
            return run_nuxmv_verification(opts, model, middleend_diag);
        }
        return print_verification_report(opts, model, middleend_diag);
    }

    if (middleend_diag.has_errors()) {
        return 1;
    }

    // RTM Export
    if (!opts.rtm_output_file.empty()) {
        ModelChecker checker(model);
        auto mc_results = checker.verify_all();
        RtmFormat rtm_fmt = RtmFormat::Markdown;
        if (!opts.rtm_format.empty()) {
            rtm_fmt = rtm_format_from_string(opts.rtm_format);
        } else if (ends_with(opts.rtm_output_file, ".json")) {
            rtm_fmt = RtmFormat::Json;
        }

        std::string rtm_content = RtmEmitter::emit(model, mc_results, rtm_fmt);
        std::string write_err;
        if (!write_file_content(opts.rtm_output_file, rtm_content, write_err)) {
            std::cerr << "Error: " << write_err << "\n";
            return 1;
        }
        std::cerr << "[SUCCESS] Requirement Traceability Matrix exported to: " << opts.rtm_output_file << "\n";
        if (opts.output_file.empty() && opts.emit_sidecar.empty() && opts.emit_test_harness.empty() &&
            opts.export_diagram_format.empty()) {
            return 0;
        }
    }

    // Diagram Export
    if (!opts.export_diagram_format.empty()) {
        std::string exported_diagram = EmitterFactory::emit_diagram(model, opts.export_diagram_format);
        if (exported_diagram.empty()) {
            std::cerr << "Error: Unsupported export diagram format: '" << opts.export_diagram_format
                      << "'. Supported: mermaid, plantuml, sysml2, json, dot, scxml, cameo, smv\n";
            return 1;
        }

        if (!opts.output_file.empty()) {
            std::string write_err;
            if (!write_file_content(opts.output_file, exported_diagram, write_err)) {
                std::cerr << "Error: " << write_err << "\n";
                return 1;
            }
            std::cerr << "[SUCCESS] Diagram exported to " << opts.export_diagram_format << ": " << opts.output_file
                      << "\n";
        } else {
            std::cout << exported_diagram;
        }

        if (!opts.emit_sidecar.empty()) {
            std::string sidecar_content;
            if (ends_with(opts.emit_sidecar, ".json")) {
                sidecar_content = CompanionManifestEmitter::emit_json(model);
            } else {
                sidecar_content = CompanionManifestEmitter::emit_yaml(model);
            }
            std::string write_err;
            if (!write_file_content(opts.emit_sidecar, sidecar_content, write_err)) {
                std::cerr << "Error writing sidecar manifest to '" << opts.emit_sidecar << "': " << write_err << "\n";
                return 1;
            }
            std::cerr << "[SUCCESS] Companion manifest sidecar exported to: " << opts.emit_sidecar << "\n";
        }
        return 0;
    }

    // Standalone Sidecar Manifest Export
    if (!opts.emit_sidecar.empty()) {
        std::string sidecar_content;
        if (ends_with(opts.emit_sidecar, ".json")) {
            sidecar_content = CompanionManifestEmitter::emit_json(model);
        } else {
            sidecar_content = CompanionManifestEmitter::emit_yaml(model);
        }
        std::string write_err;
        if (!write_file_content(opts.emit_sidecar, sidecar_content, write_err)) {
            std::cerr << "Error writing sidecar manifest to '" << opts.emit_sidecar << "': " << write_err << "\n";
            return 1;
        }
        std::cerr << "[SUCCESS] Companion manifest sidecar exported to: " << opts.emit_sidecar << "\n";
        if (opts.output_file.empty() && opts.emit_test_harness.empty() && opts.rtm_output_file.empty()) {
            return 0;
        }
    }

    // MC/DC Test Harness Synthesis
    if (!opts.emit_test_harness.empty()) {
        std::string harness = fsm::backend::verification::McdcHarnessGenerator::generate_gtest_harness(model);
        std::string write_err;
        if (!write_file_content(opts.emit_test_harness, harness, write_err)) {
            std::cerr << "Error writing MC/DC test harness to '" << opts.emit_test_harness << "': " << write_err
                      << "\n";
            return 1;
        }
        std::cerr << "[SUCCESS] MC/DC Test Harness synthesized successfully to: " << opts.emit_test_harness << "\n";
        if (opts.output_file.empty() && opts.export_diagram_format.empty()) {
            return 0;
        }
    }

    if (opts.output_file.empty() &&
        (!opts.rtm_output_file.empty() || !opts.emit_sidecar.empty() || !opts.emit_test_harness.empty())) {
        return 0;
    }
    return std::nullopt;
}

int emit_cpp_target(const FsmcOptions& opts, const std::string& content, fsm::ir::FsmIr& model, const IParser& parser,
                    bool has_sidecar, DiagnosticFormat diag_fmt) {
    // Enforce untyped diagram codegen safety check before C++ generation
    if (parser.kind() == FrontendKind::Diagram && !has_sidecar && !opts.allow_diagram_codegen) {
        std::cerr << "\n[ERROR] Direct code generation blocked: '" << opts.input_file
                  << "' is a visual diagram format (" << parser.format_name()
                  << ").\nPass '--sidecar <manifest.yaml>' or '--allow-diagram' to allow code "
                     "generation, or use '--verify' / '--export <fmt>' / '--rtm <file>'.\n";
        return 1;
    }

    // Target-required C++ structural lowering (executed in driver/middleend layer, not inside fsmc_backend)
    {
        PassManager lowering_pm;
        lowering_pm.add_pass(PassManager::create_pass_by_name("connective-junction-chaining"));
        lowering_pm.add_pass(PassManager::create_pass_by_name("choice-inlining"));
        lowering_pm.add_pass(PassManager::create_pass_by_name("fork-join-lowering"));
        lowering_pm.add_pass(PassManager::create_pass_by_name("orthogonal-product"));
        DiagnosticEngine lowering_diag;
        if (!lowering_pm.run(model, lowering_diag)) {
            std::cerr << lowering_diag.render_to_format(diag_fmt, content);
            return 1;
        }
    }

    // Native Pure-C99 Bare-Metal Backend (--target c99)
    if (opts.target_lang == "c99" || opts.target_lang == "c") {
        DiagnosticEngine lower_diag;
        ir::FsmIr c99_model = model;
        auto lower_pm = middleend::PassManager::create_lowering_pipeline(ir::TargetCapabilities::c99_baremetal());
        if (!lower_pm.run(c99_model, lower_diag) || lower_diag.has_errors()) {
            std::cerr << lower_diag.render_to_format(diag_fmt, content);
            return 1;
        }

        const bool is_null_device = (opts.output_file == "/dev/null" || opts.output_file == "NUL");
        if (!opts.output_file.empty() && !is_stdout_path(opts.output_file) && !is_null_device) {
            const fs::path out_path(opts.output_file);
            const std::string stem = out_path.stem().string();
            const fs::path c_hdr_path = out_path.parent_path() / (stem + ".h");
            const fs::path c_src_path = out_path.parent_path() / (stem + ".c");

            backend::c99::C99GeneratorOptions c99_opts;
            c99_opts.header_include = c_hdr_path.filename().string();
            backend::C99CodegenBackend c99_backend(c99_opts);
            DiagnosticEngine c99_diag;
            const auto bundle = c99_backend.emit_bundle(c99_model, c99_diag);
            if (c99_diag.has_errors()) {
                std::cerr << c99_diag.render_to_format(diag_fmt, content);
                return 1;
            }

            std::string write_err;
            if (!write_file_content(c_hdr_path.string(), bundle.header, write_err) ||
                !write_file_content(c_src_path.string(), bundle.source, write_err)) {
                std::cerr << "Error: " << write_err << "\n";
                return 1;
            }
            std::cout << "[SUCCESS] Native Pure-C99 generated successfully:\n"
                      << "  - Header: " << c_hdr_path.string() << "\n"
                      << "  - Source: " << c_src_path.string() << "\n"
                      << "  - States: " << c99_model.states.size() << "\n"
                      << "  - Events: " << c99_model.signals.size() << "\n"
                      << "  - Transitions: " << c99_model.transitions.size() << "\n";

            if (opts.emit_manifest) {
                const fs::path manifest_path = opts.emit_manifest_file.empty()
                                                   ? (out_path.parent_path() / (stem + ".manifest.json"))
                                                   : fs::path(opts.emit_manifest_file);
                ManifestArtifacts manifest_artifacts;
                manifest_artifacts.target = "c99";
                manifest_artifacts.standalone = true;
                manifest_artifacts.c_header = c_hdr_path.filename().string();
                manifest_artifacts.c_source = c_src_path.filename().string();

                ManifestVerificationSummary ver_summary;
                ver_summary.verified = true;
                ver_summary.properties_checked = c99_model.properties.size();

                ir::FsmIr analysis_copy = c99_model;
                DiagnosticEngine sched_diag;
                middleend::passes::WcetAnalysisPass wcet_pass;
                wcet_pass.run(analysis_copy, sched_diag);
                ver_summary.wcet_max_microsteps = wcet_pass.max_micro_steps();
                ver_summary.zeno_free = !wcet_pass.has_zeno_cycle();

                middleend::analysis::EventQueueBoundPass queue_pass;
                queue_pass.run(analysis_copy, sched_diag);
                if (auto it = analysis_copy.attributes.find("static_event_queue_capacity");
                    it != analysis_copy.attributes.end()) {
                    try {
                        ver_summary.max_event_queue_bound = static_cast<std::size_t>(std::stoull(it->second));
                    } catch (...) {
                    }
                }

                const std::string manifest_json =
                    ManifestEmitter::emit_json(c99_model, manifest_artifacts, ver_summary);
                if (!write_file_content(manifest_path.string(), manifest_json, write_err)) {
                    std::cerr << "Error: " << write_err << "\n";
                    return 1;
                }
                std::cout << "[SUCCESS] System manifest generated successfully to: " << manifest_path.string() << "\n";
            }
        } else {
            backend::C99CodegenBackend c99_backend;
            DiagnosticEngine c99_diag;
            const std::string out_code = c99_backend.emit(c99_model, c99_diag);
            if (c99_diag.has_errors()) {
                std::cerr << c99_diag.render_to_format(diag_fmt, content);
                return 1;
            }
            if (is_null_device) {
                std::string write_err;
                if (!write_file_content(opts.output_file, out_code, write_err)) {
                    std::cerr << "Error: " << write_err << "\n";
                    return 1;
                }
            } else {
                std::cout << out_code;
            }
        }
        return 0;
    }

    // Generate C++17 code
    GeneratorOptions gen_opts;
    gen_opts.standalone = opts.standalone;
    gen_opts.include_stubs = opts.include_stubs;
    gen_opts.thread_safe = opts.thread_safe;
    gen_opts.freestanding = opts.freestanding;
    gen_opts.target_namespace = opts.ns_name;
    gen_opts.runtime_header = opts.runtime_header;

    if (!opts.output_file.empty() && !is_stdout_path(opts.output_file)) {
        const bool is_null_device = (opts.output_file == "/dev/null" || opts.output_file == "NUL");
        const bool use_split = !opts.standalone_specified && !is_null_device;
        const fs::path fsm_path(opts.output_file);
        std::string stem = fsm_path.stem().string();
        const std::string ext = fsm_path.extension().string();
        if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, "_fsm") == 0) {
            stem.erase(stem.size() - 4);
        }

        ManifestArtifacts manifest_artifacts;
        manifest_artifacts.standalone = !use_split;
        manifest_artifacts.fsm_header = fsm_path.filename().string();

        if (use_split) {
            const fs::path ports_path = fsm_path.parent_path() / (stem + "_ports" + (ext.empty() ? ".hpp" : ext));
            const std::string ports_inc = ports_path.filename().string();
            manifest_artifacts.ports_header = ports_inc;

            GeneratedCppBundle bundle;
            try {
                bundle = CppGenerator::generate_bundle(model, gen_opts, ports_inc);
            } catch (const std::exception& ex) {
                std::cerr << "Error: C++ backend rejected the model: " << ex.what() << "\n";
                return 1;
            }

            std::string write_err;
            if (!write_file_content(ports_path.string(), bundle.ports_header, write_err)) {
                std::cerr << "Error: " << write_err << "\n";
                return 1;
            }
            if (!write_file_content(opts.output_file, bundle.fsm_header, write_err)) {
                std::cerr << "Error: " << write_err << "\n";
                return 1;
            }
            std::cout << "[SUCCESS] C++17 (Split" << (opts.standalone ? "" : " + Modular")
                      << ") generated successfully:\n"
                      << "  - Ports: " << ports_path.string() << "\n"
                      << "  - FSM:   " << opts.output_file << "\n"
                      << "  - States: " << model.states.size() << "\n"
                      << "  - Events: " << model.signals.size() << "\n"
                      << "  - Transitions: " << model.transitions.size() << "\n"
                      << "  - Guards: " << model.guards.size() << "\n"
                      << "  - Actions: " << model.actions.size() << "\n";
        } else {
            std::string generated_code;
            try {
                generated_code = CppGenerator::generate_header(model, gen_opts);
            } catch (const std::exception& ex) {
                std::cerr << "Error: C++ backend rejected the model: " << ex.what() << "\n";
                return 1;
            }

            std::string write_err;
            if (!write_file_content(opts.output_file, generated_code, write_err)) {
                std::cerr << "Error: " << write_err << "\n";
                return 1;
            }
            std::cout << "[SUCCESS] C++17 (" << (opts.standalone ? "Standalone" : "Modular")
                      << ") generated successfully to: " << opts.output_file << "\n"
                      << "  - States: " << model.states.size() << "\n"
                      << "  - Events: " << model.signals.size() << "\n"
                      << "  - Transitions: " << model.transitions.size() << "\n"
                      << "  - Guards: " << model.guards.size() << "\n"
                      << "  - Actions: " << model.actions.size() << "\n";
        }

        if (opts.emit_c_abi) {
            const fs::path c_hdr_path = fsm_path.parent_path() / (stem + "_c.h");
            const fs::path c_src_path = fsm_path.parent_path() / (stem + "_c.cpp");
            GeneratedCAbiBundle c_bundle;
            try {
                c_bundle = CAbiGenerator::generate(model, gen_opts, fsm_path.filename().string(),
                                                   c_hdr_path.filename().string());
            } catch (const std::exception& ex) {
                std::cerr << "Error: C ABI generator rejected the model: " << ex.what() << "\n";
                return 1;
            }
            std::string write_err;
            if (!write_file_content(c_hdr_path.string(), c_bundle.header, write_err) ||
                !write_file_content(c_src_path.string(), c_bundle.source, write_err)) {
                std::cerr << "Error: " << write_err << "\n";
                return 1;
            }
            manifest_artifacts.c_header = c_hdr_path.filename().string();
            manifest_artifacts.c_source = c_src_path.filename().string();
            std::cout << "[SUCCESS] C99 ABI generated successfully:\n"
                      << "  - Header: " << c_hdr_path.string() << "\n"
                      << "  - Bridge: " << c_src_path.string() << "\n";
        }

        if (opts.emit_manifest) {
            const fs::path manifest_path = opts.emit_manifest_file.empty()
                                               ? (fsm_path.parent_path() / (stem + ".manifest.json"))
                                               : fs::path(opts.emit_manifest_file);
            ManifestVerificationSummary ver_summary;
            ver_summary.verified = true;
            ver_summary.properties_checked = model.properties.size();

            ir::FsmIr analysis_copy = model;
            diagnostic::DiagnosticEngine sched_diag;
            middleend::passes::WcetAnalysisPass wcet_pass;
            wcet_pass.run(analysis_copy, sched_diag);
            ver_summary.wcet_max_microsteps = wcet_pass.max_micro_steps();
            ver_summary.zeno_free = !wcet_pass.has_zeno_cycle();

            middleend::analysis::EventQueueBoundPass queue_pass;
            queue_pass.run(analysis_copy, sched_diag);
            if (auto it = analysis_copy.attributes.find("static_event_queue_capacity");
                it != analysis_copy.attributes.end()) {
                try {
                    ver_summary.max_event_queue_bound = static_cast<std::size_t>(std::stoull(it->second));
                } catch (...) {
                }
            }

            const std::string manifest_json = ManifestEmitter::emit_json(model, manifest_artifacts, ver_summary);
            std::string write_err;
            if (!write_file_content(manifest_path.string(), manifest_json, write_err)) {
                std::cerr << "Error: " << write_err << "\n";
                return 1;
            }
            std::cout << "[SUCCESS] System manifest generated successfully to: " << manifest_path.string() << "\n";
        }
    } else {
        std::string generated_code;
        try {
            generated_code = CppGenerator::generate_header(model, gen_opts);
        } catch (const std::exception& ex) {
            std::cerr << "Error: C++ backend rejected the model: " << ex.what() << "\n";
            return 1;
        }
        std::cout << generated_code;
    }

    return 0;
}

}  // namespace fsm::tools::driver_detail

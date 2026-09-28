#pragma once

#include <chrono>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/cpp/runtime_exporter.hpp"
#include "fsm/backend/diagram/companion_manifest_emitter.hpp"
#include "fsm/backend/emitter_factory.hpp"
#include "fsm/backend/formal/smv_serializer.hpp"
#include "fsm/backend/rtm/rtm_emitter.hpp"
#include "fsm/backend/verification/mcdc_harness_generator.hpp"
#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/frontend/common/companion_manifest_parser.hpp"
#include "fsm/frontend/common/parser_factory.hpp"
#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/frontend/diagram/diagram_contract_combiner.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/fsm_validator.hpp"
#include "fsm/middleend/analysis/model_checker.hpp"
#include "fsm/middleend/pass_manager.hpp"
#include "fsm/middleend/passes/dead_state_pruning_pass.hpp"
#include "fsm/middleend/passes/determinism_enforcement_pass.hpp"
#include "fsm/middleend/passes/guard_simplification_pass.hpp"
#include "fsm/middleend/passes/orthogonal_interference_pass.hpp"
#include "fsm/middleend/passes/submachine_inlining_pass.hpp"
#include "tools/common/file_utils.hpp"
#include "tools/fsmc/fsmc_options.hpp"

namespace fsm::tools {

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

class FsmcDriver {
  public:
    static int run(const FsmcOptions& opts) {
        if (opts.show_help) {
            print_help("fsmc");
            return 0;
        }

        if (opts.show_version) {
            std::cout << "fsmc version 0.6.0 (Universal State Machine Compiler & Optimization Infrastructure)\n";
            return 0;
        }

        if (!opts.is_valid) {
            std::cerr << "Error: " << opts.error_message << "\n";
            std::cerr << "Use 'fsmc --help' for usage information.\n";
            return 1;
        }

        // Validate all CLI path arguments to reject null bytes or illegal characters
        auto validate_arg_path = [](const std::string& path_str, const std::string& arg_name) -> bool {
            if (!path_str.empty() && !is_valid_path_string(path_str)) {
                std::cerr << "Error: Illegal character in " << arg_name << " path: " << path_str << "\n";
                return false;
            }
            return true;
        };

        if (!validate_arg_path(opts.input_file, "--input") || !validate_arg_path(opts.output_file, "--output") ||
            !validate_arg_path(opts.sidecar_file, "--sidecar") || !validate_arg_path(opts.rtm_output_file, "--rtm") ||
            !validate_arg_path(opts.emit_sidecar, "--emit-sidecar") ||
            !validate_arg_path(opts.emit_test_harness, "--emit-test-harness") ||
            !validate_arg_path(opts.export_runtime_dir, "--export-runtime")) {
            return 1;
        }

        // Export standalone runtime if requested
        if (!opts.export_runtime_dir.empty()) {
            std::string err;
            if (!RuntimeExporter::export_runtime(opts.export_runtime_dir, opts.cpp_standard, err)) {
                std::cerr << "Error: " << err << "\n";
                return 1;
            }
            return 0;
        }

        if (opts.input_file.empty()) {
            std::cerr << "Error: No input model file specified.\n";
            std::cerr << "Use 'fsmc --help' for usage information.\n";
            return 1;
        }

        std::string read_err;
        std::string content = read_file_content(opts.input_file, read_err);
        if (!read_err.empty()) {
            std::cerr << "Error: " << read_err << "\n";
            return 1;
        }

        // Parse input model
        auto parser = ParserFactory::create(opts.input_file, opts.format);
        if (!parser) {
            std::cerr << "Error: Could not instantiate parser for input: " << opts.input_file << "\n";
            return 1;
        }

        fsm::ir::FsmIr model;
        std::string parse_error;
        if (!parser->parse(content, model, parse_error)) {
            std::cerr << "Syntax Error in " << opts.input_file << ":\n" << parse_error << "\n";
            return 1;
        }

        // Check for companion sidecar manifest (--sidecar <file> or auto-discovery)
        std::string sidecar_path = opts.sidecar_file;
        if (sidecar_path.empty()) {
            // Auto-discovery
            fs::path p(opts.input_file);
            std::string stem = p.stem().string();
            fs::path dir = p.parent_path();
            std::vector<std::string> candidates = {(dir / (stem + ".fsm.yaml")).string(),
                                                   (dir / (stem + ".fsm.yml")).string(),
                                                   (dir / (stem + ".fsm.json")).string(),
                                                   (dir / (stem + ".yaml")).string(), (dir / (stem + ".yml")).string()};
            for (const auto& candidate : candidates) {
                if (fs::exists(candidate)) {
                    sidecar_path = candidate;
                    break;
                }
            }
        }

        bool has_sidecar = false;
        if (!sidecar_path.empty()) {
            std::string sidecar_read_err;
            std::string sidecar_content = read_file_content(sidecar_path, sidecar_read_err);
            if (!sidecar_read_err.empty()) {
                std::cerr << "Warning: Could not read sidecar file '" << sidecar_path << "': " << sidecar_read_err
                          << "\n";
            } else {
                CompanionManifest manifest;
                std::string parse_manifest_err;
                if (CompanionManifestParser::parse(sidecar_content, manifest, parse_manifest_err)) {
                    std::string combine_err;
                    fsm::frontend::diagram::DiagramContractCombiner::combine(model, manifest, combine_err);
                    has_sidecar = true;
                    std::cout << "info: Loaded companion manifest: '" << sidecar_path << "'\n";
                } else {
                    std::cerr << "Warning: Failed to parse companion manifest '" << sidecar_path
                              << "': " << parse_manifest_err << "\n";
                }
            }
        }

        // Warning for diagram sources
        if (parser->kind() == FrontendKind::Diagram) {
            if (!has_sidecar) {
                std::cerr << "warning[W0301]: Untyped or inferred symbol in diagram source: '" << opts.input_file
                          << "'\n";
                if (!opts.allow_diagram_codegen && !opts.verify_mode && opts.export_diagram_format.empty() &&
                    opts.rtm_output_file.empty()) {
                    std::cerr << "\n[ERROR] Direct code generation blocked: '" << opts.input_file
                              << "' is a visual diagram format (" << parser->format_name()
                              << ").\nPass '--sidecar <manifest.yaml>' or '--allow-diagram-codegen' to allow code "
                                 "generation, or use '--verify' / '--export <fmt>' / '--rtm-output <file>'.\n";
                    return 1;
                }
            }
        }

        // Configure metadata
        if (!opts.fsm_name.empty()) {
            model.name = opts.fsm_name;
        } else if (model.name.empty() || model.name == "MyStateMachine") {
            model.name = infer_fsm_name_from_file(opts.input_file);
        }
        if (!opts.ns_name.empty()) {
            model.package = opts.ns_name;
        }

        // Inject custom CLI verification properties if specified
        if (!opts.ltl_spec.empty()) {
            model.add_property(fsm::ir::FormalProperty("cli_ltl_property", fsm::ir::PropertyKind::Safety, opts.ltl_spec,
                                                       "CLI specified LTL specification"));
        }
        if (!opts.ctl_spec.empty()) {
            model.add_property(fsm::ir::FormalProperty("cli_ctl_property", fsm::ir::PropertyKind::Safety, opts.ctl_spec,
                                                       "CLI specified CTL specification"));
        }

        // Requirement audit
        if (opts.req_audit) {
            perform_req_audit(model);
        }

        // Execute Middle-End Optimization Passes
        if (opts.opt_level > 0) {
            PassManager pm;
            pm.add_pass(std::make_unique<HierarchyCanonicalizationPass>());
            if (opts.simplify_guards) {
                pm.add_pass(std::make_unique<GuardSimplificationPassWrapper>());
            }
            if (opts.strict_determinism) {
                pm.add_pass(std::make_unique<DeterminismEnforcementPassWrapper>());
            }
            if (opts.check_races) {
                pm.add_pass(std::make_unique<OrthogonalInterferencePassWrapper>());
            }
            if (opts.inline_submachines) {
                pm.add_pass(create_submachine_pass(opts));
            }
            if (opts.prune_dead_states || opts.opt_level >= 2) {
                pm.add_pass(std::make_unique<DeadStatePruningPassWrapper>(true));
                pm.add_pass(std::make_unique<ConstantFoldingPassWrapper>());
            }
            pm.add_pass(std::make_unique<ChoiceCompletenessPass>());
            pm.add_pass(std::make_unique<ChoiceInliningPassWrapper>());
            pm.add_pass(std::make_unique<TimedDeadlockPassWrapper>());
            pm.add_pass(std::make_unique<EFSMDataPathPass>());
            pm.add_pass(std::make_unique<GuardSatisfiabilityPassWrapper>());
            pm.add_pass(std::make_unique<LivelockAnalysisPassWrapper>());
            pm.add_pass(std::make_unique<PriorityConflictPassWrapper>());
            pm.add_pass(std::make_unique<TimedInvariantsVerifierPassWrapper>());
            pm.add_pass(std::make_unique<EventQueueBoundPassWrapper>());
            pm.add_pass(std::make_unique<WcetAnalysisPassWrapper>());
            if (opts.verify_mode || opts.export_diagram_format.empty()) {
                pm.add_pass(std::make_unique<ModelSafetyVerifierPass>());
                pm.add_pass(std::make_unique<ModelCheckingPass>());
            }
            for (const auto& plugin_path : opts.pass_plugins) {
                DiagnosticEngine plugin_diag;
                if (!pm.load_plugin(plugin_path, plugin_diag)) {
                    std::cerr << plugin_diag.render_to_string(content);
                    return 1;
                }
            }
            if (!opts.pipe_through_cmd.empty()) {
                pm.add_pass(std::make_unique<PipeThroughPassWrapper>(opts.pipe_through_cmd));
            }

            DiagnosticEngine diag;
            if (!pm.run(model, diag)) {
                std::cerr << diag.render_to_string(content);
                return 1;
            }

            if (opts.werror && !diag.get_diagnostics().empty()) {
                bool has_warnings = false;
                for (const auto& d : diag.get_diagnostics()) {
                    if (d.severity == DiagnosticSeverity::Warning || d.severity == DiagnosticSeverity::Fatal ||
                        d.severity == DiagnosticSeverity::Error) {
                        has_warnings = true;
                        break;
                    }
                }
                if (has_warnings) {
                    std::cerr << "\n[ERROR] -Werror enabled: compilation failed due to middle-end warnings/errors:\n";
                    std::cerr << diag.render_to_string(content);
                    return 1;
                }
            }
        }

        // Semantic validation
        const auto validation = FsmValidator::validate(model);

        if (opts.verify_mode) {
            if (opts.verify_engine == "nuxmv") {
                return run_nuxmv_verification(opts, model, validation);
            }
            return print_verification_report(opts, model, validation);
        }

        for (const auto& warn_msg : validation.warnings) {
            std::cerr << "[WARNING] " << warn_msg << "\n";
        }

        if (!validation.is_valid) {
            std::cerr << "Semantic validation errors in model:\n";
            for (const auto& err : validation.errors) {
                std::cerr << "  - " << err << "\n";
            }
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
            std::cout << "[SUCCESS] Requirement Traceability Matrix exported to: " << opts.rtm_output_file << "\n";
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
                std::cout << "[SUCCESS] Diagram exported to " << opts.export_diagram_format << ": " << opts.output_file
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
                    std::cerr << "Error writing sidecar manifest to '" << opts.emit_sidecar << "': " << write_err
                              << "\n";
                    return 1;
                }
                std::cout << "[SUCCESS] Companion manifest sidecar exported to: " << opts.emit_sidecar << "\n";
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
            std::cout << "[SUCCESS] Companion manifest sidecar exported to: " << opts.emit_sidecar << "\n";
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
            std::cout << "[SUCCESS] MC/DC Test Harness synthesized successfully to: " << opts.emit_test_harness << "\n";
            if (opts.output_file.empty() && opts.export_diagram_format.empty() && opts.rtm_output_file.empty()) {
                return 0;
            }
        }

        // Generate C++ code
        GeneratorOptions gen_opts;
        gen_opts.cpp_standard = opts.cpp_standard;
        gen_opts.standalone = opts.standalone;
        gen_opts.include_stubs = opts.include_stubs;
        gen_opts.thread_safe = opts.thread_safe;
        gen_opts.target_namespace = opts.ns_name;

        std::string generated_code;
        try {
            generated_code = CppGenerator::generate_header(model, gen_opts);
        } catch (const std::exception& ex) {
            std::cerr << "Error: C++ backend rejected the model: " << ex.what() << "\n";
            return 1;
        }

        if (!opts.output_file.empty()) {
            std::string write_err;
            if (!write_file_content(opts.output_file, generated_code, write_err)) {
                std::cerr << "Error: " << write_err << "\n";
                return 1;
            }
            const std::string std_label = (opts.cpp_standard == CppStandard::Cpp20) ? "C++20" : "C++17";
            std::cout << "[SUCCESS] " << std_label << " (" << (opts.standalone ? "Standalone" : "Modular")
                      << ") generated successfully to: " << opts.output_file << "\n"
                      << "  - States: " << model.states.size() << "\n"
                      << "  - Events: " << model.signals.size() << "\n"
                      << "  - Transitions: " << model.transitions.size() << "\n"
                      << "  - Guards: " << model.guards.size() << "\n"
                      << "  - Actions: " << model.actions.size() << "\n";
        } else {
            std::cout << generated_code;
        }

        return 0;
    }

  private:
    static void perform_req_audit(const fsm::ir::FsmIr& model) {
        std::cout << "============================================================================\n"
                  << " Requirement Traceability Matrix (@fsm:req) : " << model.name << "\n"
                  << "============================================================================\n";
        std::size_t req_count = 0;
        for (const auto& st : model.states) {
            if (!st.traceability_reqs.empty()) {
                std::cout << " State '" << st.name << "' -> Requirements: ";
                for (const auto& r : st.traceability_reqs) {
                    std::cout << "[" << r << "] ";
                    ++req_count;
                }
                std::cout << "\n";
            }
        }
        for (const auto& prop : model.properties) {
            if (!prop.traceability_req.empty()) {
                std::cout << " Formal Property '" << prop.name << "' -> Requirement: [" << prop.traceability_req
                          << "]\n";
                ++req_count;
            }
        }
        std::cout << " Total Requirements Mapped: " << req_count << "\n"
                  << "============================================================================\n\n";
    }

    static std::unique_ptr<IPass> create_submachine_pass(const FsmcOptions& opts) {
        auto loaded_submachines = std::make_shared<std::map<std::string, fsm::ir::FsmIr>>();
        auto sub_pass = std::make_unique<SubmachineInliningPass>(
            [opts, loaded_submachines](const std::string& sub_name) -> const fsm::ir::FsmIr* {
                if (loaded_submachines->count(sub_name) != 0) {
                    return &(*loaded_submachines)[sub_name];
                }
                fs::path search_dir = opts.submachine_dir.empty() ? fs::path(opts.input_file).parent_path()
                                                                  : fs::path(opts.submachine_dir);
                for (const auto& ext : {".sysml", ".puml", ".mmd", ".xmi", ".scxml", ".json", ".dot"}) {
                    fs::path candidate = search_dir / (sub_name + ext);
                    if (fs::exists(candidate)) {
                        std::string read_err;
                        std::string sub_content = read_file_content(candidate.string(), read_err);
                        if (read_err.empty()) {
                            auto sub_parser = ParserFactory::create(candidate.string(), "auto");
                            if (sub_parser) {
                                fsm::ir::FsmIr sub_ir;
                                std::string sub_err;
                                if (sub_parser->parse(sub_content, sub_ir, sub_err)) {
                                    (*loaded_submachines)[sub_name] = std::move(sub_ir);
                                    return &(*loaded_submachines)[sub_name];
                                }
                            }
                        }
                    }
                }
                return nullptr;
            });

        class SubmachineWrapper : public IPass {
          public:
            explicit SubmachineWrapper(std::unique_ptr<SubmachineInliningPass> pass) : pass_(std::move(pass)) {}
            [[nodiscard]] std::string name() const override { return SubmachineInliningPass::name(); }
            [[nodiscard]] std::string description() const override { return SubmachineInliningPass::description(); }
            bool run(fsm::ir::FsmIr& ir, DiagnosticEngine& diag) override { return pass_->run(ir, diag); }

          private:
            std::unique_ptr<SubmachineInliningPass> pass_;
        };

        return std::make_unique<SubmachineWrapper>(std::move(sub_pass));
    }

    static int run_nuxmv_verification(const FsmcOptions& opts, const fsm::ir::FsmIr& model,
                                      const ValidationResult& validation) {
        std::cout << "============================================================================\n"
                  << " Formal Model Verification Report (Engine: nuXmv): " << model.name << "\n"
                  << "============================================================================\n"
                  << " Input File:       " << opts.input_file << "\n"
                  << " States:           " << model.states.size() << "\n"
                  << " Total Events:     " << model.signals.size() << "\n"
                  << " Transitions:      " << model.transitions.size() << "\n"
                  << " Formal Properties:" << model.properties.size() << "\n"
                  << "----------------------------------------------------------------------------\n";

        // Serialize model to temporary SMV file in standard temp directory
        std::string smv_content = fsm::backend::formal::SmvSerializer::serialize(model);
        std::error_code ec;
        fs::path temp_dir = fs::temp_directory_path(ec);
        if (ec) {
            temp_dir = fs::current_path();
        }
        fs::path temp_smv_path =
            temp_dir /
            ("fsmc_verify_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".smv");
        std::string temp_smv = temp_smv_path.string();
        std::string write_err;
        if (!write_file_content(temp_smv, smv_content, write_err)) {
            std::cerr << "[ERROR] Could not write temporary SMV file '" << temp_smv << "': " << write_err << "\n";
            return print_verification_report(opts, model, validation);
        }

        // Direct process execution without shell (/bin/sh or cmd.exe) to prevent command injection.
        // Hardcoded trusted executable names are used with system PATH resolution to prevent uncontrolled process
        // operation.
        std::string nuxmv_output;
        int exit_code = -1;
#if !defined(_WIN32)
        int pipe_fds[2];
        if (pipe(pipe_fds) != 0) {
            std::cerr << "[ERROR] Failed to create IPC pipe for nuXmv execution.\n";
            fs::remove(temp_smv_path, ec);
            return print_verification_report(opts, model, validation);
        }

        pid_t pid = fork();
        if (pid < 0) {
            close(pipe_fds[0]);
            close(pipe_fds[1]);
            std::cerr << "[ERROR] Failed to fork process for nuXmv execution.\n";
            fs::remove(temp_smv_path, ec);
            return print_verification_report(opts, model, validation);
        }

        if (pid == 0) {
            // Child: redirect stdout and stderr to the write-end of the pipe
            close(pipe_fds[0]);
            dup2(pipe_fds[1], STDOUT_FILENO);
            dup2(pipe_fds[1], STDERR_FILENO);
            close(pipe_fds[1]);

            char* const argv_caps[] = {const_cast<char*>("nuXmv"), const_cast<char*>(temp_smv.c_str()), nullptr};
            execvp("nuXmv", argv_caps);

            char* const argv_lower[] = {const_cast<char*>("nuxmv"), const_cast<char*>(temp_smv.c_str()), nullptr};
            execvp("nuxmv", argv_lower);

            _exit(127);
        }

        // Parent: read output from child pipe until EOF
        close(pipe_fds[1]);
        char out_buf[512];
        ssize_t bytes_read = 0;
        while ((bytes_read = read(pipe_fds[0], out_buf, sizeof(out_buf) - 1)) > 0) {
            out_buf[bytes_read] = '\0';
            nuxmv_output += out_buf;
        }
        close(pipe_fds[0]);

        int status = 0;
        waitpid(pid, &status, 0);
        exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

        if (exit_code == 127 && nuxmv_output.empty()) {
            fs::remove(temp_smv_path, ec);
            std::cerr << "[WARNING W0401] nuXmv binary ('nuXmv') not found in PATH.\n"
                      << "Falling back to embedded formal verification engine (ModelChecker).\n\n";
            return print_verification_report(opts, model, validation);
        }
#else
        HANDLE h_read = NULL;
        HANDLE h_write = NULL;
        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = NULL;

        bool process_started = false;
        if (CreatePipe(&h_read, &h_write, &sa, 0)) {
            SetHandleInformation(h_read, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOA si;
            ZeroMemory(&si, sizeof(si));
            si.cb = sizeof(si);
            si.hStdOutput = h_write;
            si.hStdError = h_write;
            si.dwFlags |= STARTF_USESTDHANDLES;

            PROCESS_INFORMATION pi;
            ZeroMemory(&pi, sizeof(pi));

            const char* candidates[] = {"nuXmv.exe", "nuXmv", "nuxmv.exe"};
            for (const char* cand : candidates) {
                std::string cmd_line = std::string(cand) + " \"" + temp_smv + "\"";
                std::vector<char> cmd_buf(cmd_line.begin(), cmd_line.end());
                cmd_buf.push_back('\0');

                if (CreateProcessA(NULL, cmd_buf.data(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
                    process_started = true;
                    break;
                }
            }

            if (process_started) {
                CloseHandle(h_write);
                h_write = NULL;

                char out_buf[512];
                DWORD dw_read = 0;
                while (ReadFile(h_read, out_buf, sizeof(out_buf) - 1, &dw_read, NULL) && dw_read > 0) {
                    out_buf[dw_read] = '\0';
                    nuxmv_output += out_buf;
                }

                WaitForSingleObject(pi.hProcess, INFINITE);
                DWORD dw_exit = 0;
                if (GetExitCodeProcess(pi.hProcess, &dw_exit)) {
                    exit_code = static_cast<int>(dw_exit);
                }
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            } else {
                if (h_write)
                    CloseHandle(h_write);
            }
            if (h_read)
                CloseHandle(h_read);
        }

        if (!process_started) {
            fs::remove(temp_smv_path, ec);
            std::cerr << "[WARNING W0401] nuXmv binary ('nuXmv') not found in PATH.\n"
                      << "Falling back to embedded formal verification engine (ModelChecker).\n\n";
            return print_verification_report(opts, model, validation);
        }
#endif
        fs::remove(temp_smv_path, ec);

        std::cout << "nuXmv Verification Output:\n";
        std::istringstream iss(nuxmv_output);
        std::string line;
        bool all_passed = (exit_code == 0);
        bool found_spec = false;
        while (std::getline(iss, line)) {
            if (line.find("-- specification") != std::string::npos) {
                found_spec = true;
                if (line.find("is true") != std::string::npos) {
                    std::cout << "  [PASSED] " << line << "\n";
                } else if (line.find("is false") != std::string::npos) {
                    std::cout << "  [VIOLATION] " << line << "\n";
                    all_passed = false;
                } else {
                    std::cout << "  " << line << "\n";
                }
            } else if (line.find("Trace Description:") != std::string::npos ||
                       line.find("Counterexample") != std::string::npos ||
                       line.find("-> State:") != std::string::npos) {
                std::cout << "    " << line << "\n";
            }
        }

        if (!found_spec) {
            std::cout << nuxmv_output;
        }

        std::cout << "----------------------------------------------------------------------------\n"
                  << " nuXmv Status: "
                  << (all_passed ? "PASSED (Model Sound & Properties Formally Verified)"
                                 : "FAILED (Violations Detected)")
                  << "\n============================================================================\n";

        return (all_passed && validation.is_valid) ? 0 : 1;
    }

    static int print_verification_report(const FsmcOptions& opts, const fsm::ir::FsmIr& model,
                                         const ValidationResult& validation) {
        std::size_t total_deferred = 0;
        for (const auto& s : model.states) {
            total_deferred += s.deferred_events.size();
        }

        std::cout << "============================================================================\n"
                  << " Formal Model Verification Report: " << model.name << "\n"
                  << "============================================================================\n"
                  << " Input File:       " << opts.input_file << "\n"
                  << " States:           " << model.states.size() << "\n"
                  << " Total Events:     " << model.signals.size() << "\n"
                  << " Transitions:      " << model.transitions.size() << "\n"
                  << " Choice Nodes:     " << model.choice_nodes.size() << "\n"
                  << " Deferred Triggers:" << total_deferred << "\n"
                  << "----------------------------------------------------------------------------\n"
                  << " Diagnostics:\n";

        if (validation.diagnostics.empty()) {
            std::cout << "  (No warnings or errors detected. Model is formally sound!)\n";
        } else {
            for (const auto& diag : validation.diagnostics) {
                std::string level_tag = "[INFO]";
                if (diag.severity == DiagnosticSeverity::Warning) {
                    level_tag = "[WARNING]";
                } else if (diag.severity == DiagnosticSeverity::SafetyCritical) {
                    level_tag = "[SAFETY CRITICAL]";
                } else if (diag.severity == DiagnosticSeverity::Error) {
                    level_tag = "[ERROR]";
                }
                std::cout << "  " << level_tag << " (" << diag.category << "): " << diag.message << "\n";
            }
        }

        bool all_valid = validation.is_valid;

        if (!model.properties.empty()) {
            std::cout << "----------------------------------------------------------------------------\n"
                      << " Formal Temporal Properties (" << model.properties.size() << "):\n";
            ModelChecker checker(model);
            auto mc_results = checker.verify_all();
            for (const auto& res : mc_results) {
                if (res.passed) {
                    std::cout << "  [PASSED] " << res.property_name << " (" << res.property_formula << ")\n";
                } else {
                    all_valid = false;
                    std::cout << "  [VIOLATION] " << res.property_name << " (" << res.property_formula << ")\n";
                    if (!res.violation_reason.empty()) {
                        std::cout << "    Reason: " << res.violation_reason << "\n";
                    }
                    std::string ce = res.format_counterexample();
                    if (!ce.empty()) {
                        std::cout << "  " << ce;
                    }
                }
            }
        }

        std::cout << "----------------------------------------------------------------------------\n"
                  << " Verification Status: "
                  << (all_valid ? "PASSED (Model Sound & Properties Verified)" : "FAILED (Errors/Violations Detected)")
                  << "\n"
                  << "============================================================================\n";

        return all_valid ? 0 : 1;
    }
};

}  // namespace fsm::tools

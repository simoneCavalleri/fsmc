#pragma once

#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/cpp/runtime_exporter.hpp"
#include "fsm/backend/emitter_factory.hpp"
#include "fsm/backend/rtm/rtm_emitter.hpp"
#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/frontend/common/parser_factory.hpp"
#include "fsm/frontend/common/parser_interface.hpp"
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

using namespace fsm::diagnostic;
using namespace fsm::frontend;
using namespace fsm::middleend;
using namespace fsm::backend;

class FsmcDriver {
  public:
    static int run(const FsmcOptions& opts) {
        if (opts.show_help) {
            print_help("fsmc");
            return 0;
        }

        if (opts.show_version) {
            std::cout << "fsmc version 0.5.0 (Universal State Machine Compiler & Optimization Infrastructure)\n";
            return 0;
        }

        if (!opts.is_valid) {
            std::cerr << "Error: " << opts.error_message << "\n";
            std::cerr << "Use 'fsmc --help' for usage information.\n";
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

        // Warning for diagram sources
        if (parser->kind() == FrontendKind::Diagram) {
            std::cerr << "warning[W0301]: Untyped or inferred symbol in diagram source: '" << opts.input_file << "'\n";
            if (!opts.allow_diagram_codegen && !opts.verify_mode && opts.export_diagram_format.empty() &&
                opts.rtm_output_file.empty()) {
                std::cerr << "\n[ERROR] Direct code generation blocked: '" << opts.input_file
                          << "' is a visual diagram format (" << parser->format_name()
                          << ").\nPass '--allow-diagram-codegen' to allow heuristic code "
                             "generation, or use '--verify' / '--export <fmt>' / '--rtm-output <file>'.\n";
                return 1;
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
            model.add_property(fsm::ir::FormalProperty("cli_ltl_property", fsm::ir::PropertyKind::Safety,
                                                       opts.ltl_spec, "CLI specified LTL specification"));
        }
        if (!opts.ctl_spec.empty()) {
            model.add_property(fsm::ir::FormalProperty("cli_ctl_property", fsm::ir::PropertyKind::Safety,
                                                       opts.ctl_spec, "CLI specified CTL specification"));
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
            }
            pm.add_pass(std::make_unique<ChoiceCompletenessPass>());
            pm.add_pass(std::make_unique<ChoiceInliningPassWrapper>());
            pm.add_pass(std::make_unique<TimedDeadlockPassWrapper>());
            pm.add_pass(std::make_unique<EFSMDataPathPass>());
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
                    if (d.severity == DiagnosticSeverity::Warning ||
                        d.severity == DiagnosticSeverity::Fatal ||
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
            std::string exported_diagram =
                EmitterFactory::emit_diagram(model, opts.export_diagram_format);
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
            return 0;
        }

        // Generate C++ code
        GeneratorOptions gen_opts;
        gen_opts.cpp_standard = opts.cpp_standard;
        gen_opts.standalone = opts.standalone;
        gen_opts.include_stubs = opts.include_stubs;
        gen_opts.thread_safe = opts.thread_safe;
        gen_opts.target_namespace = opts.ns_name;


        const std::string generated_code = CppGenerator::generate_header(model, gen_opts);

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
            explicit SubmachineWrapper(std::unique_ptr<SubmachineInliningPass> pass)
                : pass_(std::move(pass)) {}
            [[nodiscard]] std::string name() const override { return SubmachineInliningPass::name(); }
            [[nodiscard]] std::string description() const override {
                return SubmachineInliningPass::description();
            }
            bool run(fsm::ir::FsmIr& ir, DiagnosticEngine& diag) override {
                return pass_->run(ir, diag);
            }

          private:
            std::unique_ptr<SubmachineInliningPass> pass_;
        };

        return std::make_unique<SubmachineWrapper>(std::move(sub_pass));
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

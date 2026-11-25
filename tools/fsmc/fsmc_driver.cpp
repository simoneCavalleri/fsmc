#include "tools/fsmc/fsmc_driver.hpp"

#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "fsm/backend/cpp/runtime_exporter.hpp"
#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/frontend/common/companion_manifest_parser.hpp"
#include "fsm/frontend/common/parser_factory.hpp"
#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/frontend/diagram/common/diagram_contract_combiner.hpp"
#include "fsm/frontend/directive/ltl_parser.hpp"
#include "fsm/frontend/formal/sysml2/sysml2_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/event_queue_bound_pass.hpp"
#include "fsm/middleend/pass_manager.hpp"
#include "fsm/middleend/passes/dead_state_pruning_pass.hpp"
#include "fsm/middleend/passes/determinism_enforcement_pass.hpp"
#include "fsm/middleend/passes/guard_simplification_pass.hpp"
#include "fsm/middleend/passes/orthogonal_interference_pass.hpp"
#include "fsm/middleend/passes/submachine_inlining_pass.hpp"
#include "fsm/middleend/passes/wcet_analysis_pass.hpp"
#include "tools/common/file_utils.hpp"
#include "tools/fsmc/driver_internal.hpp"
#include "tools/fsmc/fsmc_options.hpp"

namespace fsm::tools {

using namespace fsm::ir;
using namespace fsm::diagnostic;
using namespace fsm::frontend;
using namespace fsm::middleend;
using namespace fsm::middleend::analysis;
using namespace fsm::middleend::passes;
using namespace fsm::backend::cpp;
using namespace driver_detail;

namespace {

std::unique_ptr<IPass> create_submachine_pass(const FsmcOptions& opts) {
    return PassManager::create_pass_by_name(
        "submachine-inlining", "", create_filesystem_submachine_resolver(opts.input_file, opts.submachine_dir));
}

std::optional<int> validate_cli_and_early_exit(const FsmcOptions& opts) {
    if (opts.show_help) {
        print_help("fsmc");
        return 0;
    }

    if (opts.show_version) {
        std::cout << "fsmc version 0.15.0 (Universal State Machine Compiler & Optimization Infrastructure)\n";
        return 0;
    }

    if (!opts.is_valid) {
        std::cerr << "Error: " << opts.error_message << "\n";
        std::cerr << "Use 'fsmc --help' for usage information.\n";
        return 1;
    }

    // Validate all CLI path arguments to reject null bytes or illegal characters
    auto validate_arg_path = [](const std::string& path_str, const std::string& arg_name) -> bool {
        if (is_stdin_path(path_str) || is_stdout_path(path_str)) {
            return true;
        }
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
        !validate_arg_path(opts.export_runtime_dir, "--export-runtime") ||
        !validate_arg_path(opts.submachine_dir, "--submachine-dir")) {
        return 1;
    }

    // Check for conflicting CLI options
    if (opts.standalone_specified && opts.modular_specified) {
        std::cerr << "warning[W0102]: Conflicting options '--standalone' and '--modular' specified; prioritizing '--"
                  << (opts.standalone ? "standalone" : "modular") << "'.\n";
        if (opts.werror) {
            return 1;
        }
    }
    // Export standalone runtime if requested
    if (!opts.export_runtime_dir.empty()) {
        std::string err;
        if (!RuntimeExporter::export_runtime(opts.export_runtime_dir, err)) {
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

    return std::nullopt;
}

bool load_and_parse_model(const FsmcOptions& opts, std::string& content, fsm::ir::FsmIr& model,
                          std::unique_ptr<IParser>& parser, bool& has_sidecar) {
    std::string read_err;
    content = read_file_content(opts.input_file, read_err);
    if (!read_err.empty()) {
        std::cerr << "Error: " << read_err << "\n";
        return false;
    }

    // Auto-detect format from content if reading from stdin without explicit format
    std::string resolved_format = opts.format;
    if (is_stdin_path(opts.input_file) && (resolved_format.empty() || resolved_format == "auto")) {
        std::string detected = ParserFactory::detect_format_from_content(content);
        if (!detected.empty()) {
            resolved_format = detected;
        }
    }

    // Parse input model
    parser = ParserFactory::create(opts.input_file, resolved_format);
    if (!parser) {
        std::cerr << "Error: Could not instantiate parser for input: " << opts.input_file << "\n";
        return false;
    }

    // Configure SysML v2 workspace options (multi-file imports & --state-def selection)
    if (auto* sysml_parser = dynamic_cast<fsm::frontend::formal::Sysml2Parser*>(parser.get())) {
        fsm::frontend::formal::Sysml2ParseOptions s_opts;
        s_opts.import_dirs = opts.import_dirs;
        s_opts.target_state_def = opts.state_def;
        if (!is_stdin_path(opts.input_file)) {
            s_opts.source_file = opts.input_file;
        }
        sysml_parser->set_options(std::move(s_opts));
    }

    fsm::diagnostic::SourceManager source_manager;
    source_manager.register_source(opts.input_file, content);

    fsm::diagnostic::DiagnosticEngine parse_diag;
    if (!parser->parse(content, model, parse_diag)) {
        const auto parse_fmt = parse_diagnostic_format(opts.diagnostic_format);
        std::cerr << "Syntax Error in " << opts.input_file << ":\n"
                  << parse_diag.render_to_format(parse_fmt, content, &source_manager);
        return false;
    }

    // Check for companion sidecar manifest (--sidecar <file> or auto-discovery)
    std::string sidecar_path = opts.sidecar_file;
    if (sidecar_path.empty() && !is_stdin_path(opts.input_file)) {
        // Auto-discovery
        fs::path p(opts.input_file);
        std::string stem = p.stem().string();
        fs::path dir = p.parent_path();
        std::vector<std::string> candidates = {
            (dir / (stem + ".fsm.yaml")).string(), (dir / (stem + ".fsm.yml")).string(),
            (dir / (stem + ".fsm.json")).string(), (dir / (stem + ".yaml")).string(), (dir / (stem + ".yml")).string()};
        for (const auto& candidate : candidates) {
            if (fs::exists(candidate)) {
                sidecar_path = candidate;
                break;
            }
        }
    }

    has_sidecar = false;
    if (!sidecar_path.empty()) {
        std::string sidecar_read_err;
        std::string sidecar_content = read_file_content(sidecar_path, sidecar_read_err);
        if (!sidecar_read_err.empty()) {
            std::cerr << "Warning: Could not read sidecar file '" << sidecar_path << "': " << sidecar_read_err << "\n";
        } else {
            CompanionManifest manifest;
            std::string parse_manifest_err;
            if (CompanionManifestParser::parse(sidecar_content, manifest, parse_manifest_err)) {
                std::string combine_err;
                if (!fsm::frontend::diagram::DiagramContractCombiner::combine(model, manifest, combine_err)) {
                    std::cerr << "Error combining companion manifest '" << sidecar_path << "': " << combine_err << "\n";
                    return false;
                }
                has_sidecar = true;
                std::cerr << "info: Loaded companion manifest: '" << sidecar_path << "'\n";
            } else {
                std::cerr << "Warning: Failed to parse companion manifest '" << sidecar_path
                          << "': " << parse_manifest_err << "\n";
            }
        }
    }

    // Warning for diagram sources
    if (parser->kind() == FrontendKind::Diagram) {
        if (!has_sidecar) {
            std::cerr << "warning[W0301]: Untyped or inferred symbol in diagram source: '" << opts.input_file << "'\n";
            if (!opts.allow_diagram_codegen && !opts.verify_mode && !opts.simulate_mode &&
                opts.export_diagram_format.empty() && opts.rtm_output_file.empty() && opts.emit_sidecar.empty() &&
                opts.emit_test_harness.empty()) {
                std::cerr << "\n[ERROR] Direct code generation blocked: '" << opts.input_file
                          << "' is a visual diagram format (" << parser->format_name()
                          << ").\nPass '--sidecar <manifest.yaml>' or '--allow-diagram' to allow code "
                             "generation, or use '--verify' / '--export <fmt>' / '--rtm <file>'.\n";
                return false;
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
        fsm::ir::FormalProperty prop("cli_ltl_property", fsm::ir::PropertyKind::Safety, opts.ltl_spec,
                                     "CLI specified LTL specification");
        auto parsed_ast = fsm::frontend::directive::LtlPropertyParser::parse(opts.ltl_spec);
        if (parsed_ast) {
            prop.ast = std::move(parsed_ast);
        } else {
            std::cerr << "Error: Syntax error in LTL formula: " << opts.ltl_spec << "\n";
            return false;
        }
        model.add_property(std::move(prop));
    }
    if (!opts.ctl_spec.empty()) {
        fsm::ir::FormalProperty prop("cli_ctl_property", fsm::ir::PropertyKind::Safety, opts.ctl_spec,
                                     "CLI specified CTL specification");
        auto parsed_ast = fsm::frontend::directive::LtlPropertyParser::parse(opts.ctl_spec);
        if (parsed_ast) {
            prop.ast = std::move(parsed_ast);
        } else {
            std::cerr << "Error: Syntax error in CTL formula: " << opts.ctl_spec << "\n";
            return false;
        }
        model.add_property(std::move(prop));
    }

    // Requirement audit
    if (opts.req_audit) {
        perform_req_audit(model);
    }
    return true;
}

bool run_middleend_pipeline(const FsmcOptions& opts, const std::string& content, fsm::ir::FsmIr& model,
                            DiagnosticEngine& middleend_diag, DiagnosticFormat diag_fmt) {
    PassManager pm;
    bool race_check_ok = true;
    if (opts.pipeline_mode == "7stage" && opts.export_diagram_format.empty()) {
        if (opts.inline_submachines) {
            create_submachine_pass(opts)->run(model, middleend_diag);
        }
        if (opts.check_races) {
            race_check_ok = PassManager::create_pass_by_name("orthogonal-interference")->run(model, middleend_diag);
        }
        const bool optimize_7stage = opts.opt_level > 0 && (opts.opt_level >= 2 || opts.prune_dead_states);
        pm = PassManager::create_verified_7stage_pipeline(optimize_7stage);
    } else {
        if (opts.export_diagram_format.empty() || opts.opt_level > 0) {
            pm.add_pass(PassManager::create_pass_by_name("hierarchy-canonicalization"));
        }
        if (opts.opt_level > 0 && opts.simplify_guards) {
            pm.add_pass(PassManager::create_pass_by_name("guard-simplification"));
        }
        if (opts.strict_determinism) {
            pm.add_pass(PassManager::create_pass_by_name("determinism-enforcement"));
        }
        if (opts.inline_submachines) {
            pm.add_pass(create_submachine_pass(opts));
        }
        if (opts.check_races) {
            pm.add_pass(PassManager::create_pass_by_name("orthogonal-interference"));
        }
        if (opts.opt_level > 0) {
            if (opts.prune_dead_states || opts.opt_level >= 2) {
                pm.add_pass(PassManager::create_pass_by_name("constant-folding"));
                pm.add_pass(PassManager::create_pass_by_name("dead-state-pruning"));
                pm.add_pass(PassManager::create_pass_by_name("dead-action-elimination"));
                pm.add_pass(PassManager::create_pass_by_name("common-action-factoring"));
                pm.add_pass(PassManager::create_pass_by_name("transition-fusion"));
            }
            pm.add_pass(PassManager::create_pass_by_name("connective-junction-chaining"));
            pm.add_pass(PassManager::create_pass_by_name("choice-completeness"));
            pm.add_pass(PassManager::create_pass_by_name("choice-inlining"));
            pm.add_pass(PassManager::create_pass_by_name("sampled-change-trigger"));
            pm.add_pass(PassManager::create_pass_by_name("timed-deadlock"));
            pm.add_pass(PassManager::create_pass_by_name("efsm-data-path"));
            if (opts.prune_dead_states || opts.opt_level >= 2) {
                pm.add_pass(PassManager::create_pass_by_name("register-liveness"));
            }
            pm.add_pass(PassManager::create_pass_by_name("guard-satisfiability"));
            pm.add_pass(PassManager::create_pass_by_name("livelock-analysis"));
            pm.add_pass(PassManager::create_pass_by_name("priority-conflict-check"));
            pm.add_pass(PassManager::create_pass_by_name("timed-invariants-verifier"));
            pm.add_pass(PassManager::create_pass_by_name("event-queue-bound"));
            pm.add_pass(PassManager::create_pass_by_name("wcet-analysis"));
            if (opts.verify_mode) {
                pm.add_pass(PassManager::create_pass_by_name("history-lowering"));
                pm.add_pass(PassManager::create_pass_by_name("fork-join-lowering"));
                pm.add_pass(PassManager::create_pass_by_name("orthogonal-product"));
            }
            if (opts.verify_mode || opts.export_diagram_format.empty()) {
                pm.add_pass(PassManager::create_pass_by_name("safety-verifier"));
                if (!opts.verify_mode) {
                    pm.add_pass(PassManager::create_pass_by_name("model-checking"));
                }
            }
        }
    }

    for (const auto& plugin_path : opts.pass_plugins) {
        DiagnosticEngine plugin_diag;
        if (!pm.load_plugin(plugin_path, plugin_diag)) {
            std::cerr << plugin_diag.render_to_format(diag_fmt, content);
            return false;
        }
    }
    if (!opts.pipe_through_cmd.empty()) {
        pm.add_pass(PassManager::create_pass_by_name("pipe-through", opts.pipe_through_cmd));
    }

    if ((!race_check_ok || !pm.run(model, middleend_diag)) && !opts.verify_mode) {
        std::cerr << middleend_diag.render_to_format(diag_fmt, content);
        return false;
    }

    if (opts.werror && (middleend_diag.has_warnings() || middleend_diag.has_errors()) && !opts.verify_mode) {
        std::cerr << "\n[ERROR] -Werror enabled: compilation failed due to middle-end warnings/errors:\n";
        std::cerr << middleend_diag.render_to_format(diag_fmt, content);
        return false;
    } else if (!middleend_diag.get_diagnostics().empty() && !opts.verify_mode) {
        std::cerr << middleend_diag.render_to_format(diag_fmt, content);
    }
    return true;
}

}  // namespace

int FsmcDriver::run(const FsmcOptions& opts) {
    if (auto early_rc = validate_cli_and_early_exit(opts); early_rc.has_value()) {
        return *early_rc;
    }

    if (opts.list_machines) {
        std::string read_err;
        std::string raw = read_file_content(opts.input_file, read_err);
        if (!read_err.empty()) {
            std::cerr << "Error: " << read_err << "\n";
            return 1;
        }
        const auto names = fsm::frontend::formal::Sysml2Parser::list_state_defs(raw);
        if (opts.list_machines_json || opts.diagnostic_format == "json") {
            std::cout << "[";
            for (std::size_t i = 0; i < names.size(); ++i) {
                if (i > 0) {
                    std::cout << ", ";
                }
                std::cout << "\"" << names[i] << "\"";
            }
            std::cout << "]\n";
        } else {
            for (const auto& n : names) {
                std::cout << n << "\n";
            }
        }
        return 0;
    }

    std::string content;
    fsm::ir::FsmIr model;
    std::unique_ptr<IParser> parser;
    bool has_sidecar = false;
    if (!load_and_parse_model(opts, content, model, parser, has_sidecar)) {
        return 1;
    }

    DiagnosticEngine middleend_diag;
    DiagnosticFormat diag_fmt = parse_diagnostic_format(opts.diagnostic_format);
    if (!run_middleend_pipeline(opts, content, model, middleend_diag, diag_fmt)) {
        return 1;
    }

    if (auto aux_rc = handle_auxiliary_modes(opts, model, middleend_diag); aux_rc.has_value()) {
        return *aux_rc;
    }

    return emit_cpp_target(opts, content, model, *parser, has_sidecar, diag_fmt);
}

}  // namespace fsm::tools

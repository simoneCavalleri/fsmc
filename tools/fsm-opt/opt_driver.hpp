#pragma once

#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "fsm/backend/emitter_factory.hpp"
#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/frontend/parser_factory.hpp"
#include "fsm/frontend/parser_interface.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"
#include "fsm/middleend/choice_inlining_pass.hpp"
#include "fsm/middleend/dead_state_pruning_pass.hpp"
#include "fsm/middleend/determinism_enforcement_pass.hpp"
#include "fsm/middleend/efsm_interval_analysis.hpp"
#include "fsm/middleend/guard_simplification_pass.hpp"
#include "fsm/middleend/orthogonal_interference_pass.hpp"
#include "fsm/middleend/pass_manager.hpp"
#include "fsm/middleend/submachine_inlining_pass.hpp"
#include "tools/common/file_utils.hpp"
#include "tools/fsm-opt/opt_options.hpp"

namespace fsm::tools {

class OptDriver {
  public:
    static int run(const OptOptions& opts) {
        if (opts.show_help) {
            print_opt_help("fsm-opt");
            return 0;
        }

        if (opts.show_version) {
            std::cout << "fsm-opt v0.3.0 (Formal FSM Intermediate Representation Optimizer & Linter)\n";
            return 0;
        }

        if (opts.list_passes) {
            print_available_passes();
            return 0;
        }

        if (!opts.is_valid) {
            std::cerr << "Error: " << opts.error_message << "\n";
            std::cerr << "Use 'fsm-opt --help' for usage information.\n";
            return 1;
        }

        if (opts.input_path.empty()) {
            std::cerr << "Error: No input file specified. Use -i <file> or specify as argument.\n\n";
            print_opt_help("fsm-opt");
            return 1;
        }

        std::string read_err;
        std::string content = read_file_content(opts.input_path, read_err);
        if (!read_err.empty()) {
            std::cerr << "Error: " << read_err << "\n";
            return 1;
        }

        auto parser = fsm::codegen::ParserFactory::create(opts.input_path, opts.format_override);
        if (!parser) {
            std::cerr << "Error: Cannot find suitable parser for: " << opts.input_path << "\n";
            return 1;
        }

        fsm::codegen::FsmIr ir;
        std::string err;
        if (!parser->parse(content, ir, err)) {
            std::cerr << "Frontend Parse Error: " << err << "\n";
            return 1;
        }

        if (opts.print_before_all) {
            std::cout << "\n=== [IR BEFORE PASSES] ===\n"
                      << fsm::codegen::FsmIrSerializer::serialize_json(ir) << "\n"
                      << "==========================\n";
        }

        // Build Pass Pipeline
        fsm::codegen::PassManager pm;
        if (!opts.custom_passes.empty()) {
            auto pass_names = split_string(opts.custom_passes, ',');
            for (const auto& p_name : pass_names) {
                if (p_name == "canonicalize") {
                    pm.add_pass(std::make_unique<fsm::codegen::HierarchyCanonicalizationPass>());
                } else if (p_name == "guard-simplification") {
                    pm.add_pass(std::make_unique<fsm::codegen::GuardSimplificationPassWrapper>());
                } else if (p_name == "determinism") {
                    pm.add_pass(std::make_unique<fsm::codegen::DeterminismEnforcementPassWrapper>());
                } else if (p_name == "race-check") {
                    pm.add_pass(std::make_unique<fsm::codegen::OrthogonalInterferencePassWrapper>());
                } else if (p_name == "dead-state-pruning") {
                    pm.add_pass(std::make_unique<fsm::codegen::DeadStatePruningPassWrapper>(true));
                } else if (p_name == "choice-completeness") {
                    pm.add_pass(std::make_unique<fsm::codegen::ChoiceCompletenessPass>());
                } else if (p_name == "choice-inlining") {
                    pm.add_pass(std::make_unique<fsm::codegen::ChoiceInliningPassWrapper>());
                } else if (p_name == "timed-deadlock") {
                    pm.add_pass(std::make_unique<fsm::codegen::TimedDeadlockPassWrapper>());
                } else if (p_name == "efsm-data-path") {
                    pm.add_pass(std::make_unique<fsm::codegen::EFSMDataPathPass>());
                } else if (p_name == "safety-verifier") {
                    pm.add_pass(std::make_unique<fsm::codegen::ModelSafetyVerifierPass>());
                } else if (p_name == "model-checking") {
                    pm.add_pass(std::make_unique<fsm::codegen::ModelCheckingPass>());
                } else {
                    std::cerr << "[WARNING] Unrecognized pass name: '" << p_name << "'. Skipping.\n";
                }
            }
        } else {
            pm = fsm::codegen::PassManager::create_optimizing_pipeline(opts.prune_dead);
        }

        fsm::codegen::DiagnosticEngine diag;
        if (!pm.run(ir, diag)) {
            std::cerr << diag.render_to_string(content);
            return 1;
        }

        if (!diag.get_diagnostics().empty()) {
            std::cerr << diag.render_to_string(content);
            if (opts.werror) {
                std::cerr << "\n[ERROR] -Werror enabled: compilation failed due to middle-end warnings.\n";
                return 1;
            }
        }

        if (opts.print_after_all) {
            std::cout << "\n=== [IR AFTER PASSES] ===\n"
                      << fsm::codegen::FsmIrSerializer::serialize_json(ir) << "\n"
                      << "=========================\n";
        }

        if (opts.profile) {
            std::cout << "\n=== [PassManager Execution Profile] ===\n";
            for (const auto& s : pm.get_stats()) {
                std::cout << "  - " << s.pass_name << ": " << s.duration_ms << " ms\n";
            }
            std::cout << "=======================================\n";
        }

        if (opts.show_metrics) {
            print_metrics(ir);
        }

        if (opts.verify_only) {
            return diag.has_errors() ? 1 : 0;
        }

        // Serialization & Emission
        std::string output_str;
        if (opts.emit_format == "ir" || opts.emit_format == "json") {
            output_str = fsm::codegen::FsmIrSerializer::serialize_json(ir);
        } else {
            output_str = fsm::codegen::EmitterFactory::emit_diagram(ir, opts.emit_format);
            if (output_str.empty()) {
                std::cerr << "Error: Unsupported output format: " << opts.emit_format << "\n";
                return 1;
            }
        }

        if (!opts.output_path.empty()) {
            std::string write_err;
            if (!write_file_content(opts.output_path, output_str, write_err)) {
                std::cerr << "Error: " << write_err << "\n";
                return 1;
            }
            std::cout << "[SUCCESS] Optimized output written to: " << opts.output_path << "\n";
        } else {
            std::cout << output_str;
        }

        return 0;
    }

  private:
    static std::vector<std::string> split_string(std::string_view s, char delimiter) {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream token_stream((std::string(s)));
        while (std::getline(token_stream, token, delimiter)) {
            if (!token.empty()) {
                tokens.push_back(token);
            }
        }
        return tokens;
    }

    static void print_metrics(const fsm::codegen::FsmIr& ir) {
        std::cout << "\n=== [FSM Formal IR Graph Metrics: " << ir.name << "] ===\n"
                  << "  - States (Total):      " << ir.states.size() << "\n"
                  << "  - Events (Signals):    " << ir.events.size() << "\n"
                  << "  - Transitions (Edges): " << ir.transitions.size() << "\n"
                  << "  - Guards (Predicates): " << ir.guards.size() << "\n"
                  << "  - Actions (Effects):   " << ir.actions.size() << "\n"
                  << "  - Temporal Properties: " << ir.properties.size() << "\n"
                  << "  - State Variables:     " << ir.variables.size() << "\n"
                  << "====================================================\n";
    }
};

}  // namespace fsm::tools

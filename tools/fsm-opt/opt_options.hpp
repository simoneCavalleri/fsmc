#pragma once

#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "fsm/middleend/pass_interface.hpp"
#include "tools/common/cli_utils.hpp"
#include "tools/common/file_utils.hpp"

namespace fsm::tools {

struct OptOptions {
    std::string input_path;
    std::string output_path;
    std::string format_override;
    std::string sidecar_file;
    std::vector<std::string> import_dirs;
    std::string state_def;
    bool list_machines = false;
    bool list_machines_json = false;
    std::string custom_passes;
    std::string submachine_dir;
    std::string emit_format = "ir";
    bool emit_explicit = false;
    std::string diagnostic_format = "text";
    bool profile = false;
    bool verify_only = false;
    bool verify_ir = false;
    bool dump_pipeline = false;
    bool show_metrics = false;
    bool prune_dead = false;
    bool print_before_all = false;
    bool print_after_all = false;
    bool werror = false;
    std::string pipe_through_cmd;
    std::vector<std::string> pass_plugins;
    bool show_help = false;
    bool show_version = false;
    bool list_passes = false;
    bool is_valid = true;
    std::string error_message;
};

inline void print_opt_help(const char* prog_name) {
    std::cout
        << "============================================================================\n"
        << " fsm-opt : Formal FSM Intermediate Representation (IR) Optimizer & Linter\n"
        << "============================================================================\n\n"
        << "Usage: " << prog_name << " -i <model_file> [OPTIONS]\n"
        << "       " << prog_name << " [OPTIONS] <model_file>\n\n"
        << "Input & Output Options:\n"
        << "  -i, --input <file>        Input model or IR file (.sysml, .puml, .mmd, .xmi, .scxml, .json, .dot, .sfx, "
           ".slx, .smv, .xml)\n"
        << "  -o, --output <file>       Output file path (default: stdout)\n"
        << "  --format <fmt>            Override parser format (sysml2, plantuml, mermaid, cameo, scxml, json, dot, "
           "stateflow, smv)\n"
        << "  -s, --sidecar <file>      Companion typed manifest (.fsm.yaml/.fsm.json; auto-discovered if omitted)\n"
        << "  -I, --import-dir <dir>    Additional SysML v2 package search directory (repeatable)\n"
        << "  --state-def <name>        Select a specific SysML v2 'state def' in multi-machine files\n"
        << "  --list-machines[=json]    List all 'state def' machines in a SysML v2 file and exit\n\n"
        << "IR Optimization & Pass Pipeline:\n"
        << "  --passes=<p1,p2,...>      Execute customized comma-separated pass pipeline\n"
        << "  --list-passes             List all available Middle-End passes and exit\n"
        << "  --dump-pipeline           Print scheduled pass pipeline DAG, prerequisites, and invalidations\n"
        << "  --verify-ir               Verify FsmGraphOps::is_well_formed(ir) after every pass\n"
        << "  --prune-dead              Enable dead state and dead transition pruning pass\n"
        << "  --print-before-all        Print IR JSON before running passes\n"
        << "  --print-after-all         Print IR JSON after running passes\n"
        << "  --diagnostic-format <fmt> Diagnostic output format: 'text', 'json', or 'github'\n"
        << "  -Werror                   Treat all diagnostic warnings as fatal errors\n\n"
        << "IR Serialization & Formal Emission:\n"
        << "  --emit-ir                 Emit optimized canonical JSON Intermediate Representation (default)\n"
        << "  --emit-puml               Emit canonical PlantUML state diagram\n"
        << "  --emit-mmd                Emit canonical Mermaid stateDiagram-v2\n"
        << "  --emit-sysml              Emit canonical OMG SysML v2 state definition\n"
        << "  --emit-json, --emit-xstate Emit canonical XState v5 JSON statechart\n"
        << "  --emit-dot                Emit canonical Graphviz DOT diagram\n"
        << "  --emit-scxml              Emit canonical W3C SCXML statechart\n"
        << "  --emit-cameo              Emit canonical Cameo / MagicDraw OMG XMI 2.1\n"
        << "  --emit-smv                Emit canonical nuXmv / SMV formal verification specification\n"
        << "  --emit-stateflow          Emit canonical MathWorks Simulink Stateflow XML\n\n"
        << "Analysis, Model Checking & Metrics:\n"
        << "  --metrics, --stats        Display formal graph complexity, states, and transition metrics\n"
        << "  --profile                 Print PassManager execution times and optimization stats\n"
        << "  --verify, --check         Run formal model checking passes (Safety, LTL/CTL, Reachability)\n\n"
        << "General Options:\n"
        << "  -h, --help                Show this help message and exit\n"
        << "  -v, --version             Show version information and exit\n\n"
        << "Examples:\n"
        << "  " << prog_name << " -i model.sysml --emit-ir -o model.ir.json\n"
        << "  " << prog_name << " -i model.sysml --passes=guard-simplification,dead-state-pruning --emit-ir\n"
        << "  " << prog_name << " -i controller.puml --emit-smv -o controller.smv\n"
        << "  " << prog_name << " -i aerospace.sysml --metrics --verify\n\n";
}

inline void print_available_passes() {
    std::cout << "============================================================================\n"
              << " Registered Target-Agnostic Middle-End Passes in fsmc\n"
              << "============================================================================\n";
    std::size_t idx = 1;
    for (const auto& desc : ::fsm::middleend::PassRegistry::descriptors()) {
        std::cout << " " << std::setw(2) << idx++ << ". " << std::left << std::setw(30) << desc.canonical_name << " ["
                  << desc.category << "] " << desc.description << "\n";
    }
    std::cout << "============================================================================\n";
}

inline OptOptions parse_opt_args(int argc, char* argv[]) {
    OptOptions opts;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            opts.show_help = true;
            return opts;
        }
        if (arg == "-v" || arg == "--version") {
            opts.show_version = true;
            return opts;
        }
        if (arg == "--list-passes") {
            opts.list_passes = true;
            return opts;
        }
        if (parse_common_compiler_arg(arg, i, argc, argv, opts.input_path, opts.output_path, opts.format_override,
                                      opts.submachine_dir, opts.diagnostic_format, opts.pipe_through_cmd,
                                      opts.pass_plugins, opts.werror, opts.error_message)) {
            if (!opts.error_message.empty()) {
                opts.is_valid = false;
                return opts;
            }
            continue;
        }
        if (match_opt_value(arg, i, argc, argv, "-p", "--passes", opts.custom_passes, opts.error_message)) {
            if (!opts.error_message.empty()) {
                opts.is_valid = false;
                return opts;
            }
        } else if (match_opt_value(arg, i, argc, argv, "-s", "--sidecar", opts.sidecar_file, opts.error_message)) {
            if (!opts.error_message.empty()) {
                opts.is_valid = false;
                return opts;
            }
        } else if (std::string inc_dir;
                   match_opt_value(arg, i, argc, argv, "-I", "--import-dir", inc_dir, opts.error_message)) {
            if (!opts.error_message.empty()) {
                opts.is_valid = false;
                return opts;
            }
            opts.import_dirs.push_back(std::move(inc_dir));
        } else if (match_opt_value(arg, i, argc, argv, "", "--state-def", opts.state_def, opts.error_message)) {
            if (!opts.error_message.empty()) {
                opts.is_valid = false;
                return opts;
            }
        } else if (arg == "--list-machines") {
            opts.list_machines = true;
        } else if (arg == "--list-machines=json") {
            opts.list_machines = true;
            opts.list_machines_json = true;
        } else if (arg == "--prune-dead") {
            opts.prune_dead = true;
        } else if (arg == "--print-before-all") {
            opts.print_before_all = true;
        } else if (arg == "--print-after-all") {
            opts.print_after_all = true;
        } else if (arg == "--metrics" || arg == "--stats") {
            opts.show_metrics = true;
        } else if (arg == "--emit-ir") {
            opts.emit_format = "ir";
            opts.emit_explicit = true;
        } else if (arg == "--emit-puml") {
            opts.emit_format = "puml";
            opts.emit_explicit = true;
        } else if (arg == "--emit-mmd") {
            opts.emit_format = "mmd";
            opts.emit_explicit = true;
        } else if (arg == "--emit-sysml") {
            opts.emit_format = "sysml";
            opts.emit_explicit = true;
        } else if (arg == "--emit-json" || arg == "--emit-xstate") {
            opts.emit_format = "xstate";
            opts.emit_explicit = true;
        } else if (arg == "--emit-dot") {
            opts.emit_format = "dot";
            opts.emit_explicit = true;
        } else if (arg == "--emit-scxml") {
            opts.emit_format = "scxml";
            opts.emit_explicit = true;
        } else if (arg == "--emit-cameo" || arg == "--emit-xmi") {
            opts.emit_format = "cameo";
            opts.emit_explicit = true;
        } else if (arg == "--emit-smv" || arg == "--emit-nuxmv") {
            opts.emit_format = "smv";
            opts.emit_explicit = true;
        } else if (arg == "--emit-stateflow" || arg == "--emit-sfx" || arg == "--emit-simulink") {
            opts.emit_format = "stateflow";
            opts.emit_explicit = true;
        } else if (arg == "--profile") {
            opts.profile = true;
        } else if (arg == "--dump-pipeline") {
            opts.dump_pipeline = true;
        } else if (arg == "--verify-ir") {
            opts.verify_ir = true;
        } else if (arg == "--verify" || arg == "--check") {
            opts.verify_only = true;
        } else if (!arg.starts_with("-") || arg == "-") {
            if (opts.input_path.empty()) {
                opts.input_path = arg;
            } else {
                opts.is_valid = false;
                opts.error_message = "Unexpected positional argument: " + std::string(arg);
                return opts;
            }
        } else {
            opts.is_valid = false;
            opts.error_message = "Unknown option: " + std::string(arg);
            return opts;
        }
    }

    return opts;
}

}  // namespace fsm::tools

#pragma once

#include <iostream>
#include <string>
#include <vector>

namespace fsm::tools {

struct OptOptions {
    std::string input_path;
    std::string output_path;
    std::string format_override;
    std::string custom_passes;
    std::string emit_format = "ir";
    bool profile = false;
    bool verify_only = false;
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
        << "  -i, --input <file>        Input model or IR file (.sysml, .puml, .mmd, .xmi, .scxml, .json, .dot)\n"
        << "  -o, --output <file>       Output file path (default: stdout)\n"
        << "  --format <fmt>            Override parser format (sysml2, plantuml, mermaid, cameo, scxml, json, dot)\n\n"
        << "IR Optimization & Pass Pipeline:\n"
        << "  --passes=<p1,p2,...>      Execute customized comma-separated pass pipeline\n"
        << "  --list-passes             List all available Middle-End passes and exit\n"
        << "  --prune-dead              Enable dead state and dead transition pruning pass\n"
        << "  --print-before-all        Print IR JSON before running passes\n"
        << "  --print-after-all         Print IR JSON after running passes\n"
        << "  -Werror                   Treat all diagnostic warnings as fatal errors\n\n"
        << "IR Serialization & Formal Emission:\n"
        << "  --emit-ir                 Emit optimized canonical JSON Intermediate Representation (default)\n"
        << "  --emit-puml               Emit canonical PlantUML state diagram\n"
        << "  --emit-mmd                Emit canonical Mermaid stateDiagram-v2\n"
        << "  --emit-sysml              Emit canonical OMG SysML v2 state definition\n"
        << "  --emit-json               Emit canonical XState JSON\n"
        << "  --emit-dot                Emit canonical Graphviz DOT diagram\n"
        << "  --emit-scxml              Emit canonical W3C SCXML statechart\n"
        << "  --emit-cameo              Emit canonical Cameo / MagicDraw OMG XMI 2.1\n"
        << "  --emit-smv                Emit canonical nuXmv / SMV formal verification specification\n\n"
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
              << "============================================================================\n"
              << " 1. canonicalize          - Normalizes state hierarchy, FQNs, and sorts canonically\n"
              << " 2. guard-simplification  - Bottom-up boolean algebra reduction (!(!A)->A, A&&true->A)\n"
              << " 3. determinism           - Enforces deterministic event dispatch and priority ordering\n"
              << " 4. race-check            - Static data-race analysis on parallel orthogonal variables\n"
              << " 5. inline-submachines    - Splicing and inlining of modular SubmachineRef statecharts\n"
              << " 6. dead-state-pruning    - Physical elimination of unreachable states and dead branches\n"
              << " 7. choice-completeness   - Verifies choice pseudostate branch exhaustiveness\n"
              << " 8. choice-inlining       - Collapses choice/junction nodes into composite transitions\n"
              << " 9. timed-deadlock        - Detects temporal deadlock traps and timer invariant conflicts\n"
              << " 10. efsm-data-path       - Abstract interpretation for unreachable data paths/dead guards\n"
              << " 11. safety-verifier      - Graph reachability, deadlock traps, and livelock cycle check\n"
              << " 12. model-checking       - Formal verification of temporal LTL/CTL formulas\n"
              << " 13. orthogonal-product   - Cartesian product expansion of parallel orthogonal regions\n"
              << " 14. wcet-analysis        - Analyzes micro-step execution chains and detects Zeno-cycles\n"
              << " 15. constant-folding     - Folds constant guard conditions and prunes dead transitions\n"
              << " 16. state-minimization   - DFA state minimization via Hopcroft/Moore partitioning\n"
              << " 17. pipe-through         - Filters and transforms IR via external Unix command\n"
              << "============================================================================\n";
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
        if ((arg == "-i" || arg == "--input") && i + 1 < argc) {
            opts.input_path = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            opts.output_path = argv[++i];
        } else if (arg == "--format" && i + 1 < argc) {
            opts.format_override = argv[++i];
        } else if (arg.starts_with("--passes=")) {
            opts.custom_passes = arg.substr(9);
        } else if (arg == "--prune-dead") {
            opts.prune_dead = true;
        } else if (arg == "--print-before-all") {
            opts.print_before_all = true;
        } else if (arg == "--print-after-all") {
            opts.print_after_all = true;
        } else if (arg == "-Werror") {
            opts.werror = true;
        } else if (arg == "--metrics" || arg == "--stats") {
            opts.show_metrics = true;
        } else if (arg == "--emit-ir") {
            opts.emit_format = "ir";
        } else if (arg == "--emit-puml") {
            opts.emit_format = "puml";
        } else if (arg == "--emit-mmd") {
            opts.emit_format = "mmd";
        } else if (arg == "--emit-sysml") {
            opts.emit_format = "sysml";
        } else if (arg == "--emit-json") {
            opts.emit_format = "json";
        } else if (arg == "--emit-dot") {
            opts.emit_format = "dot";
        } else if (arg == "--emit-scxml") {
            opts.emit_format = "scxml";
        } else if (arg == "--emit-cameo" || arg == "--emit-xmi") {
            opts.emit_format = "cameo";
        } else if (arg == "--emit-smv" || arg == "--emit-nuxmv") {
            opts.emit_format = "smv";
        } else if (arg == "--profile") {
            opts.profile = true;
        } else if (arg == "--verify" || arg == "--check") {
            opts.verify_only = true;
        } else if (arg == "--pipe-through" && i + 1 < argc) {
            opts.pipe_through_cmd = argv[++i];
        } else if (arg.rfind("--pipe-through=", 0) == 0) {
            opts.pipe_through_cmd = std::string(arg.substr(15));
        } else if (arg == "--load-pass-plugin" && i + 1 < argc) {
            opts.pass_plugins.push_back(argv[++i]);
        } else if (arg.rfind("--load-pass-plugin=", 0) == 0) {
            opts.pass_plugins.push_back(std::string(arg.substr(19)));
        } else if (!arg.starts_with("-")) {
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

#pragma once

#include <iostream>
#include <string>
#include <vector>

#include "fsm/backend/cpp/cpp_generator.hpp"

namespace fsm::tools {

struct FsmcOptions {
    std::string input_file;
    std::string output_file;
    std::string export_runtime_dir;
    std::string export_diagram_format;
    std::string rtm_output_file;
    std::string rtm_format;
    std::string fsm_name;
    std::string ns_name = "fsm_generated";
    std::string target_lang = "cpp";
    std::string format = "auto";
    std::string submachine_dir;
    fsm::codegen::CppStandard cpp_standard = fsm::codegen::CppStandard::Cpp17;
    int opt_level = 1;                   // -O0, -O1, -O2
    bool prune_dead_states = false;      // --prune-dead-states
    bool simplify_guards = true;         // --no-guard-simplification
    bool inline_submachines = false;     // --inline-submachines
    bool strict_determinism = false;     // --strict-determinism
    bool check_races = false;            // --check-races
    bool werror = false;                 // -Werror
    bool req_audit = false;              // --req-audit
    bool allow_diagram_codegen = false;  // --allow-diagram-codegen
    bool standalone = true;
    bool thread_safe = true;
    bool include_stubs = true;
    bool verify_mode = false;
    bool show_help = false;
    bool show_version = false;
    bool is_valid = true;
    std::string error_message;
};

inline void print_help(const char* prog_name) {
    std::cout
        << "============================================================================\n"
        << " fsmc : The Universal State Machine Compiler Driver\n"
        << "============================================================================\n\n"
        << "Usage: " << prog_name << " -i <model_file> [OPTIONS]\n"
        << "       " << prog_name << " [OPTIONS] <model_file>\n"
        << "       " << prog_name << " -i <model_file> --export <format> -o <out_file>\n"
        << "       " << prog_name << " -i <model_file> --verify\n"
        << "       " << prog_name << " --export-runtime <dir> [--std 17|20]\n\n"
        << "Input & Output Options:\n"
        << "  -i, --input <file>          Input model file (.sysml, .puml, .mmd, .xmi, .scxml, .json, .dot)\n"
        << "  -o, --output <file>         Output generated code or exported diagram file (default: stdout)\n"
        << "  -t, --target <lang>         Target code generator backend: 'cpp' (default)\n"
        << "  -n, --name <name>           Generated FSM class name (default: inferred from filename or 'MyFSM')\n"
        << "  --namespace, --package <ns> Generated namespace/package/module name (default: 'fsm_generated')\n"
        << "  --format <fmt>              Override input format: 'sysml2', 'plantuml', 'mermaid', 'cameo', 'scxml', "
           "'json', 'dot', 'auto'\n\n"
        << "Optimization & Code Transformation Options:\n"
        << "  -O0, --no-opt               Disable middle-end optimization passes\n"
        << "  -O1, -O2, --optimize        Enable middle-end optimization passes (default: -O1)\n"
        << "  --prune-dead-states         Prune unreachable states and statically dead transitions before codegen\n"
        << "  --no-guard-simplification   Disable algebraic boolean simplification on guard expressions\n"
        << "  --inline-submachines        Inline modular submachines (SubmachineRef) into a single flat/composite FSM\n"
        << "  --submachine-dir <dir>      Search directory for external submachine diagram files\n\n"
        << "Safety & Static Analysis Verification Options:\n"
        << "  -Werror                     Treat all middle-end compiler warnings as fatal errors\n"
        << "  --strict-determinism        Fail compilation on non-deterministic branch collisions or unprioritized "
           "transitions\n"
        << "  --check-races               Perform static concurrency data-race analysis across parallel orthogonal "
           "regions\n"
        << "  --req-audit                 Print Requirement Traceability Matrix (@fsm:req) before code generation\n"
        << "  --rtm-output <file>         Export Requirement Traceability Matrix to file\n"
        << "  --rtm-format <json|md>      Requirement Traceability Matrix format ('json' or 'markdown')\n\n"
        << "C++ Backend Options (--target cpp):\n"
        << "  --std <17|20>               Target C++ standard: '17' or '20' (default: 17)\n"
        << "  --c++17                     Target C++17 standard\n"
        << "  --c++20                     Target C++20 standard\n"
        << "  --standalone                Generate self-contained header with embedded zero-alloc runtime (default)\n"
        << "  --modular                   Generate FSM header only, including external <fsm/fsm.hpp>\n"
        << "  --export-runtime <dir>      Export the standalone FSM runtime library headers to directory\n"
        << "  --no-thread-safe            Do not generate thread_safe_fsm asynchronous wrapper\n"
        << "  --no-stubs                  Do not emit default stub functors for actions and guards\n"
        << "  --allow-diagram-codegen     Allow C++ code generation from visual diagram formats (PlantUML, Mermaid, "
           "etc.)\n\n"
        << "Model Analysis & Diagram Export:\n"
        << "  -e, --export <fmt>          Export diagram or formal model to: 'mermaid', 'plantuml', 'sysml2', 'json', "
           "'dot', 'scxml', 'cameo', 'smv'\n"
        << "  --verify, --check           Run formal model checker (livelock, choice completeness, reachability) and "
           "exit\n\n"
        << "General Options:\n"
        << "  -h, --help                  Show this help message and exit\n"
        << "  -v, --version               Show version information and exit\n\n";
}

inline FsmcOptions parse_cli_args(int argc, char* argv[]) {
    FsmcOptions opts;

    for (int idx = 1; idx < argc; ++idx) {
        const std::string arg = argv[idx];
        if (arg == "-h" || arg == "--help") {
            opts.show_help = true;
            return opts;
        }
        if (arg == "-v" || arg == "--version") {
            opts.show_version = true;
            return opts;
        }
        if ((arg == "-i" || arg == "--input") && idx + 1 < argc) {
            opts.input_file = argv[++idx];
        } else if ((arg == "-o" || arg == "--output") && idx + 1 < argc) {
            opts.output_file = argv[++idx];
        } else if ((arg == "-t" || arg == "--target" || arg == "--lang") && idx + 1 < argc) {
            opts.target_lang = argv[++idx];
            if (opts.target_lang != "cpp" && opts.target_lang != "c++") {
                opts.is_valid = false;
                opts.error_message =
                    "Unsupported target language: '" + opts.target_lang + "' (currently supported: 'cpp')";
                return opts;
            }
        } else if ((arg == "-n" || arg == "--name") && idx + 1 < argc) {
            opts.fsm_name = argv[++idx];
        } else if ((arg == "--namespace" || arg == "--package") && idx + 1 < argc) {
            opts.ns_name = argv[++idx];
        } else if (arg == "--format" && idx + 1 < argc) {
            opts.format = argv[++idx];
        } else if ((arg == "-e" || arg == "--export") && idx + 1 < argc) {
            opts.export_diagram_format = argv[++idx];
        } else if (arg == "--export-runtime" && idx + 1 < argc) {
            opts.export_runtime_dir = argv[++idx];
        } else if (arg == "--submachine-dir" && idx + 1 < argc) {
            opts.submachine_dir = argv[++idx];
        } else if (arg == "-O0" || arg == "--no-opt") {
            opts.opt_level = 0;
        } else if (arg == "-O1") {
            opts.opt_level = 1;
        } else if (arg == "-O2" || arg == "--optimize") {
            opts.opt_level = 2;
        } else if (arg == "--prune-dead-states") {
            opts.prune_dead_states = true;
        } else if (arg == "--no-guard-simplification") {
            opts.simplify_guards = false;
        } else if (arg == "--inline-submachines") {
            opts.inline_submachines = true;
        } else if (arg == "--strict-determinism") {
            opts.strict_determinism = true;
        } else if (arg == "--check-races") {
            opts.check_races = true;
        } else if (arg == "-Werror") {
            opts.werror = true;
        } else if (arg == "--req-audit") {
            opts.req_audit = true;
        } else if (arg == "--rtm-output" && idx + 1 < argc) {
            opts.rtm_output_file = argv[++idx];
        } else if (arg == "--rtm-format" && idx + 1 < argc) {
            opts.rtm_format = argv[++idx];
        } else if (arg == "--std" && idx + 1 < argc) {
            const std::string std_val = argv[++idx];
            if (std_val == "17" || std_val == "c++17" || std_val == "C++17") {
                opts.cpp_standard = fsm::codegen::CppStandard::Cpp17;
            } else if (std_val == "20" || std_val == "c++20" || std_val == "C++20") {
                opts.cpp_standard = fsm::codegen::CppStandard::Cpp20;
            } else {
                opts.is_valid = false;
                opts.error_message = "Unsupported C++ standard: " + std_val + " (expected 17 or 20)";
                return opts;
            }
        } else if (arg == "--c++17" || arg == "-std=c++17") {
            opts.cpp_standard = fsm::codegen::CppStandard::Cpp17;
        } else if (arg == "--c++20" || arg == "-std=c++20") {
            opts.cpp_standard = fsm::codegen::CppStandard::Cpp20;
        } else if (arg == "--standalone") {
            opts.standalone = true;
        } else if (arg == "--modular") {
            opts.standalone = false;
        } else if (arg == "--verify" || arg == "--check") {
            opts.verify_mode = true;
        } else if (arg == "--no-thread-safe") {
            opts.thread_safe = false;
        } else if (arg == "--no-stubs") {
            opts.include_stubs = false;
        } else if (arg == "--allow-diagram-codegen") {
            opts.allow_diagram_codegen = true;
        } else if (!arg.empty() && arg[0] != '-') {
            if (opts.input_file.empty()) {
                opts.input_file = arg;
            } else {
                opts.is_valid = false;
                opts.error_message = "Unexpected positional argument: " + arg;
                return opts;
            }
        } else {
            opts.is_valid = false;
            opts.error_message = "Unknown option: " + arg;
            return opts;
        }
    }

    return opts;
}

}  // namespace fsm::tools

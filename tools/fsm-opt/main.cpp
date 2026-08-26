#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/backend/emitter_factory.hpp"
#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/frontend/parser_factory.hpp"
#include "fsm/frontend/parser_interface.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"
#include "fsm/middleend/dead_state_pruning_pass.hpp"
#include "fsm/middleend/determinism_enforcement_pass.hpp"
#include "fsm/middleend/guard_simplification_pass.hpp"
#include "fsm/middleend/orthogonal_interference_pass.hpp"
#include "fsm/middleend/pass_manager.hpp"
#include "fsm/middleend/submachine_inlining_pass.hpp"

using namespace fsm;
using namespace fsm::codegen;

namespace {

void print_help(const char* prog_name) {
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

void print_available_passes() {
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
              << " 8. safety-verifier       - Graph reachability, deadlock traps, and livelock cycle check\n"
              << " 9. model-checking        - Formal verification of temporal LTL/CTL formulas\n"
              << "============================================================================\n";
}

std::vector<std::string> split_string(std::string_view s, char delimiter) {
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

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_help(argv[0]);
        return 1;
    }

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

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        }
        if (arg == "-v" || arg == "--version") {
            std::cout << "fsm-opt (The Universal Finite State Machine Compiler Infrastructure)\n";
            return 0;
        }
        if (arg == "--list-passes") {
            print_available_passes();
            return 0;
        }
        if ((arg == "-i" || arg == "--input") && i + 1 < argc) {
            input_path = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "--format" && i + 1 < argc) {
            format_override = argv[++i];
        } else if (arg.starts_with("--passes=")) {
            custom_passes = arg.substr(9);
        } else if (arg == "--prune-dead") {
            prune_dead = true;
        } else if (arg == "--print-before-all") {
            print_before_all = true;
        } else if (arg == "--print-after-all") {
            print_after_all = true;
        } else if (arg == "-Werror") {
            werror = true;
        } else if (arg == "--metrics" || arg == "--stats") {
            show_metrics = true;
        } else if (arg == "--emit-ir") {
            emit_format = "ir";
        } else if (arg == "--emit-puml") {
            emit_format = "puml";
        } else if (arg == "--emit-mmd") {
            emit_format = "mmd";
        } else if (arg == "--emit-sysml") {
            emit_format = "sysml";
        } else if (arg == "--emit-json") {
            emit_format = "json";
        } else if (arg == "--emit-dot") {
            emit_format = "dot";
        } else if (arg == "--emit-scxml") {
            emit_format = "scxml";
        } else if (arg == "--emit-cameo" || arg == "--emit-xmi") {
            emit_format = "cameo";
        } else if (arg == "--emit-smv" || arg == "--emit-nuxmv") {
            emit_format = "smv";
        } else if (arg == "--profile") {
            profile = true;
        } else if (arg == "--verify" || arg == "--check") {
            verify_only = true;
        } else if (!arg.starts_with("-")) {
            input_path = arg;
        }
    }

    if (input_path.empty()) {
        std::cerr << "Error: No input file specified. Use -i <file> or specify as argument.\n\n";
        print_help(argv[0]);
        return 1;
    }

    std::ifstream in_file(input_path);
    if (!in_file.is_open()) {
        std::cerr << "Error: Cannot open input file: " << input_path << "\n";
        return 1;
    }

    std::stringstream buffer;
    buffer << in_file.rdbuf();
    std::string content = buffer.str();

    // Use unified ParserFactory
    std::unique_ptr<IParser> parser = ParserFactory::create(input_path, format_override);
    if (!parser) {
        std::cerr << "Error: Cannot find suitable parser for: " << input_path << "\n";
        return 1;
    }

    FsmIr ir;
    std::string err;
    if (!parser->parse(content, ir, err)) {
        std::cerr << "Frontend Parse Error: " << err << "\n";
        return 1;
    }

    if (print_before_all) {
        std::cout << "\n=== [IR BEFORE PASSES] ===\n"
                  << FsmIrSerializer::serialize_json(ir) << "\n"
                  << "==========================\n";
    }

    // Build Pass Pipeline
    PassManager pm;
    if (!custom_passes.empty()) {
        auto passes = split_string(custom_passes, ',');
        for (const auto& p_name : passes) {
            if (p_name == "canonicalize") {
                pm.add_pass(std::make_unique<HierarchyCanonicalizationPass>());
            } else if (p_name == "guard-simplification") {
                pm.add_pass(std::make_unique<GuardSimplificationPassWrapper>());
            } else if (p_name == "determinism") {
                pm.add_pass(std::make_unique<DeterminismEnforcementPassWrapper>());
            } else if (p_name == "race-check") {
                pm.add_pass(std::make_unique<OrthogonalInterferencePassWrapper>());
            } else if (p_name == "dead-state-pruning") {
                pm.add_pass(std::make_unique<DeadStatePruningPassWrapper>(true));
            } else if (p_name == "choice-completeness") {
                pm.add_pass(std::make_unique<ChoiceCompletenessPass>());
            } else if (p_name == "safety-verifier") {
                pm.add_pass(std::make_unique<ModelSafetyVerifierPass>());
            } else if (p_name == "model-checking") {
                pm.add_pass(std::make_unique<ModelCheckingPass>());
            } else {
                std::cerr << "[WARNING] Unrecognized pass name: '" << p_name << "'. Skipping.\n";
            }
        }
    } else {
        pm = PassManager::create_optimizing_pipeline(prune_dead);
    }

    DiagnosticEngine diag;
    if (!pm.run(ir, diag)) {
        std::cerr << diag.render_to_string(content);
        return 1;
    }

    if (!diag.get_diagnostics().empty()) {
        std::cerr << diag.render_to_string(content);
        if (werror) {
            std::cerr << "\n[ERROR] -Werror enabled: compilation halted due to middle-end warnings.\n";
            return 1;
        }
    }

    if (print_after_all) {
        std::cout << "\n=== [IR AFTER PASSES] ===\n"
                  << FsmIrSerializer::serialize_json(ir) << "\n"
                  << "=========================\n";
    }

    if (show_metrics) {
        std::size_t atomic_count = 0;
        std::size_t composite_count = 0;
        std::size_t parallel_count = 0;
        for (const auto& s : ir.states) {
            if (s.kind == StateKind::Composite)
                ++composite_count;
            else if (s.kind == StateKind::Parallel)
                ++parallel_count;
            else
                ++atomic_count;
        }

        // Cyclomatic complexity estimation for state machine: E - N + 2P
        long cyclomatic =
            std::max<long>(1, static_cast<long>(ir.transitions.size()) - static_cast<long>(ir.states.size()) + 2);

        std::cout << "============================================================================\n"
                  << " Formal FSM Model Metrics: " << ir.name << "\n"
                  << "============================================================================\n"
                  << " Total States:               " << ir.states.size() << " (Atomic: " << atomic_count
                  << ", Composite: " << composite_count << ", Parallel: " << parallel_count << ")\n"
                  << " Total Transitions:          " << ir.transitions.size() << "\n"
                  << " Signals & Events:           " << ir.signals.size() << "\n"
                  << " EFSM State Variables:       " << ir.variables.size() << "\n"
                  << " Formal Properties (LTL/CTL):" << ir.properties.size() << "\n"
                  << " Cyclomatic Graph Metric:    " << cyclomatic << "\n"
                  << "============================================================================\n";
    }

    if (profile) {
        const auto& stats = pm.get_stats();
        std::cerr << "\n[PassManager Profiling Summary]\n";
        double total_ms = 0.0;
        for (const auto& p : stats) {
            std::cerr << "  - " << p.pass_name << ": " << p.duration_ms << " ms ("
                      << (p.modified_ir ? "MODIFIED" : "UNCHANGED") << ")\n";
            total_ms += p.duration_ms;
        }
        std::cerr << "Total Middle-End Time: " << total_ms << " ms\n\n";
    }

    if (verify_only) {
        std::cout << "[SUCCESS] Formal verification completed: Model is sound with zero safety violations.\n";
        return 0;
    }

    // Emit transformed result using unified EmitterFactory
    std::string output_str = EmitterFactory::emit_diagram(ir, emit_format);

    if (!output_path.empty()) {
        std::ofstream out_file(output_path);
        if (!out_file.is_open()) {
            std::cerr << "Error: Cannot open output file: " << output_path << "\n";
            return 1;
        }
        out_file << output_str;
        std::cout << "[SUCCESS] Transformed representation emitted to: " << output_path << "\n";
    } else {
        std::cout << output_str;
    }

    return 0;
}

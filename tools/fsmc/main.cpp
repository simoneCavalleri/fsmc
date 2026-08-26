#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/emitter_factory.hpp"
#include "fsm/backend/runtime_exporter.hpp"
#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/frontend/parser_factory.hpp"
#include "fsm/frontend/parser_interface.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/dead_state_pruning_pass.hpp"
#include "fsm/middleend/determinism_enforcement_pass.hpp"
#include "fsm/middleend/fsm_validator.hpp"
#include "fsm/middleend/guard_simplification_pass.hpp"
#include "fsm/middleend/orthogonal_interference_pass.hpp"
#include "fsm/middleend/pass_manager.hpp"
#include "fsm/middleend/submachine_inlining_pass.hpp"

namespace fs = std::filesystem;
using namespace fsm::codegen;

namespace {

inline bool ends_with(std::string_view str, std::string_view suffix) noexcept {
    return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

struct CliOptions {
    std::string input_file;
    std::string output_file;
    std::string export_runtime_dir;
    std::string export_diagram_format;
    std::string fsm_name;
    std::string ns_name = "fsm_generated";
    std::string context_type = "no_context";
    std::string target_lang = "cpp";
    std::string format = "auto";
    std::string submachine_dir;
    CppStandard cpp_standard = CppStandard::Cpp17;
    int opt_level = 1;                   // -O0, -O1, -O2
    bool prune_dead_states = false;      // --prune-dead-states
    bool simplify_guards = true;         // --no-guard-simplification
    bool inline_submachines = false;     // --inline-submachines
    bool strict_determinism = false;     // --strict-determinism
    bool check_races = false;            // --check-races
    bool werror = false;                 // -Werror
    bool req_audit = false;              // --req-audit
    bool allow_diagram_codegen = false;  // --allow-diagram-codegen / --allow-tier2-codegen
    bool standalone = true;
    bool thread_safe = true;
    bool include_stubs = true;
    bool verify_mode = false;
    bool show_help = false;
    bool show_version = false;
    bool is_valid = true;
    std::string error_message;
};

void print_help(const char* prog_name) {
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
        << "  -t, --target <lang>         Target code generator backend: 'cpp' (default), 'c', 'rust', 'zig', 'ts'\n"
        << "  -n, --name <name>           Generated FSM class name (default: inferred from filename or 'MyFSM')\n"
        << "  --namespace, --package <ns> Generated namespace/package/module name (default: 'fsm_generated')\n"
        << "  --context <type>            Hardware/Software context type name (default: 'no_context')\n"
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
        << "  --req-audit                 Print Requirement Traceability Matrix (@fsm:req) before code generation\n\n"
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
           "etc.)\n"
        << "  --allow-tier2-codegen       (Alias for --allow-diagram-codegen)\n\n"
        << "Model Analysis & Diagram Export:\n"
        << "  -e, --export <fmt>          Export diagram or formal model to: 'mermaid', 'plantuml', 'sysml2', 'json', "
           "'dot', 'scxml', 'cameo', 'smv'\n"
        << "  --verify, --check           Run formal model checker (livelock, choice completeness, reachability) and "
           "exit\n\n"
        << "General Options:\n"
        << "  -h, --help                  Show this help message and exit\n"
        << "  -v, --version               Show version information and exit\n\n";
}

CliOptions parse_cli_args(int argc, char* argv[]) {
    CliOptions opts;

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
        } else if ((arg == "-n" || arg == "--name") && idx + 1 < argc) {
            opts.fsm_name = argv[++idx];
        } else if ((arg == "--namespace" || arg == "--package") && idx + 1 < argc) {
            opts.ns_name = argv[++idx];
        } else if (arg == "--context" && idx + 1 < argc) {
            opts.context_type = argv[++idx];
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
        } else if (arg == "--std" && idx + 1 < argc) {
            const std::string std_val = argv[++idx];
            if (std_val == "17" || std_val == "c++17" || std_val == "C++17") {
                opts.cpp_standard = CppStandard::Cpp17;
            } else if (std_val == "20" || std_val == "c++20" || std_val == "C++20") {
                opts.cpp_standard = CppStandard::Cpp20;
            } else {
                opts.is_valid = false;
                opts.error_message = "Unsupported C++ standard: " + std_val + " (expected 17 or 20)";
                return opts;
            }
        } else if (arg == "--c++17" || arg == "-std=c++17") {
            opts.cpp_standard = CppStandard::Cpp17;
        } else if (arg == "--c++20" || arg == "-std=c++20") {
            opts.cpp_standard = CppStandard::Cpp20;
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
        } else if (arg == "--allow-diagram-codegen" || arg == "--allow-tier2-codegen") {
            opts.allow_diagram_codegen = true;
        } else if (!arg.empty() && arg[0] != '-') {
            if (opts.input_file.empty()) {
                opts.input_file = arg;
            } else if (opts.output_file.empty()) {
                opts.output_file = arg;
            }
        } else {
            opts.is_valid = false;
            opts.error_message = "Unknown command line option: " + arg;
            return opts;
        }
    }

    return opts;
}

std::string infer_fsm_name_from_file(const std::string& filepath) {
    fs::path p(filepath);
    std::string stem = p.stem().string();
    if (stem.empty()) {
        return "MyStateMachine";
    }

    std::string result;
    bool capitalize_next = true;
    for (char character : stem) {
        if (character == '_' || character == '-' || character == '.') {
            capitalize_next = true;
        } else if (capitalize_next) {
            result += static_cast<char>(std::toupper(character));
            capitalize_next = false;
        } else {
            result += character;
        }
    }

    if (!result.empty() && !ends_with(result, "FSM") && !ends_with(result, "Fsm")) {
        result += "FSM";
    }
    return result.empty() ? "MyStateMachine" : result;
}

}  // namespace

int main(int argc, char* argv[]) {
    const CliOptions opts = parse_cli_args(argc, argv);

    if (opts.show_help) {
        print_help(argv[0]);
        return 0;
    }
    if (opts.show_version) {
        std::cout << "fsmc (The Finite State Machine Compiler Infrastructure)\n";
        return 0;
    }
    if (!opts.is_valid) {
        std::cerr << "Error: " << opts.error_message << "\n\n";
        print_help(argv[0]);
        return 1;
    }

    // Export Runtime Library Mode
    if (!opts.export_runtime_dir.empty()) {
        const std::string runtime_file_path = (fs::path(opts.export_runtime_dir) / "fsm.hpp").string();
        std::string export_err;
        if (!RuntimeExporter::export_runtime(runtime_file_path, opts.cpp_standard, export_err)) {
            std::cerr << "Error exporting runtime: " << export_err << "\n";
            return 1;
        }
        return 0;
    }

    // Input Validation
    if (opts.input_file.empty()) {
        std::cerr << "Error: No input model file specified.\n\n";
        print_help(argv[0]);
        return 1;
    }

    // Read input model file
    std::ifstream file_stream(opts.input_file);
    if (!file_stream.is_open()) {
        std::cerr << "Error: Could not open input model file: " << opts.input_file << "\n";
        return 1;
    }
    std::stringstream buffer;
    buffer << file_stream.rdbuf();
    const std::string content = buffer.str();

    // Select parser using unified ParserFactory
    auto parser = ParserFactory::create(opts.input_file, opts.format);
    if (!parser) {
        std::cerr << "Error: Could not instantiate parser for input: " << opts.input_file << "\n";
        return 1;
    }

    FsmIr model;
    std::string parse_error;
    if (!parser->parse(content, model, parse_error)) {
        std::cerr << "Syntax Error in " << opts.input_file << ":\n" << parse_error << "\n";
        return 1;
    }

    // Diagram / Heuristic Frontend Validation and Warning
    if (parser->kind() == FrontendKind::Diagram) {
        std::cerr << "warning[W0301]: Untyped or inferred symbol in diagram source: '" << opts.input_file << "'\n";
        if (!opts.allow_diagram_codegen && !opts.verify_mode && opts.export_diagram_format.empty()) {
            std::cerr << "\n[ERROR] Direct code generation blocked: '" << opts.input_file
                      << "' is a visual diagram format (" << parser->format_name()
                      << ").\nPass '--allow-diagram-codegen' (or '--allow-tier2-codegen') to allow heuristic code "
                         "generation, or use '--verify' / '--export <fmt>'.\n";
            return 1;
        }
    }

    // Configure model properties
    if (!opts.fsm_name.empty()) {
        model.name = opts.fsm_name;
    } else if (model.name.empty() || model.name == "MyStateMachine") {
        model.name = infer_fsm_name_from_file(opts.input_file);
    }
    if (!opts.ns_name.empty()) {
        model.ns = opts.ns_name;
    }
    if (!opts.context_type.empty()) {
        model.context_type = opts.context_type;
    }
    model.thread_safe = opts.thread_safe;

    // Requirement Traceability Audit
    if (opts.req_audit) {
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
        std::cout << " Total Traceability Links Verified: " << req_count << "\n"
                  << "============================================================================\n";
    }

    // Middle-End Optimization & Verification Pipeline
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
            // Resolver for external submachines
            std::map<std::string, FsmIr> loaded_submachines;
            auto sub_pass = std::make_unique<SubmachineInliningPass>([&](const std::string& sub_name) -> const FsmIr* {
                if (loaded_submachines.count(sub_name) != 0) {
                    return &loaded_submachines[sub_name];
                }
                fs::path search_dir = opts.submachine_dir.empty() ? fs::path(opts.input_file).parent_path()
                                                                  : fs::path(opts.submachine_dir);
                // Look for submachine files with supported extensions
                for (const auto& ext : {".sysml", ".puml", ".mmd", ".xmi", ".scxml", ".json", ".dot"}) {
                    fs::path candidate = search_dir / (sub_name + ext);
                    if (fs::exists(candidate)) {
                        std::ifstream sub_file(candidate);
                        if (sub_file.is_open()) {
                            std::stringstream sub_ss;
                            sub_ss << sub_file.rdbuf();
                            auto sub_parser = ParserFactory::create(candidate.string(), "auto");
                            if (sub_parser) {
                                FsmIr sub_ir;
                                std::string sub_err;
                                if (sub_parser->parse(sub_ss.str(), sub_ir, sub_err)) {
                                    loaded_submachines[sub_name] = std::move(sub_ir);
                                    return &loaded_submachines[sub_name];
                                }
                            }
                        }
                    }
                }
                return nullptr;
            });
            // Wrap SubmachineInliningPass into IPass
            class SubmachineWrapper : public IPass {
              public:
                explicit SubmachineWrapper(std::unique_ptr<SubmachineInliningPass> pass) : pass_(std::move(pass)) {}
                [[nodiscard]] std::string name() const override { return SubmachineInliningPass::name(); }
                [[nodiscard]] std::string description() const override { return SubmachineInliningPass::description(); }
                bool run(FsmIr& ir, DiagnosticEngine& diag) override { return pass_->run(ir, diag); }

              private:
                std::unique_ptr<SubmachineInliningPass> pass_;
            };
            pm.add_pass(std::make_unique<SubmachineWrapper>(std::move(sub_pass)));
        }
        if (opts.prune_dead_states || opts.opt_level >= 2) {
            pm.add_pass(std::make_unique<DeadStatePruningPassWrapper>(true));
        }
        pm.add_pass(std::make_unique<ChoiceCompletenessPass>());
        pm.add_pass(std::make_unique<ModelSafetyVerifierPass>());
        pm.add_pass(std::make_unique<ModelCheckingPass>());

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
    const ValidationResult validation = FsmValidator::validate(model);

    if (opts.verify_mode) {
        std::size_t total_deferred = 0;
        for (const auto& state_item : model.states) {
            total_deferred += state_item.deferred_events.size();
        }

        std::cout << "============================================================================\n"
                  << " Formal Model Verification Report: " << model.name << "\n"
                  << "============================================================================\n"
                  << " Input File:       " << opts.input_file << "\n"
                  << " States:           " << model.states.size() << "\n"
                  << " Total Events:     " << model.events.size() << "\n"
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

        std::cout << "----------------------------------------------------------------------------\n"
                  << " Verification Status: "
                  << (validation.is_valid ? "PASSED (Model Sound)" : "FAILED (Errors Detected)") << "\n"
                  << "============================================================================\n";

        return validation.is_valid ? 0 : 1;
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

    // Handle Diagram Export (--export / -e) using unified EmitterFactory
    if (!opts.export_diagram_format.empty()) {
        std::string exported_diagram = EmitterFactory::emit_diagram(model, opts.export_diagram_format);
        if (exported_diagram.empty()) {
            std::cerr << "Error: Unsupported export diagram format: '" << opts.export_diagram_format
                      << "'. Supported: mermaid, plantuml, sysml2, json, dot, scxml, cameo, smv\n";
            return 1;
        }

        if (!opts.output_file.empty()) {
            std::ofstream output_stream(opts.output_file);
            if (!output_stream.is_open()) {
                std::cerr << "Error: Could not write output file: " << opts.output_file << "\n";
                return 1;
            }
            output_stream << exported_diagram;
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

    const std::string generated_code = CppGenerator::generate_header(model, gen_opts);

    // Write output
    if (!opts.output_file.empty()) {
        std::ofstream output_stream(opts.output_file);
        if (!output_stream.is_open()) {
            std::cerr << "Error: Could not write output file: " << opts.output_file << "\n";
            return 1;
        }
        output_stream << generated_code;
        const std::string std_label = (opts.cpp_standard == CppStandard::Cpp20) ? "C++20" : "C++17";
        std::cout << "[SUCCESS] " << std_label << " (" << (opts.standalone ? "Standalone" : "Modular")
                  << ") generated successfully to: " << opts.output_file << "\n"
                  << "  - States: " << model.states.size() << "\n"
                  << "  - Events: " << model.events.size() << "\n"
                  << "  - Transitions: " << model.transitions.size() << "\n"
                  << "  - Guards: " << model.guards.size() << "\n"
                  << "  - Actions: " << model.actions.size() << "\n";
    } else {
        std::cout << generated_code;
    }

    return 0;
}

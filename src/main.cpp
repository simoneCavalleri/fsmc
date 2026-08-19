#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "codegen/cameo_xmi_parser.hpp"
#include "codegen/cpp_generator.hpp"
#include "codegen/dot_parser.hpp"
#include "codegen/fsm_model.hpp"
#include "codegen/fsm_validator.hpp"
#include "codegen/json_parser.hpp"
#include "codegen/mermaid_parser.hpp"
#include "codegen/mermaid_serializer.hpp"
#include "codegen/parser_interface.hpp"
#include "codegen/plantuml_parser.hpp"
#include "codegen/plantuml_serializer.hpp"
#include "codegen/runtime_exporter.hpp"
#include "codegen/scxml_parser.hpp"
#include "codegen/sysml2_parser.hpp"
#include "codegen/sysml2_serializer.hpp"

namespace fs = std::filesystem;
using namespace fsm::codegen;

namespace {

struct CliOptions {
    std::string input_file;
    std::string output_file;
    std::string export_runtime_dir;
    std::string export_diagram_format;
    std::string fsm_name;
    std::string ns_name = "fsm_generated";
    std::string context_type = "no_context";
    std::string format = "auto";
    CppStandard cpp_standard = CppStandard::Cpp17;
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
    std::cout << "====================================================================="
                 "=======\n"
              << " fsmc : Universal OMG UML 2.5, SysML v2, SCXML, JSON & DOT C++ FSM Compiler\n"
              << "====================================================================="
                 "=======\n\n"
              << "Usage: " << prog_name << " -i <model_file> [OPTIONS]\n"
              << "       " << prog_name << " -i <model_file> --export <mermaid|plantuml|sysml2> -o <out_file>\n"
              << "       " << prog_name << " -i <model_file> --verify\n"
              << "       " << prog_name << " --export-runtime <dir> [--std 17|20]\n\n"
              << "Options:\n"
              << "  -i, --input <file>        Input model file (.xmi, .scxml, .json, .dot, "
                 ".sysml, .puml, .mmd) [Required]\n"
              << "  -o, --output <file>       Output C++ header or diagram file (default: "
                 "stdout)\n"
              << "  -e, --export <fmt>        Export diagram to format: 'mermaid', 'plantuml', "
                 "'sysml2'\n"
              << "  --verify, --check         Run formal model checker (livelock, choice, deadlock) and exit\n"
              << "  -n, --name <name>         FSM class name (default: inferred from "
                 "file name or 'MyFSM')\n"
              << "  --namespace <ns>          C++ namespace (default: "
                 "'fsm_generated')\n"
              << "  --context <type>          Context struct/class name (default: "
                 "'no_context')\n"
              << "  --std <17|20>             Target C++ standard ('17' or '20', "
                 "default: 17)\n"
              << "  --c++17                   Target C++17 standard\n"
              << "  --c++20                   Target C++20 standard\n"
              << "  --standalone              Generate a self-contained header with "
                 "embedded runtime (default)\n"
              << "  --modular                 Generate FSM header only, including "
                 "external fsm/fsm.hpp\n"
              << "  --export-runtime <dir>    Export the FSM runtime library to the "
                 "specified directory\n"
              << "  --format <fmt>            Model format: 'cameo', 'scxml', 'json', 'dot', "
                 "'sysml2', 'plantuml', 'mermaid', 'auto' (default: auto)\n"
              << "  --no-thread-safe          Do not generate thread_safe_fsm wrapper "
                 "alias\n"
              << "  --no-stubs                Do not include default stub functors for "
                 "guards and actions\n"
              << "  -h, --help                Show this help message and exit\n"
              << "  -v, --version             Show version information and exit\n\n"
              << "Examples:\n"
              << "  " << prog_name
              << " -i protocol.scxml -o protocol_fsm.hpp --std 20 --namespace net "
                 "--name ProtocolFSM\n"
              << "  " << prog_name << " -i model.xmi --export mermaid -o model.mmd\n"
              << "  " << prog_name << " -i model.sysml --export plantuml -o model.puml\n"
              << "  " << prog_name << " --export-runtime ./include/fsm --std 20\n\n";
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
        } else if ((arg == "-e" || arg == "--export") && idx + 1 < argc) {
            opts.export_diagram_format = argv[++idx];
        } else if ((arg == "-n" || arg == "--name") && idx + 1 < argc) {
            opts.fsm_name = argv[++idx];
        } else if (arg == "--namespace" && idx + 1 < argc) {
            opts.ns_name = argv[++idx];
        } else if (arg == "--context" && idx + 1 < argc) {
            opts.context_type = argv[++idx];
        } else if (arg == "--format" && idx + 1 < argc) {
            opts.format = argv[++idx];
        } else if (arg == "--export-runtime" && idx + 1 < argc) {
            opts.export_runtime_dir = argv[++idx];
        } else if (arg == "--std" && idx + 1 < argc) {
            const std::string std_str = argv[++idx];
            if (std_str == "20" || std_str == "c++20" || std_str == "C++20") {
                opts.cpp_standard = CppStandard::Cpp20;
            } else {
                opts.cpp_standard = CppStandard::Cpp17;
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
        } else {
            opts.is_valid = false;
            opts.error_message = "Unknown or incomplete argument: " + arg;
            return opts;
        }
    }

    return opts;
}

std::unique_ptr<IParser> create_parser(const std::string& input_file, const std::string& format_option) {
    if (format_option == "scxml") {
        return std::make_unique<ScxmlParser>();
    }
    if (format_option == "json") {
        return std::make_unique<JsonStateParser>();
    }
    if (format_option == "dot" || format_option == "gv") {
        return std::make_unique<DotParser>();
    }
    if (format_option == "cameo" || format_option == "xmi" || format_option == "magicdraw") {
        return std::make_unique<CameoXmiParser>();
    }
    if (format_option == "sysml" || format_option == "sysml2") {
        return std::make_unique<Sysml2Parser>();
    }
    if (format_option == "plantuml" || format_option == "puml") {
        return std::make_unique<PlantUmlParser>();
    }
    if (format_option == "mermaid") {
        return std::make_unique<MermaidParser>();
    }
    const std::string ext = fs::path(input_file).extension().string();
    if (ext == ".scxml") {
        return std::make_unique<ScxmlParser>();
    }
    if (ext == ".json") {
        return std::make_unique<JsonStateParser>();
    }
    if (ext == ".dot" || ext == ".gv") {
        return std::make_unique<DotParser>();
    }
    if (ext == ".xmi" || ext == ".xml" || ext == ".mdxml" || ext == ".uml") {
        return std::make_unique<CameoXmiParser>();
    }
    if (ext == ".sysml") {
        return std::make_unique<Sysml2Parser>();
    }
    if (ext == ".puml" || ext == ".plantuml") {
        return std::make_unique<PlantUmlParser>();
    }
    return std::make_unique<MermaidParser>();
}

}  // namespace

int main(int argc, char* argv[]) {
    const CliOptions opts = parse_cli_args(argc, argv);

    if (opts.show_help) {
        print_help(argv[0]);
        return 0;
    }
    if (opts.show_version) {
        std::cout << "fsm-gen v1.0.0 (C++ State Machine Compiler)\n";
        return 0;
    }
    if (!opts.is_valid) {
        std::cerr << "Error: " << opts.error_message << "\n";
        print_help(argv[0]);
        return 1;
    }

    // Handle runtime export request
    if (!opts.export_runtime_dir.empty()) {
        std::string export_err;
        if (!RuntimeExporter::export_runtime(opts.export_runtime_dir, opts.cpp_standard, export_err)) {
            std::cerr << "Error exporting runtime: " << export_err << "\n";
            return 1;
        }
        return 0;
    }

    if (opts.input_file.empty()) {
        std::cerr << "Error: Missing required argument: -i / --input <file> (or "
                     "--export-runtime <dir>)\n";
        print_help(argv[0]);
        return 1;
    }

    // Read input diagram file
    std::ifstream input_stream(opts.input_file);
    if (!input_stream.is_open()) {
        std::cerr << "Error: Could not open input file: " << opts.input_file << "\n";
        return 1;
    }

    std::stringstream buffer;
    buffer << input_stream.rdbuf();
    const std::string content = buffer.str();

    std::unique_ptr<IParser> parser = create_parser(opts.input_file, opts.format);

    FsmModel model;
    if (opts.fsm_name.empty()) {
        const std::string stem = fs::path(opts.input_file).stem().string();
        model.name = sanitize_identifier(stem);
        if (model.name.empty()) {
            model.name = "GeneratedFSM";
        }
    } else {
        model.name = sanitize_identifier(opts.fsm_name);
    }
    model.ns = sanitize_identifier(opts.ns_name);
    model.context_type = opts.context_type;
    model.thread_safe = opts.thread_safe;

    std::string parse_err;
    if (!parser->parse(content, model, parse_err)) {
        std::cerr << "Error parsing diagram:\n  " << parse_err << "\n";
        return 1;
    }

    // Formal Verification & Semantic Validation
    const auto validation = FsmValidator::validate(model);

    // If in verification mode, output formal report and exit
    if (opts.verify_mode) {
        size_t total_deferred = 0;
        for (const auto& s : model.states) {
            total_deferred += s.deferred_events.size();
        }

        std::cout << "============================================================================\n"
                  << "                  fsmc Formal Model Checker Report                          \n"
                  << "============================================================================\n"
                  << " Model Name:       " << model.name << "\n"
                  << " Total States:     " << model.states.size() << "\n"
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
        std::cout << "[WARNING] " << warn_msg << "\n";
    }

    if (!validation.is_valid) {
        std::cerr << "Semantic validation errors in model:\n";
        for (const auto& err : validation.errors) {
            std::cerr << "  - " << err << "\n";
        }
        return 1;
    }

    // Handle Diagram Export (--export / -e)
    if (!opts.export_diagram_format.empty()) {
        std::string exported_diagram;
        if (opts.export_diagram_format == "mermaid" || opts.export_diagram_format == "mmd") {
            exported_diagram = MermaidSerializer::serialize(model);
        } else if (opts.export_diagram_format == "plantuml" || opts.export_diagram_format == "puml") {
            exported_diagram = PlantUmlSerializer::serialize(model);
        } else if (opts.export_diagram_format == "sysml" || opts.export_diagram_format == "sysml2") {
            exported_diagram = Sysml2Serializer::serialize(model);
        } else {
            std::cerr << "Error: Unsupported export diagram format: '" << opts.export_diagram_format
                      << "'. Supported: mermaid, plantuml, sysml2\n";
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

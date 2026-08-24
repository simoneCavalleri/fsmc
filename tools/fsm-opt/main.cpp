#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/backend/emitters/cameo_serializer.hpp"
#include "fsm/backend/emitters/dot_serializer.hpp"
#include "fsm/backend/emitters/json_serializer.hpp"
#include "fsm/backend/emitters/mermaid_serializer.hpp"
#include "fsm/backend/emitters/plantuml_serializer.hpp"
#include "fsm/backend/emitters/scxml_serializer.hpp"
#include "fsm/backend/emitters/sysml2_serializer.hpp"
#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/frontend/cameo_xmi_parser.hpp"
#include "fsm/frontend/dot_parser.hpp"
#include "fsm/frontend/json_parser.hpp"
#include "fsm/frontend/mermaid_parser.hpp"
#include "fsm/frontend/parser_interface.hpp"
#include "fsm/frontend/plantuml_parser.hpp"
#include "fsm/frontend/scxml_parser.hpp"
#include "fsm/frontend/sysml2_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"
#include "fsm/middleend/pass_manager.hpp"

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
        << "IR Optimization & Emission:\n"
        << "  --emit-ir                 Emit optimized canonical JSON Intermediate Representation (default)\n"
        << "  --emit-puml               Emit canonical PlantUML state diagram\n"
        << "  --emit-mmd                Emit canonical Mermaid stateDiagram-v2\n"
        << "  --emit-sysml              Emit canonical OMG SysML v2 state definition\n"
        << "  --emit-json               Emit canonical XState JSON\n"
        << "  --emit-dot                Emit canonical Graphviz DOT diagram\n"
        << "  --emit-scxml              Emit canonical W3C SCXML statechart\n"
        << "  --emit-cameo              Emit canonical Cameo / MagicDraw OMG XMI 2.1\n\n"
        << "Analysis & Diagnostics:\n"
        << "  --profile                 Print PassManager execution times and optimization stats\n"
        << "  --verify, --check         Run formal model checker passes without emitting transformed model\n\n"
        << "General Options:\n"
        << "  -h, --help                Show this help message and exit\n"
        << "  -v, --version             Show version information and exit\n\n"
        << "Examples:\n"
        << "  " << prog_name << " -i model.sysml --emit-ir -o model.ir.json\n"
        << "  " << prog_name << " -i controller.puml --emit-mmd -o controller.mmd\n"
        << "  " << prog_name << " -i protocol.scxml --profile\n"
        << "  " << prog_name << " -i aerospace.sysml --verify\n\n";
}

std::unique_ptr<IParser> create_parser(std::string_view input_path, std::string_view format_override) {
    if (!format_override.empty() && format_override != "auto") {
        if (format_override == "sysml" || format_override == "sysml2") {
            return std::make_unique<Sysml2Parser>();
        }
        if (format_override == "plantuml" || format_override == "puml") {
            return std::make_unique<PlantUmlParser>();
        }
        if (format_override == "mermaid" || format_override == "mmd") {
            return std::make_unique<MermaidParser>();
        }
        if (format_override == "cameo" || format_override == "xmi") {
            return std::make_unique<CameoXmiParser>();
        }
        if (format_override == "scxml") {
            return std::make_unique<ScxmlParser>();
        }
        if (format_override == "json") {
            return std::make_unique<JsonStateParser>();
        }
        if (format_override == "dot") {
            return std::make_unique<DotParser>();
        }
    }

    if (input_path.ends_with(".mmd") || input_path.ends_with(".mermaid")) {
        return std::make_unique<MermaidParser>();
    }
    if (input_path.ends_with(".sysml")) {
        return std::make_unique<Sysml2Parser>();
    }
    if (input_path.ends_with(".xmi") || input_path.ends_with(".xml")) {
        return std::make_unique<CameoXmiParser>();
    }
    if (input_path.ends_with(".scxml")) {
        return std::make_unique<ScxmlParser>();
    }
    if (input_path.ends_with(".json")) {
        return std::make_unique<JsonStateParser>();
    }
    if (input_path.ends_with(".dot")) {
        return std::make_unique<DotParser>();
    }

    // Default to PlantUML parser for .puml, .plantuml or unspecified text files
    return std::make_unique<PlantUmlParser>();
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
    std::string emit_format = "ir";
    bool profile = false;
    bool verify_only = false;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        }
        if (arg == "-v" || arg == "--version") {
            std::cout << "fsm-opt v1.0.0 (The Universal Finite State Machine Compiler Infrastructure)\n";
            return 0;
        }
        if ((arg == "-i" || arg == "--input") && i + 1 < argc) {
            input_path = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "--format" && i + 1 < argc) {
            format_override = argv[++i];
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

    std::unique_ptr<IParser> parser = create_parser(input_path, format_override);

    FsmIr ir;
    std::string err;
    if (!parser->parse(content, ir, err)) {
        std::cerr << "Frontend Parse Error: " << err << "\n";
        return 1;
    }

    // Execute Middle-End Optimization & Verification Passes
    PassManager pm;
    pm.add_pass(std::make_unique<HierarchyCanonicalizationPass>());
    pm.add_pass(std::make_unique<ChoiceCompletenessPass>());
    pm.add_pass(std::make_unique<ModelSafetyVerifierPass>());

    DiagnosticEngine diag;
    if (!pm.run(ir, diag)) {
        std::cerr << diag.render_to_string(content);
        return 1;
    }

    if (!diag.get_diagnostics().empty()) {
        std::cerr << diag.render_to_string(content);
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

    // Emit transformed result
    std::string output_str;
    if (emit_format == "ir") {
        output_str = FsmIrSerializer::serialize_json(ir);
    } else if (emit_format == "puml") {
        output_str = PlantUmlSerializer::serialize(ir);
    } else if (emit_format == "mmd") {
        output_str = MermaidSerializer::serialize(ir);
    } else if (emit_format == "sysml") {
        output_str = Sysml2Serializer::serialize(ir);
    } else if (emit_format == "json") {
        output_str = JsonSerializer::serialize(ir);
    } else if (emit_format == "dot") {
        output_str = DotSerializer::serialize(ir);
    } else if (emit_format == "scxml") {
        output_str = ScxmlSerializer::serialize(ir);
    } else if (emit_format == "cameo") {
        output_str = CameoSerializer::serialize(ir);
    }

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

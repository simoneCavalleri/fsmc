#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/frontend/diagram/dot_parser.hpp"
#include "fsm/frontend/diagram/json_parser.hpp"
#include "fsm/frontend/diagram/mermaid_parser.hpp"
#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/frontend/formal/cameo_xmi_parser.hpp"
#include "fsm/frontend/formal/scxml_parser.hpp"
#include "fsm/frontend/formal/smv_parser.hpp"
#include "fsm/frontend/formal/sysml2_parser.hpp"
#include "fsm/frontend/common/parser_interface.hpp"

namespace fsm::codegen {

class ParserFactory {
  public:
    static std::unique_ptr<IParser> create_by_format(std::string_view format_name) {
        if (format_name == "sysml" || format_name == "sysml2") {
            return std::make_unique<Sysml2Parser>();
        }
        if (format_name == "plantuml" || format_name == "puml") {
            return std::make_unique<PlantUmlParser>();
        }
        if (format_name == "mermaid" || format_name == "mmd") {
            return std::make_unique<MermaidParser>();
        }
        if (format_name == "cameo" || format_name == "xmi" || format_name == "magicdraw") {
            return std::make_unique<CameoXmiParser>();
        }
        if (format_name == "scxml") {
            return std::make_unique<ScxmlParser>();
        }
        if (format_name == "smv" || format_name == "nusmv" || format_name == "nuxmv") {
            return std::make_unique<SmvParser>();
        }
        if (format_name == "json") {
            return std::make_unique<JsonStateParser>();
        }
        if (format_name == "dot" || format_name == "gv") {
            return std::make_unique<DotParser>();
        }
        return nullptr;
    }

    static std::unique_ptr<IParser> create_by_extension(std::string_view file_path) {
        namespace fs = std::filesystem;
        const std::string ext = fs::path(file_path).extension().string();
        if (ext == ".sysml") {
            return std::make_unique<Sysml2Parser>();
        }
        if (ext == ".puml" || ext == ".plantuml") {
            return std::make_unique<PlantUmlParser>();
        }
        if (ext == ".mmd" || ext == ".mermaid") {
            return std::make_unique<MermaidParser>();
        }
        if (ext == ".xmi" || ext == ".xml" || ext == ".mdxml" || ext == ".uml") {
            return std::make_unique<CameoXmiParser>();
        }
        if (ext == ".scxml") {
            return std::make_unique<ScxmlParser>();
        }
        if (ext == ".smv") {
            return std::make_unique<SmvParser>();
        }
        if (ext == ".json") {
            return std::make_unique<JsonStateParser>();
        }
        if (ext == ".dot" || ext == ".gv") {
            return std::make_unique<DotParser>();
        }
        return nullptr;
    }

    static std::unique_ptr<IParser> create(std::string_view file_path, std::string_view format_override = "") {
        if (!format_override.empty() && format_override != "auto") {
            auto parser = create_by_format(format_override);
            if (parser) {
                return parser;
            }
        }
        auto parser = create_by_extension(file_path);
        if (parser) {
            return parser;
        }
        // Default fallback to PlantUML parser
        return std::make_unique<PlantUmlParser>();
    }

    static std::string detect_format_from_content(std::string_view source) {
        if (source.find("@startuml") != std::string_view::npos || source.find("@enduml") != std::string_view::npos)
            return "plantuml";
        if (source.find("stateDiagram") != std::string_view::npos ||
            source.find("stateDiagram-v2") != std::string_view::npos)
            return "mermaid";
        if (source.find("state def ") != std::string_view::npos || source.find("entry;") != std::string_view::npos ||
            source.find("item def ") != std::string_view::npos || source.find("attribute ") != std::string_view::npos ||
            source.find("event def ") != std::string_view::npos)
            return "sysml2";
        if (source.find("<scxml") != std::string_view::npos)
            return "scxml";
        if (source.find("MODULE") != std::string_view::npos &&
            (source.find("VAR") != std::string_view::npos || source.find("ASSIGN") != std::string_view::npos ||
             source.find("LTLSPEC") != std::string_view::npos))
            return "smv";
        if (source.find("digraph") != std::string_view::npos)
            return "dot";
        if (source.find("<xmi:") != std::string_view::npos || source.find("<uml:") != std::string_view::npos ||
            source.find("<packagedElement") != std::string_view::npos)
            return "cameo";
        size_t first = source.find_first_not_of(" \t\n\r");
        if (first != std::string_view::npos && source[first] == '{')
            return "json";
        return "";
    }

    static FrontendKind get_kind_for_format(std::string_view format_name) {
        std::string f(format_name);
        for (auto& c : f) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (f == "sysml" || f == "sysml2" || f == "scxml" || f == "cameo" || f == "xmi" || f == "smv" || f == "nusmv" ||
            f == "nuxmv") {
            return FrontendKind::Formal;
        }
        return FrontendKind::Diagram;
    }

    static std::vector<std::string> supported_formats() {
        return {"sysml2", "plantuml", "mermaid", "cameo", "scxml", "smv", "json", "dot"};
    }
};

}  // namespace fsm::codegen

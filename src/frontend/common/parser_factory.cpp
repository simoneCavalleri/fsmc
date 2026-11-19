#include "fsm/frontend/common/parser_factory.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "fsm/frontend/common/json_parser.hpp"
#include "fsm/frontend/diagram/dot_parser.hpp"
#include "fsm/frontend/diagram/mermaid_parser.hpp"
#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/frontend/formal/cameo_xmi_parser.hpp"
#include "fsm/frontend/formal/scxml_parser.hpp"
#include "fsm/frontend/formal/smv_parser.hpp"
#include "fsm/frontend/formal/stateflow_parser.hpp"
#include "fsm/frontend/formal/sysml2_parser.hpp"

namespace fsm::frontend {

using namespace diagram;
using namespace formal;

namespace {

struct FormatDescriptor {
    std::string_view canonical_name;
    std::vector<std::string_view> aliases;
    std::vector<std::string_view> extensions;
    FrontendKind kind;
    std::function<std::unique_ptr<IParser>()> factory;
};

std::string to_lower_ascii(std::string_view s) {
    std::string out(s);
    for (auto& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

const std::vector<FormatDescriptor>& format_registry() {
    static const std::vector<FormatDescriptor> kRegistry = {
        {"sysml2", {"sysml"}, {".sysml"}, FrontendKind::Formal, []() { return std::make_unique<Sysml2Parser>(); }},
        {"plantuml",
         {"puml"},
         {".puml", ".plantuml"},
         FrontendKind::Diagram,
         []() { return std::make_unique<PlantUmlParser>(); }},
        {"mermaid",
         {"mmd"},
         {".mmd", ".mermaid"},
         FrontendKind::Diagram,
         []() { return std::make_unique<MermaidParser>(); }},
        {"cameo",
         {"xmi", "magicdraw"},
         {".xmi", ".xml", ".mdxml", ".uml"},
         FrontendKind::Formal,
         []() { return std::make_unique<CameoXmiParser>(); }},
        {"scxml", {}, {".scxml"}, FrontendKind::Formal, []() { return std::make_unique<ScxmlParser>(); }},
        {"stateflow",
         {"sfx", "slx", "simulink"},
         {".sfx", ".stateflow", ".slx"},
         FrontendKind::Formal,
         []() { return std::make_unique<StateflowParser>(); }},
        {"smv", {"nusmv", "nuxmv"}, {".smv"}, FrontendKind::Formal, []() { return std::make_unique<SmvParser>(); }},
        {"json", {}, {".json"}, FrontendKind::Formal, []() { return std::make_unique<JsonParser>(); }},
        {"dot", {"gv"}, {".dot", ".gv"}, FrontendKind::Diagram, []() { return std::make_unique<DotParser>(); }},
    };
    return kRegistry;
}

const FormatDescriptor* find_by_format(std::string_view format_name) {
    const std::string lower = to_lower_ascii(format_name);
    for (const auto& desc : format_registry()) {
        if (desc.canonical_name == lower) {
            return &desc;
        }
        for (const auto& alias : desc.aliases) {
            if (alias == lower) {
                return &desc;
            }
        }
    }
    return nullptr;
}

}  // namespace

std::unique_ptr<IParser> ParserFactory::create_by_format(std::string_view format_name) {
    if (const auto* desc = find_by_format(format_name)) {
        return desc->factory();
    }
    return nullptr;
}

std::unique_ptr<IParser> ParserFactory::create_by_extension(std::string_view file_path) {
    const std::string ext = to_lower_ascii(std::filesystem::path(file_path).extension().string());
    for (const auto& desc : format_registry()) {
        for (const auto& e : desc.extensions) {
            if (e == ext) {
                return desc.factory();
            }
        }
    }
    return nullptr;
}

std::unique_ptr<IParser> ParserFactory::create(std::string_view file_path, std::string_view format_override) {
    if (!format_override.empty() && format_override != "auto") {
        return create_by_format(format_override);
    }
    const std::string ext = to_lower_ascii(std::filesystem::path(file_path).extension().string());
    if (ext != ".xml") {
        auto parser = create_by_extension(file_path);
        if (parser) {
            return parser;
        }
    }
    if (!file_path.empty() && file_path != "-" && file_path != "/dev/stdin") {
        std::error_code ec;
        std::filesystem::path p(file_path);
        if (std::filesystem::exists(p, ec) && std::filesystem::is_regular_file(p, ec)) {
            std::ifstream stream(p);
            if (stream.is_open()) {
                std::string head(4096, '\0');
                stream.read(head.data(), static_cast<std::streamsize>(head.size()));
                head.resize(static_cast<std::size_t>(stream.gcount()));
                std::string detected = detect_format_from_content(head);
                if (!detected.empty()) {
                    if (auto content_parser = create_by_format(detected)) {
                        return content_parser;
                    }
                }
            }
        }
    }
    if (ext == ".xml") {
        return std::make_unique<CameoXmiParser>();
    }
    // Default fallback to PlantUML parser
    return std::make_unique<PlantUmlParser>();
}

std::string ParserFactory::detect_format_from_content(std::string_view source) {
    if (source.size() >= 4 && source[0] == 'P' && source[1] == 'K' && source[2] == '\x03' && source[3] == '\x04') {
        return "stateflow";
    }
    if (source.find("@startuml") != std::string_view::npos || source.find("@enduml") != std::string_view::npos) {
        return "plantuml";
    }
    if (source.find("stateDiagram") != std::string_view::npos ||
        source.find("stateDiagram-v2") != std::string_view::npos) {
        return "mermaid";
    }
    // Check unambiguous XML/JSON/SMV/DOT headers BEFORE generic SysML v2 keywords like "attribute "
    if (source.find("<scxml") != std::string_view::npos) {
        return "scxml";
    }
    if (source.find("<Stateflow") != std::string_view::npos || source.find("<chart") != std::string_view::npos) {
        return "stateflow";
    }
    if (source.find("<xmi:") != std::string_view::npos || source.find("<uml:") != std::string_view::npos ||
        source.find("<packagedElement") != std::string_view::npos) {
        return "cameo";
    }
    if (source.find("MODULE") != std::string_view::npos &&
        (source.find("VAR") != std::string_view::npos || source.find("ASSIGN") != std::string_view::npos ||
         source.find("LTLSPEC") != std::string_view::npos)) {
        return "smv";
    }
    if (source.find("digraph") != std::string_view::npos) {
        return "dot";
    }
    size_t first = source.find_first_not_of(" \t\n\r");
    if (first != std::string_view::npos && source[first] == '{') {
        return "json";
    }
    if (source.find("state def ") != std::string_view::npos || source.find("entry;") != std::string_view::npos ||
        source.find("item def ") != std::string_view::npos || source.find("attribute ") != std::string_view::npos ||
        source.find("event def ") != std::string_view::npos) {
        return "sysml2";
    }
    return "";
}

FrontendKind ParserFactory::get_kind_for_format(std::string_view format_name) {
    if (const auto* desc = find_by_format(format_name)) {
        return desc->kind;
    }
    return FrontendKind::Diagram;
}

std::vector<std::string> ParserFactory::supported_formats() {
    std::vector<std::string> out;
    out.reserve(format_registry().size());
    for (const auto& desc : format_registry()) {
        out.emplace_back(desc.canonical_name);
    }
    return out;
}

}  // namespace fsm::frontend

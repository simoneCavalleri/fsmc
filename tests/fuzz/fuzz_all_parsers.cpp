/**
 * @file fuzz_all_parsers.cpp
 * @brief libFuzzer harness exercising all 9 FSM frontend parsers and SimpleXmlParser.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/frontend/common/parser_factory.hpp"
#include "fsm/frontend/common/xml_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < 2) {
        return 0;
    }
    static constexpr std::string_view kFormats[] = {"plantuml", "mermaid",   "dot", "sysml2", "scxml",
                                                    "cameo",    "stateflow", "smv", "json"};
    const std::size_t selector = data[0] % (sizeof(kFormats) / sizeof(kFormats[0]) + 1);
    const std::string_view input(reinterpret_cast<const char*>(data + 1), size - 1);

    if (selector == sizeof(kFormats) / sizeof(kFormats[0])) {
        std::string xml_err;
        (void)fsm::frontend::SimpleXmlParser::parse(input, xml_err);
        return 0;
    }

    auto parser = fsm::frontend::ParserFactory::create_by_format(kFormats[selector]);
    if (parser) {
        fsm::ir::FsmIr ir;
        fsm::diagnostic::DiagnosticEngine diag;
        (void)parser->parse(input, ir, diag);
    }
    return 0;
}

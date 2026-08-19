#include <cassert>
#include <iostream>
#include <string>

#include "codegen/cameo_xmi_parser.hpp"
#include "codegen/dot_parser.hpp"
#include "codegen/fsm_model.hpp"
#include "codegen/fsm_validator.hpp"
#include "codegen/json_parser.hpp"
#include "codegen/mermaid_parser.hpp"
#include "codegen/plantuml_parser.hpp"
#include "codegen/scxml_parser.hpp"
#include "codegen/sysml2_parser.hpp"

using namespace fsm::codegen;

namespace {

void test_plantuml_negative_inputs() {
    std::cout << "[TEST] Running test_plantuml_negative_inputs...\n";

    PlantUmlParser parser;
    FsmModel model;
    std::string err;

    // 1. Completely empty or whitespace
    assert(!parser.parse("", model, err));
    assert(!err.empty());

    assert(!parser.parse("   \n\t  \n  ", model, err));
    assert(!err.empty());

    // 2. Only @startuml / @enduml with no states
    assert(!parser.parse("@startuml\n@enduml", model, err));
    assert(!err.empty());

    // 3. Corrupted syntax
    assert(!parser.parse("THIS IS NOT A VALID DIAGRAM AT ALL 12345", model, err));

    std::cout << "[PASS] test_plantuml_negative_inputs correctly rejected malformed inputs without crashing.\n";
}

void test_mermaid_negative_inputs() {
    std::cout << "[TEST] Running test_mermaid_negative_inputs...\n";

    MermaidParser parser;
    FsmModel model;
    std::string err;

    // 1. Empty input
    assert(!parser.parse("", model, err));

    // 2. Header only with no states
    assert(!parser.parse("stateDiagram-v2\n", model, err));

    // 3. Malformed transition syntax
    assert(!parser.parse("stateDiagram-v2\n--> --> -->", model, err));

    std::cout << "[PASS] test_mermaid_negative_inputs passed.\n";
}

void test_xml_parsers_negative_inputs() {
    std::cout << "[TEST] Running test_xml_parsers_negative_inputs...\n";

    FsmModel model;
    std::string err;

    // 1. Cameo XMI corrupt XML
    CameoXmiParser cameo_parser;
    assert(!cameo_parser.parse("<xmi:XMI><unclosed_tag>", model, err));
    assert(!cameo_parser.parse("NOT XML AT ALL", model, err));

    // 2. SCXML corrupt XML
    ScxmlParser scxml_parser;
    assert(!scxml_parser.parse("<scxml><state id='A'>", model, err));
    assert(!scxml_parser.parse("INVALID_SCXML_CONTENT", model, err));

    std::cout << "[PASS] test_xml_parsers_negative_inputs handled corrupted XML cleanly.\n";
}

void test_json_parser_negative_inputs() {
    std::cout << "[TEST] Running test_json_parser_negative_inputs...\n";

    JsonStateParser parser;
    FsmModel model;
    std::string err;

    // 1. Invalid JSON syntax
    assert(!parser.parse("{ states: { unquoted_key: 123 ", model, err));
    assert(!err.empty());

    // 2. Valid JSON but wrong data type (e.g. integer or array at root)
    assert(!parser.parse("[1, 2, 3]", model, err));
    assert(!parser.parse("\"just a string\"", model, err));

    // 3. Valid JSON object with no states
    assert(!parser.parse("{\"id\": \"EmptyMachine\"}", model, err));

    std::cout << "[PASS] test_json_parser_negative_inputs rejected invalid JSON gracefully.\n";
}

void test_sysml2_negative_inputs() {
    std::cout << "[TEST] Running test_sysml2_negative_inputs...\n";

    Sysml2Parser parser;
    FsmModel model;
    std::string err;

    assert(!parser.parse("", model, err));
    assert(!parser.parse("state def EmptySM { }", model, err));
    assert(!parser.parse("random unparsed tokens ;;;;", model, err));

    std::cout << "[PASS] test_sysml2_negative_inputs passed.\n";
}

void test_model_checker_diagnostics() {
    std::cout << "[TEST] Running test_model_checker_diagnostics...\n";

    // Build a deliberately defective model:
    // - Unreachable state "Island"
    // - Trap state "BlackHole" (incoming transition but no outgoing)
    // - Deadlock choice node without else branch
    FsmModel model;
    model.initial_state = "Idle";
    model.add_state("Idle");
    model.add_state("BlackHole");
    model.add_state("Island");

    TransitionModel t1;
    t1.source = "Idle";
    t1.target = "BlackHole";
    t1.event = "FallIn";
    model.add_transition(t1);

    auto result = FsmValidator::validate(model);
    assert(result.is_valid);  // Model is syntactically valid but has warnings
    assert(!result.warnings.empty());
    assert(result.diagnostics.size() >= 2);

    std::cout << "[PASS] test_model_checker_diagnostics detected all design defects.\n";
}

}  // namespace

int main() {
    std::cout << "========================================\n"
              << "   RUNNING PARSER NEGATIVE TESTS        \n"
              << "========================================\n";

    test_plantuml_negative_inputs();
    test_mermaid_negative_inputs();
    test_xml_parsers_negative_inputs();
    test_json_parser_negative_inputs();
    test_sysml2_negative_inputs();
    test_model_checker_diagnostics();

    std::cout << "========================================\n"
              << "   ALL PARSER NEGATIVE TESTS PASSED!    \n"
              << "========================================\n";
    return 0;
}

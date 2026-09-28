/**
 * @file test_parser_negative.cpp
 * @brief Negative unit tests across front-end parsers verifying rejection of malformed, empty, or corrupt inputs.
 */

#include <gtest/gtest.h>

#include <string>

#include "fsm/frontend/common/json_parser.hpp"
#include "fsm/frontend/diagram/dot_parser.hpp"
#include "fsm/frontend/diagram/mermaid_parser.hpp"
#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/frontend/formal/cameo_xmi_parser.hpp"
#include "fsm/frontend/formal/scxml_parser.hpp"
#include "fsm/frontend/formal/sysml2_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/fsm_validator.hpp"

using namespace fsm::frontend;
using namespace fsm::frontend::diagram;
using namespace fsm::frontend::formal;
using namespace fsm::middleend::analysis;
using namespace fsm::middleend;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify PlantUmlParser rejects malformed, empty, and corrupted syntax with informative error strings.
 * @scenario Pass empty string, whitespace, header-only diagram, and corrupted tokens to PlantUmlParser.
 * @expected parse() returns false and populates descriptive error string.
 */
TEST(PlantUmlParser, EmptyAndCorruptedInputStreams_RejectsWithInformativeDiagnostic) {
    PlantUmlParser parser;
    FsmIr model;
    std::string err;

    // 1. Completely empty or whitespace
    EXPECT_FALSE(parser.parse("", model, err));
    EXPECT_FALSE(err.empty());

    EXPECT_FALSE(parser.parse("   \n\t  \n  ", model, err));
    EXPECT_FALSE(err.empty());

    // 2. Only @startuml / @enduml with no states
    EXPECT_FALSE(parser.parse("@startuml\n@enduml", model, err));
    EXPECT_FALSE(err.empty());

    // 3. Corrupted syntax
    EXPECT_FALSE(parser.parse("THIS IS NOT A VALID DIAGRAM AT ALL 12345", model, err));
}

/**
 * @brief Verify MermaidParser rejects empty inputs and malformed transition statements.
 * @scenario Empty input string, header-only diagram, and invalid transition arrow sequences.
 * @expected Parser returns false indicating syntactic rejection.
 */
TEST(MermaidParser, MissingStatesAndMalformedArrows_RejectsParseCleanly) {
    MermaidParser parser;
    FsmIr model;
    std::string err;

    // 1. Empty input
    EXPECT_FALSE(parser.parse("", model, err));

    // 2. Header only with no states
    EXPECT_FALSE(parser.parse("stateDiagram-v2\n", model, err));

    // 3. Malformed transition syntax
    EXPECT_FALSE(parser.parse("stateDiagram-v2\n--> --> -->", model, err));
}

/**
 * @brief Verify XML parsers (Cameo XMI and W3C SCXML) reject malformed XML tags and non-XML text.
 * @scenario Unclosed XML elements and arbitrary non-XML text fed to CameoXmiParser and ScxmlParser.
 * @expected Parsing terminates cleanly returning false without throwing unhandled exceptions.
 */
TEST(XmlFrontend, UnclosedTagsAndNonXmlStrings_FailsGracefullyWithoutThrowing) {
    FsmIr model;
    std::string err;

    // 1. Cameo XMI corrupt XML
    CameoXmiParser cameo_parser;
    EXPECT_FALSE(cameo_parser.parse("<xmi:XMI><unclosed_tag>", model, err));
    EXPECT_FALSE(cameo_parser.parse("NOT XML AT ALL", model, err));

    // 2. SCXML corrupt XML
    ScxmlParser scxml_parser;
    EXPECT_FALSE(scxml_parser.parse("<scxml><state id='A'>", model, err));
    EXPECT_FALSE(scxml_parser.parse("INVALID_SCXML_CONTENT", model, err));
}

/**
 * @brief Verify JsonParser rejects invalid JSON syntax, wrong root types, and empty objects.
 * @scenario Unquoted keys, array root, string root, and JSON object with no state definitions.
 * @expected Parser returns false and populates descriptive error message.
 */
TEST(JsonParser, MalformedSyntaxAndEmptyStateObjects_RejectsWithErrors) {
    JsonParser parser;
    FsmIr model;
    std::string err;

    // 1. Invalid JSON syntax
    EXPECT_FALSE(parser.parse("{ states: { unquoted_key: 123 ", model, err));
    EXPECT_FALSE(err.empty());

    // 2. Valid JSON but wrong data type (e.g. integer or array at root)
    EXPECT_FALSE(parser.parse("[1, 2, 3]", model, err));
    EXPECT_FALSE(parser.parse("\"just a string\"", model, err));

    // 3. Valid JSON object with no states
    EXPECT_FALSE(parser.parse("{\"id\": \"EmptyMachine\"}", model, err));
}

/**
 * @brief Verify Sysml2Parser rejects empty definitions and invalid token streams.
 * @scenario Empty input text, empty state def block, and random token strings.
 * @expected Parser returns false indicating invalid or empty SysML v2 state model.
 */
TEST(Sysml2Parser, EmptyStateMachineDefinitions_RejectsGracefully) {
    Sysml2Parser parser;
    FsmIr model;
    std::string err;

    EXPECT_FALSE(parser.parse("", model, err));
    EXPECT_FALSE(parser.parse("state def EmptySM { }", model, err));
    EXPECT_FALSE(parser.parse("random unparsed tokens ;;;;", model, err));
}

/**
 * @brief Verify FsmValidator semantic diagnostics (unreachable island states, trap/deadlock states).
 * @scenario Defective model with unreachable state 'Island' and trap state 'BlackHole' (no egress).
 * @expected FsmValidator flags model with multiple semantic warning diagnostics identifying design flaws.
 */
TEST(FsmValidator, DisconnectedSubgraphsAndTrapStates_EmitsSemanticWarningDiagnostics) {
    // Build a deliberately defective model:
    // - Unreachable state "Island"
    // - Trap state "BlackHole" (incoming transition but no outgoing)
    FsmIr model;
    model.initial_state = "Idle";
    model.add_state("Idle");
    model.add_state("BlackHole");
    model.add_state("Island");

    TransitionEdge t1;
    t1.source = "Idle";
    t1.target = "BlackHole";
    t1.event = "FallIn";
    model.add_transition(t1);

    auto result = FsmValidator::validate(model);
    EXPECT_TRUE(result.is_valid);  // Syntactically valid, has semantic warnings
    EXPECT_FALSE(result.warnings.empty());
    EXPECT_GE(result.diagnostics.size(), 2u);
}

}  // namespace

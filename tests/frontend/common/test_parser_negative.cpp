#include <gtest/gtest.h>

#include <string>

#include "fsm/frontend/diagram/dot_parser.hpp"
#include "fsm/frontend/diagram/json_parser.hpp"
#include "fsm/frontend/diagram/mermaid_parser.hpp"
#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/frontend/formal/cameo_xmi_parser.hpp"
#include "fsm/frontend/formal/scxml_parser.hpp"
#include "fsm/frontend/formal/sysml2_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/fsm_validator.hpp"

using namespace fsm::codegen;

namespace {

/**
 * @brief Test Intent: Verify PlantUmlParser rejects malformed, empty, and corrupted syntax with informative error
 * strings.
 *
 * Scenario:
 * - Pass empty string, header-only diagram, and corrupted tokens to PlantUmlParser.
 * - Verify parse() returns false and populates the error string.
 */
TEST(ParserNegativeTest, PlantUmlRejectsMalformedInputs) {
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
 * @brief Test Intent: Verify MermaidParser rejects empty inputs and malformed transition statements.
 *
 * Scenario:
 * - Test empty input, header-only diagram, and invalid transition arrows.
 * - Verify parse failure is reported cleanly.
 */
TEST(ParserNegativeTest, MermaidRejectsMalformedInputs) {
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
 * @brief Test Intent: Verify XML parsers (Cameo XMI and W3C SCXML) reject malformed XML tags and non-XML text.
 *
 * Scenario:
 * - Feed unclosed XML tags and plain text to CameoXmiParser and ScxmlParser.
 * - Verify parsing fails without exceptions.
 */
TEST(ParserNegativeTest, XmlParsersRejectCorruptInputs) {
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
 * @brief Test Intent: Verify JsonStateParser rejects invalid JSON syntax, wrong root types, and empty objects.
 *
 * Scenario:
 * - Pass unquoted keys, JSON arrays, and state machines with 0 states.
 * - Verify rejection and non-empty error message.
 */
TEST(ParserNegativeTest, JsonParserRejectsInvalidInputs) {
    JsonStateParser parser;
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
 * @brief Test Intent: Verify Sysml2Parser rejects empty definitions and invalid token streams.
 *
 * Scenario:
 * - Pass empty text, empty state def blocks, and invalid tokens to Sysml2Parser.
 * - Verify parser returns false.
 */
TEST(ParserNegativeTest, Sysml2RejectsMalformedInputs) {
    Sysml2Parser parser;
    FsmIr model;
    std::string err;

    EXPECT_FALSE(parser.parse("", model, err));
    EXPECT_FALSE(parser.parse("state def EmptySM { }", model, err));
    EXPECT_FALSE(parser.parse("random unparsed tokens ;;;;", model, err));
}

/**
 * @brief Test Intent: Verify FsmValidator semantic diagnostics (unreachable island states, trap/deadlock states).
 *
 * Scenario:
 * - Construct model with unreachable state "Island" and trap state "BlackHole" (no exit transitions).
 * - Verify FsmValidator emits semantic warnings for both design defects.
 */
TEST(ParserNegativeTest, ModelCheckerDetectsDefects) {
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

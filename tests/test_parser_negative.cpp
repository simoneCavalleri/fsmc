#include <gtest/gtest.h>

#include <string>

#include "fsm/frontend/cameo_xmi_parser.hpp"
#include "fsm/frontend/dot_parser.hpp"
#include "fsm/frontend/json_parser.hpp"
#include "fsm/frontend/mermaid_parser.hpp"
#include "fsm/frontend/plantuml_parser.hpp"
#include "fsm/frontend/scxml_parser.hpp"
#include "fsm/frontend/sysml2_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/fsm_validator.hpp"

using namespace fsm::codegen;

namespace {

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

TEST(ParserNegativeTest, Sysml2RejectsMalformedInputs) {
    Sysml2Parser parser;
    FsmIr model;
    std::string err;

    EXPECT_FALSE(parser.parse("", model, err));
    EXPECT_FALSE(parser.parse("state def EmptySM { }", model, err));
    EXPECT_FALSE(parser.parse("random unparsed tokens ;;;;", model, err));
}

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

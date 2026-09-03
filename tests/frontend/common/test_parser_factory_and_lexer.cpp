#include <gtest/gtest.h>

#include "fsm/frontend/common/lexer_utils.hpp"
#include "fsm/frontend/common/parser_factory.hpp"
#include "fsm/frontend/diagram/dot_parser.hpp"
#include "fsm/frontend/diagram/json_parser.hpp"
#include "fsm/frontend/diagram/mermaid_parser.hpp"
#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/frontend/formal/cameo_xmi_parser.hpp"
#include "fsm/frontend/formal/scxml_parser.hpp"
#include "fsm/frontend/formal/smv_parser.hpp"
#include "fsm/frontend/formal/sysml2_parser.hpp"

namespace {

using namespace fsm::codegen;

// ============================================================================
// 1. Parser Factory Format and Extension Resolution Tests
// ============================================================================

TEST(ParserFactoryAndLexerTest, ParserFactoryFormatResolution) {
    auto p_sysml = ParserFactory::create_by_format("sysml2");
    EXPECT_NE(dynamic_cast<Sysml2Parser*>(p_sysml.get()), nullptr);

    auto p_puml = ParserFactory::create_by_format("plantuml");
    EXPECT_NE(dynamic_cast<PlantUmlParser*>(p_puml.get()), nullptr);

    auto p_mmd = ParserFactory::create_by_format("mermaid");
    EXPECT_NE(dynamic_cast<MermaidParser*>(p_mmd.get()), nullptr);

    auto p_cameo = ParserFactory::create_by_format("cameo");
    EXPECT_NE(dynamic_cast<CameoXmiParser*>(p_cameo.get()), nullptr);

    auto p_scxml = ParserFactory::create_by_format("scxml");
    EXPECT_NE(dynamic_cast<ScxmlParser*>(p_scxml.get()), nullptr);

    auto p_smv = ParserFactory::create_by_format("smv");
    EXPECT_NE(dynamic_cast<SmvParser*>(p_smv.get()), nullptr);

    auto p_json = ParserFactory::create_by_format("json");
    EXPECT_NE(dynamic_cast<JsonStateParser*>(p_json.get()), nullptr);

    auto p_dot = ParserFactory::create_by_format("dot");
    EXPECT_NE(dynamic_cast<DotParser*>(p_dot.get()), nullptr);

    auto p_invalid = ParserFactory::create_by_format("unknown_format_xyz");
    EXPECT_EQ(p_invalid, nullptr);
}

TEST(ParserFactoryAndLexerTest, ParserFactoryExtensionResolution) {
    auto p_sysml = ParserFactory::create_by_extension("models/system.sysml");
    EXPECT_NE(dynamic_cast<Sysml2Parser*>(p_sysml.get()), nullptr);

    auto p_puml = ParserFactory::create_by_extension("diagram.puml");
    EXPECT_NE(dynamic_cast<PlantUmlParser*>(p_puml.get()), nullptr);

    auto p_plantuml = ParserFactory::create_by_extension("diagram.plantuml");
    EXPECT_NE(dynamic_cast<PlantUmlParser*>(p_plantuml.get()), nullptr);

    auto p_mmd = ParserFactory::create_by_extension("chart.mmd");
    EXPECT_NE(dynamic_cast<MermaidParser*>(p_mmd.get()), nullptr);

    auto p_mermaid = ParserFactory::create_by_extension("chart.mermaid");
    EXPECT_NE(dynamic_cast<MermaidParser*>(p_mermaid.get()), nullptr);

    auto p_smv = ParserFactory::create_by_extension("verify.smv");
    EXPECT_NE(dynamic_cast<SmvParser*>(p_smv.get()), nullptr);
    EXPECT_NE(dynamic_cast<MermaidParser*>(p_mermaid.get()), nullptr);

    auto p_xmi = ParserFactory::create_by_extension("model.xmi");
    EXPECT_NE(dynamic_cast<CameoXmiParser*>(p_xmi.get()), nullptr);

    auto p_xml = ParserFactory::create_by_extension("model.xml");
    EXPECT_NE(dynamic_cast<CameoXmiParser*>(p_xml.get()), nullptr);

    auto p_scxml = ParserFactory::create_by_extension("statechart.scxml");
    EXPECT_NE(dynamic_cast<ScxmlParser*>(p_scxml.get()), nullptr);

    auto p_json = ParserFactory::create_by_extension("machine.json");
    EXPECT_NE(dynamic_cast<JsonStateParser*>(p_json.get()), nullptr);

    auto p_dot = ParserFactory::create_by_extension("graph.dot");
    EXPECT_NE(dynamic_cast<DotParser*>(p_dot.get()), nullptr);

    auto p_gv = ParserFactory::create_by_extension("graph.gv");
    EXPECT_NE(dynamic_cast<DotParser*>(p_gv.get()), nullptr);
}

TEST(ParserFactoryAndLexerTest, ParserFactoryOverrideAndFallback) {
    // Override .puml with format "mermaid"
    auto p_override = ParserFactory::create("file.puml", "mermaid");
    EXPECT_NE(dynamic_cast<MermaidParser*>(p_override.get()), nullptr);

    // Fallback for unknown extension defaults to PlantUML parser
    auto p_fallback = ParserFactory::create("unknown.txt");
    EXPECT_NE(dynamic_cast<PlantUmlParser*>(p_fallback.get()), nullptr);

    auto formats = ParserFactory::supported_formats();
    EXPECT_GE(formats.size(), 7u);
}

// ============================================================================
// 2. LexerUtils Parsing and Token Extraction Tests
// ============================================================================

TEST(ParserFactoryAndLexerTest, LexerUtilsBracketedExtraction) {
    auto b1 = LexerUtils::extract_bracketed("State [x > 10 && y < 20] / Action", '[', ']');
    ASSERT_TRUE(b1.has_value());
    EXPECT_EQ(*b1, "x > 10 && y < 20");

    // Nested brackets
    auto b2 = LexerUtils::extract_bracketed("Guard [arr[i] == 5] -> Next", '[', ']');
    ASSERT_TRUE(b2.has_value());
    EXPECT_EQ(*b2, "arr[i] == 5");

    // No brackets
    auto b3 = LexerUtils::extract_bracketed("Plain identifier", '[', ']');
    EXPECT_FALSE(b3.has_value());

    // Curly braces
    auto b4 = LexerUtils::extract_bracketed("state S { do_something(); }", '{', '}');
    ASSERT_TRUE(b4.has_value());
    EXPECT_EQ(*b4, " do_something(); ");
}

TEST(ParserFactoryAndLexerTest, LexerUtilsQuotedExtraction) {
    auto q1 = LexerUtils::extract_quoted("\"DoubleQuotedName\"");
    ASSERT_TRUE(q1.has_value());
    EXPECT_EQ(*q1, "DoubleQuotedName");

    auto q2 = LexerUtils::extract_quoted("'SingleQuotedName'");
    ASSERT_TRUE(q2.has_value());
    EXPECT_EQ(*q2, "SingleQuotedName");

    auto q3 = LexerUtils::extract_quoted("UnquotedName");
    EXPECT_FALSE(q3.has_value());
}

TEST(ParserFactoryAndLexerTest, LexerUtilsParseTransitionLabel) {
    // 1. Full label: "event [guard] / action"
    auto [ev1, g1, a1] = LexerUtils::parse_transition_label("ConnectCmd [HasValidCredentials] / InitSocket");
    EXPECT_EQ(ev1, "ConnectCmd");
    ASSERT_TRUE(g1.has_value());
    EXPECT_EQ(*g1, "HasValidCredentials");
    ASSERT_TRUE(a1.has_value());
    EXPECT_EQ(*a1, "InitSocket");

    // 2. Event and Action only: "event / action"
    auto [ev2, g2, a2] = LexerUtils::parse_transition_label("TimeoutEvent / CloseSocket");
    EXPECT_EQ(ev2, "TimeoutEvent");
    EXPECT_FALSE(g2.has_value());
    ASSERT_TRUE(a2.has_value());
    EXPECT_EQ(*a2, "CloseSocket");

    // 3. Event and Guard only: "event [guard]"
    auto [ev3, g3, a3] = LexerUtils::parse_transition_label("RetryCmd [retry_count < 3]");
    EXPECT_EQ(ev3, "RetryCmd");
    ASSERT_TRUE(g3.has_value());
    EXPECT_EQ(*g3, "retry_count < 3");
    EXPECT_FALSE(a3.has_value());

    // 4. Plain Event only: "event"
    auto [ev4, g4, a4] = LexerUtils::parse_transition_label("HeartbeatTick");
    EXPECT_EQ(ev4, "HeartbeatTick");
    EXPECT_FALSE(g4.has_value());
    EXPECT_FALSE(a4.has_value());
}

/**
 * @brief Test Intent: Verify C++ reserved keyword detection and escaping utilities.
 *
 * Scenario:
 * - Verify standard C++ keywords (class, default, switch, volatile, template) return true from is_cpp_keyword.
 * - Verify non-keywords return false.
 * - Verify escape_cpp_keyword appends trailing underscore to keywords and preserves user identifiers.
 */
TEST(ParserFactoryAndLexerTest, CppKeywordEscaping) {
    EXPECT_TRUE(is_cpp_keyword("class"));
    EXPECT_TRUE(is_cpp_keyword("default"));
    EXPECT_TRUE(is_cpp_keyword("switch"));
    EXPECT_TRUE(is_cpp_keyword("volatile"));
    EXPECT_TRUE(is_cpp_keyword("template"));
    EXPECT_FALSE(is_cpp_keyword("MotorState"));
    EXPECT_FALSE(is_cpp_keyword("StartCmd"));

    EXPECT_EQ(escape_cpp_keyword("class"), "class_");
    EXPECT_EQ(escape_cpp_keyword("default"), "default_");
    EXPECT_EQ(escape_cpp_keyword("Running"), "Running");
}

}  // namespace

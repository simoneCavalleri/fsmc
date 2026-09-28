/**
 * @file test_parser_classification.cpp
 * @brief Unit tests for front-end parser taxonomy and format classification (Formal vs Diagram).
 */

#include <gtest/gtest.h>

#include "fsm/frontend/common/json_parser.hpp"
#include "fsm/frontend/common/parser_factory.hpp"
#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/frontend/diagram/dot_parser.hpp"
#include "fsm/frontend/diagram/mermaid_parser.hpp"
#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/frontend/formal/cameo_xmi_parser.hpp"
#include "fsm/frontend/formal/scxml_parser.hpp"
#include "fsm/frontend/formal/smv_parser.hpp"
#include "fsm/frontend/formal/sysml2_parser.hpp"

using namespace fsm::frontend;
using namespace fsm::frontend::diagram;
using namespace fsm::frontend::formal;
using namespace fsm::ir;

/**
 * @brief Verify formal parser implementations report FrontendKind::Formal and canonical format tokens.
 * @scenario Instantiate SysML v2, SCXML, Cameo, SMV, and JSON parser instances.
 * @expected kind() returns FrontendKind::Formal and format_name() matches expected strings.
 */
TEST(FrontendClassification, FormalParsers_ReportsFormalFrontendKind) {
    Sysml2Parser sysml_parser;
    EXPECT_EQ(sysml_parser.kind(), FrontendKind::Formal);
    EXPECT_EQ(sysml_parser.format_name(), "sysml2");

    ScxmlParser scxml_parser;
    EXPECT_EQ(scxml_parser.kind(), FrontendKind::Formal);
    EXPECT_EQ(scxml_parser.format_name(), "scxml");

    CameoXmiParser cameo_parser;
    EXPECT_EQ(cameo_parser.kind(), FrontendKind::Formal);
    EXPECT_EQ(cameo_parser.format_name(), "cameo");

    SmvParser smv_parser;
    EXPECT_EQ(smv_parser.kind(), FrontendKind::Formal);
    EXPECT_EQ(smv_parser.format_name(), "smv");

    JsonParser json_parser;
    EXPECT_EQ(json_parser.kind(), FrontendKind::Formal);
    EXPECT_EQ(json_parser.format_name(), "json");
}

/**
 * @brief Verify diagram parser implementations report FrontendKind::Diagram and canonical format tokens.
 * @scenario Instantiate PlantUML, Mermaid, and DOT parser instances.
 * @expected kind() returns FrontendKind::Diagram and format_name() returns "plantuml", "mermaid", and "dot".
 */
TEST(FrontendClassification, DiagramParsers_ReportsDiagramFrontendKind) {
    PlantUmlParser puml_parser;
    EXPECT_EQ(puml_parser.kind(), FrontendKind::Diagram);
    EXPECT_EQ(puml_parser.format_name(), "plantuml");

    MermaidParser mmd_parser;
    EXPECT_EQ(mmd_parser.kind(), FrontendKind::Diagram);
    EXPECT_EQ(mmd_parser.format_name(), "mermaid");

    DotParser dot_parser;
    EXPECT_EQ(dot_parser.kind(), FrontendKind::Diagram);
    EXPECT_EQ(dot_parser.format_name(), "dot");
}

/**
 * @brief Verify ParserFactory format name to FrontendKind resolution mapping.
 * @scenario Query ParserFactory::get_kind_for_format with format name strings.
 * @expected Returned FrontendKind matches domain architecture classification.
 */
TEST(ParserFactory, FormatNameToKindLookup_ResolvesFormalAndDiagramKinds) {
    EXPECT_EQ(ParserFactory::get_kind_for_format("sysml2"), FrontendKind::Formal);
    EXPECT_EQ(ParserFactory::get_kind_for_format("scxml"), FrontendKind::Formal);
    EXPECT_EQ(ParserFactory::get_kind_for_format("cameo"), FrontendKind::Formal);
    EXPECT_EQ(ParserFactory::get_kind_for_format("smv"), FrontendKind::Formal);
    EXPECT_EQ(ParserFactory::get_kind_for_format("json"), FrontendKind::Formal);
    EXPECT_EQ(ParserFactory::get_kind_for_format("plantuml"), FrontendKind::Diagram);
    EXPECT_EQ(ParserFactory::get_kind_for_format("mermaid"), FrontendKind::Diagram);
    EXPECT_EQ(ParserFactory::get_kind_for_format("dot"), FrontendKind::Diagram);
}

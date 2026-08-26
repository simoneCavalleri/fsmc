#include <gtest/gtest.h>

#include "fsm/frontend/formal/cameo_xmi_parser.hpp"
#include "fsm/frontend/formal/scxml_parser.hpp"
#include "fsm/frontend/formal/smv_parser.hpp"
#include "fsm/frontend/formal/sysml2_parser.hpp"
#include "fsm/frontend/diagram/dot_parser.hpp"
#include "fsm/frontend/diagram/json_parser.hpp"
#include "fsm/frontend/diagram/mermaid_parser.hpp"
#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/frontend/parser_factory.hpp"
#include "fsm/frontend/parser_interface.hpp"

using namespace fsm::codegen;

TEST(FrontendClassificationTest, FormalParsersClassification) {
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
}

TEST(FrontendClassificationTest, DiagramParsersClassification) {
    PlantUmlParser puml_parser;
    EXPECT_EQ(puml_parser.kind(), FrontendKind::Diagram);
    EXPECT_EQ(puml_parser.format_name(), "plantuml");

    MermaidParser mmd_parser;
    EXPECT_EQ(mmd_parser.kind(), FrontendKind::Diagram);
    EXPECT_EQ(mmd_parser.format_name(), "mermaid");

    DotParser dot_parser;
    EXPECT_EQ(dot_parser.kind(), FrontendKind::Diagram);
    EXPECT_EQ(dot_parser.format_name(), "dot");

    JsonStateParser json_parser;
    EXPECT_EQ(json_parser.kind(), FrontendKind::Diagram);
    EXPECT_EQ(json_parser.format_name(), "json");
}

TEST(FrontendClassificationTest, ParserFactoryKindResolution) {
    EXPECT_EQ(ParserFactory::get_kind_for_format("sysml2"), FrontendKind::Formal);
    EXPECT_EQ(ParserFactory::get_kind_for_format("scxml"), FrontendKind::Formal);
    EXPECT_EQ(ParserFactory::get_kind_for_format("cameo"), FrontendKind::Formal);
    EXPECT_EQ(ParserFactory::get_kind_for_format("smv"), FrontendKind::Formal);
    EXPECT_EQ(ParserFactory::get_kind_for_format("plantuml"), FrontendKind::Diagram);
    EXPECT_EQ(ParserFactory::get_kind_for_format("mermaid"), FrontendKind::Diagram);
    EXPECT_EQ(ParserFactory::get_kind_for_format("dot"), FrontendKind::Diagram);
    EXPECT_EQ(ParserFactory::get_kind_for_format("json"), FrontendKind::Diagram);
}

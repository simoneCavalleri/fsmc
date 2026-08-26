#include <gtest/gtest.h>

#include "fsm/backend/emitter_factory.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace {

using namespace fsm::codegen;

TEST(EmitterFactoryTest, SupportedFormats) {
    auto formats = EmitterFactory::supported_formats();
    EXPECT_GE(formats.size(), 9u);
}

TEST(EmitterFactoryTest, EmitAllFormatsViaFactory) {
    FsmIr ir;
    ir.name = "MissionControlFSM";
    ir.initial_state = "Idle";
    ir.add_state("Idle");
    ir.add_state("Active");
    TransitionEdge edge("Idle", "Active", "StartCmd", "PowerOkGuard", "InitEnginesAction");
    ir.add_transition(std::move(edge));

    // 1. Canonical IR
    std::string ir_out = EmitterFactory::emit_diagram(ir, "ir");
    EXPECT_FALSE(ir_out.empty());
    EXPECT_NE(ir_out.find("\"name\": \"MissionControlFSM\""), std::string::npos);

    // 2. PlantUML
    std::string puml_out = EmitterFactory::emit_diagram(ir, "plantuml");
    EXPECT_FALSE(puml_out.empty());
    EXPECT_NE(puml_out.find("@startuml"), std::string::npos);
    EXPECT_NE(puml_out.find("@enduml"), std::string::npos);

    // 3. Mermaid
    std::string mmd_out = EmitterFactory::emit_diagram(ir, "mermaid");
    EXPECT_FALSE(mmd_out.empty());
    EXPECT_NE(mmd_out.find("stateDiagram-v2"), std::string::npos);

    // 4. SysML v2
    std::string sysml_out = EmitterFactory::emit_diagram(ir, "sysml2");
    EXPECT_FALSE(sysml_out.empty());
    EXPECT_NE(sysml_out.find("state def MissionControlFSM"), std::string::npos);

    // 5. Cameo XMI
    std::string cameo_out = EmitterFactory::emit_diagram(ir, "cameo");
    EXPECT_FALSE(cameo_out.empty());
    EXPECT_NE(cameo_out.find("<xmi:XMI"), std::string::npos);

    // 6. SCXML
    std::string scxml_out = EmitterFactory::emit_diagram(ir, "scxml");
    EXPECT_FALSE(scxml_out.empty());
    EXPECT_NE(scxml_out.find("<scxml"), std::string::npos);

    // 7. JSON
    std::string json_out = EmitterFactory::emit_diagram(ir, "json");
    EXPECT_FALSE(json_out.empty());
    EXPECT_NE(json_out.find("\"initial\": \"Idle\""), std::string::npos);

    // 8. DOT / Graphviz
    std::string dot_out = EmitterFactory::emit_diagram(ir, "dot");
    EXPECT_FALSE(dot_out.empty());
    EXPECT_NE(dot_out.find("digraph MissionControlFSM"), std::string::npos);

    // 9. nuXmv SMV
    std::string smv_out = EmitterFactory::emit_diagram(ir, "smv");
    EXPECT_FALSE(smv_out.empty());
    EXPECT_NE(smv_out.find("MODULE main"), std::string::npos);

    // 10. Unsupported format returns empty string
    std::string invalid_out = EmitterFactory::emit_diagram(ir, "unsupported_nonexistent_format");
    EXPECT_TRUE(invalid_out.empty());
}

}  // namespace

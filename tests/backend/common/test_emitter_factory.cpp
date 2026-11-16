/**
 * @file test_emitter_factory.cpp
 * @brief Unit test suite for the multi-format emitter factory.
 */

#include <gtest/gtest.h>

#include "fsm/backend/emitter_factory.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace {

using namespace fsm::backend;
using namespace fsm::ir;

/**
 * @brief Verify query of supported emitter target formats.
 * @scenario Query EmitterFactory for list of all registered emitter formats.
 * @expected At least 9 canonical formats are returned (IR, PlantUML, Mermaid, SysML v2, Cameo, SCXML, JSON, DOT, SMV).
 */
TEST(EmitterFactory, SupportedFormatsList_ContainsAllRegisteredBackends) {
    auto formats = EmitterFactory::supported_formats();
    EXPECT_GE(formats.size(), 9u);
}

/**
 * @brief Verify emission across all supported formats via factory dispatcher.
 * @scenario Construct canonical FsmIr and emit through factory to all 9 supported formats and invalid format.
 * @expected Valid emission strings returned containing format markers for all 9 formats, and empty string for invalid
 * format.
 */
TEST(EmitterFactory, CanonicalModel_EmittedAcrossAllRegisteredFormats) {
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

/**
 * @brief Verify EmitterFactory format discovery and C++ target code emission.
 * @scenario Query supported_formats(), is_supported_format(), and emit C++ headers via EmitterFactory::emit().
 * @expected Format queries report C++ and diagram formats accurately, and emit(model, "cpp") generates valid C++
 * header.
 */
TEST(EmitterFactory, FormatDiscoveryAndCppTargetEmission_SupportedAndSynthesized) {
    const auto formats = EmitterFactory::supported_formats();
    EXPECT_GE(formats.size(), 11u);

    EXPECT_TRUE(EmitterFactory::is_supported_format("cpp"));
    EXPECT_TRUE(EmitterFactory::is_supported_format("c++"));
    EXPECT_TRUE(EmitterFactory::is_supported_format("c++17"));
    EXPECT_TRUE(EmitterFactory::is_supported_format("c++20"));
    EXPECT_TRUE(EmitterFactory::is_supported_format("plantuml"));
    EXPECT_TRUE(EmitterFactory::is_supported_format("mermaid"));
    EXPECT_TRUE(EmitterFactory::is_supported_format("sysml2"));
    EXPECT_TRUE(EmitterFactory::is_supported_format("scxml"));
    EXPECT_FALSE(EmitterFactory::is_supported_format("nonexistent_target"));

    FsmIr model;
    model.name = "FormatTestFSM";
    model.initial_state = "Idle";
    model.add_state("Idle");
    model.add_state("Active");
    model.add_transition(TransitionEdge("Idle", "Active", "EvStart"));

    const std::string cpp_out = EmitterFactory::emit(model, "cpp");
    EXPECT_FALSE(cpp_out.empty());
    EXPECT_NE(cpp_out.find("FormatTestFSM"), std::string::npos);

    const std::string cpp17_out = EmitterFactory::emit(model, "c++17");
    EXPECT_FALSE(cpp17_out.empty());
    EXPECT_NE(cpp17_out.find("FormatTestFSM"), std::string::npos);
}

}  // namespace

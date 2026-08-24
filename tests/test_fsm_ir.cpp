#include <gtest/gtest.h>

#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"

using namespace fsm::codegen;

namespace {

TEST(FsmIrTest, DeterministicIdGeneration) {
    std::string id1 = compute_deterministic_id("Operating.Running.Manual");
    std::string id2 = compute_deterministic_id("Operating.Running.Manual");
    std::string id3 = compute_deterministic_id("Operating.Running.Auto");

    EXPECT_EQ(id1, id2);
    EXPECT_NE(id1, id3);
    EXPECT_EQ(id1.rfind("id_", 0), 0u);
}

TEST(FsmIrTest, StateHierarchyAndOrthogonalRegions) {
    FsmIr ir;
    ir.name = "IndustrialController";
    ir.ns = "industrial";
    ir.context_type = "MachineContext";
    ir.satisfies_reqs = {"REQ-SAFETY-01", "REQ-REALTIME-02"};

    // Add state hierarchy
    auto& operating = ir.add_or_get_state("Operating", "", StateKind::Composite);
    operating.do_activity = "async_sensor_poll";
    operating.traceability_reqs = {"REQ-SAFETY-01"};

    auto& running = ir.add_or_get_state("Running", "Operating", StateKind::Parallel);

    // Add orthogonal regions to Running
    OrthogonalRegion reg_motion;
    reg_motion.id = "reg_motion";
    reg_motion.name = "MotionRegion";
    reg_motion.state_ids = {"Manual", "Auto"};
    running.orthogonal_regions.push_back(reg_motion);

    OrthogonalRegion reg_diagnostics;
    reg_diagnostics.id = "reg_diag";
    reg_diagnostics.name = "DiagnosticsRegion";
    reg_diagnostics.state_ids = {"Normal", "Degraded"};
    running.orthogonal_regions.push_back(reg_diagnostics);

    // Add signals with typed payloads and validators
    SignalDefinition sig_packet("EvPacketRecv");
    sig_packet.attributes.emplace_back("len", "uint32_t");
    sig_packet.attributes.emplace_back("ptr", "const uint8_t*");
    sig_packet.validators.emplace_back("len > 0");
    sig_packet.validators.emplace_back("ptr != nullptr");
    ir.add_signal(sig_packet);

    // Add transitions with guard AST
    GuardAstNode guard_ast(GuardOp::And,
                           {GuardAstNode("SafetyOk"), GuardAstNode(GuardOp::Not, {GuardAstNode("EStop")})});
    ActionSignature action_sig("ctx.on_start(payload)", "ctx.on_start(payload)");
    ir.add_transition("Idle", "Operating", SignalTrigger{"StartCmd", "payload"}, guard_ast, action_sig);

    ir.canonicalize();

    // Verify properties
    EXPECT_EQ(ir.states.size(), 2u);
    EXPECT_EQ(ir.signals.size(), 2u);
    EXPECT_EQ(ir.transitions.size(), 1u);
    EXPECT_EQ(ir.signals[0].validators.size(), 2u);
    EXPECT_EQ(ir.transitions[0].guard_ast->to_string(), "SafetyOk && !EStop");

    // Test JSON serialization
    std::string json = FsmIrSerializer::serialize_json(ir);
    EXPECT_NE(json.find("\"name\": \"IndustrialController\""), std::string::npos);
    EXPECT_NE(json.find("\"REQ-SAFETY-01\""), std::string::npos);
    EXPECT_NE(json.find("\"do_activity\": \"async_sensor_poll\""), std::string::npos);
    EXPECT_NE(json.find("\"validators\": [\"len > 0\", \"ptr != nullptr\"]"), std::string::npos);
    EXPECT_NE(json.find("\"guard_ast\": \"SafetyOk && !EStop\""), std::string::npos);
}

}  // namespace

#include <gtest/gtest.h>

#include "fsm/backend/rtm_emitter.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::codegen;

namespace {

TEST(RtmEmitterTest, MarkdownAndJsonGeneration) {
    FsmIr model;
    model.name = "FlightControlFSM";
    model.initial_state = "Idle";

    StateNode s_idle;
    s_idle.name = "Idle";
    model.states.push_back(s_idle);

    StateNode s_safe;
    s_safe.name = "SafeMode";
    s_safe.traceability_reqs.push_back("REQ-SAFETY-01");
    model.states.push_back(s_safe);

    StateNode s_active;
    s_active.name = "Active";
    s_active.traceability_reqs.push_back("REQ-PERF-02");
    model.states.push_back(s_active);

    TransitionEdge t1("t1", "Idle", "SafeMode", SignalTrigger("Emergency"));
    t1.traceability_reqs.push_back("REQ-SAFETY-01");
    model.add_transition(t1);

    FormalProperty p1("Prop_SafeFailsafe", PropertyKind::Safety, "G (Emergency -> F SafeMode)", "Failsafe guarantee",
                      "REQ-SAFETY-01");
    model.properties.push_back(p1);

    FormalProperty p2("Prop_ActiveResponse", PropertyKind::Liveness, "G (Start -> F Active)", "Response guarantee",
                      "REQ-PERF-02");
    model.properties.push_back(p2);

    std::vector<ModelCheckResult> passed_results = {
        {true, "Prop_SafeFailsafe", "G (Emergency -> F SafeMode)", PropertyKind::Safety, "", {}},
        {true, "Prop_ActiveResponse", "G (Start -> F Active)", PropertyKind::Liveness, "", {}}};

    // 1. Markdown Export
    std::string md = RtmEmitter::emit(model, passed_results, RtmFormat::Markdown);
    EXPECT_NE(md.find("# Requirement Traceability Matrix (RTM): FlightControlFSM"), std::string::npos);
    EXPECT_NE(md.find("100.0% (2/2 Requirements Verified)"), std::string::npos);
    EXPECT_NE(md.find("`REQ-SAFETY-01`"), std::string::npos);
    EXPECT_NE(md.find("`SafeMode`"), std::string::npos);
    EXPECT_NE(md.find("`Idle -> SafeMode`"), std::string::npos);
    EXPECT_NE(md.find("**PASSED**"), std::string::npos);

    // 2. JSON Export
    std::string json = RtmEmitter::emit(model, passed_results, RtmFormat::Json);
    EXPECT_NE(json.find("\"fsm_name\": \"FlightControlFSM\""), std::string::npos);
    EXPECT_NE(json.find("\"total_requirements\": 2"), std::string::npos);
    EXPECT_NE(json.find("\"verified_requirements\": 2"), std::string::npos);
    EXPECT_NE(json.find("\"compliance_rate\": 100.0"), std::string::npos);
    EXPECT_NE(json.find("\"id\": \"REQ-SAFETY-01\""), std::string::npos);
    EXPECT_NE(json.find("\"status\": \"VERIFIED\""), std::string::npos);

    // 3. Violation Export (50% compliance)
    std::vector<ModelCheckResult> failed_results = {
        {false, "Prop_SafeFailsafe", "G (Emergency -> F SafeMode)", PropertyKind::Safety, "Safety breach", {}},
        {true, "Prop_ActiveResponse", "G (Start -> F Active)", PropertyKind::Liveness, "", {}}};

    std::string md_violated = RtmEmitter::emit(model, failed_results, RtmFormat::Markdown);
    EXPECT_NE(md_violated.find("50.0% (1/2 Requirements Verified)"), std::string::npos);
    EXPECT_NE(md_violated.find("**VIOLATED**"), std::string::npos);

    std::string json_violated = RtmEmitter::emit(model, failed_results, RtmFormat::Json);
    EXPECT_NE(json_violated.find("\"compliance_rate\": 50.0"), std::string::npos);
    EXPECT_NE(json_violated.find("\"status\": \"VIOLATED\""), std::string::npos);
}

}  // namespace

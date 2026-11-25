/**
 * @file test_ir_json_and_integrity.cpp
 * @brief Tests for strict canonical ir-json ingestion, schema version enforcement, and VerifyIrIntegrityPass.
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/frontend/common/parser_factory.hpp"
#include "fsm/frontend/ir_json/ir_json_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/fsm_ir_deserializer.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"
#include "fsm/middleend/pass_manager.hpp"
#include "fsm/middleend/passes/verify_ir_integrity_pass.hpp"

namespace {

using namespace fsm::ir;
using namespace fsm::frontend;
using namespace fsm::diagnostic;
using namespace fsm::middleend;
using namespace fsm::middleend::passes;

bool has_error_code(const DiagnosticEngine& diag, std::string_view code) {
    for (const auto& d : diag.get_diagnostics()) {
        if (d.code == code) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Verify lossless byte-identical round-trip via emit-ir -> IrJsonParser -> emit-ir and ParserFactory dispatch.
 * @scenario Serialize a canonical FsmIr with ports, variables, hierarchy, and guards, re-ingest via IrJsonParser, and
 *           re-serialize.
 * @expected Output JSON contains ir_schema_version "1.0" and re-serialization is byte-identical.
 */
TEST(IrJsonAndIntegrityTest, LosslessCanonicalRoundtripAndFactoryDispatch) {
    auto p_by_fmt = ParserFactory::create_by_format("ir-json");
    ASSERT_NE(p_by_fmt, nullptr);
    EXPECT_EQ(p_by_fmt->format_name(), "ir-json");
    EXPECT_EQ(p_by_fmt->kind(), FrontendKind::Formal);

    auto p_by_ext = ParserFactory::create_by_extension("subsystem/controller.ir.json");
    ASSERT_NE(p_by_ext, nullptr);
    EXPECT_EQ(p_by_ext->format_name(), "ir-json");

    FsmIr ir;
    ir.name = "TelemetryController";
    ir.package = "aero::telemetry";
    ir.initial_state = "Idle";
    ir.attributes["z_key"] = "last";
    ir.attributes["a_key"] = "first";
    ir.add_port(PortDefinition("temp_c", "float", PortDirection::In));
    ir.add_port(PortDefinition("heater_on", "bool", PortDirection::Out));
    ir.add_variable(VariableDefinition("retries", DataType::uint32(), "0"));
    ir.add_state("Idle");
    ir.add_state("Heating");
    ir.add_event("Tick");
    ir.add_guard("too_cold", "", "temp_c < 15.0", "temp_c < 15.0");
    ir.transitions.emplace_back("Idle", "Heating", "Tick", "too_cold");
    ir.transitions.emplace_back("Heating", "Idle", "Tick");
    ir.canonicalize();

    const std::string json1 = FsmIrSerializer::serialize_json(ir);
    EXPECT_NE(json1.find("\"ir_schema_version\": \"1.0\""), std::string::npos);

    FsmIr parsed;
    DiagnosticEngine diag;
    IrJsonParser parser;
    ASSERT_TRUE(parser.parse(json1, parsed, diag)) << diag.render_to_string();
    parsed.canonicalize();

    const std::string json2 = FsmIrSerializer::serialize_json(parsed);
    EXPECT_EQ(json1, json2);

    // Must pass VerifyIrIntegrityPass cleanly
    VerifyIrIntegrityPass integrity_pass;
    DiagnosticEngine int_diag;
    EXPECT_TRUE(integrity_pass.run(parsed, int_diag)) << int_diag.render_to_string();
    EXPECT_FALSE(int_diag.has_errors());
}

/**
 * @brief Verify strict ir-json ingestion rejects missing/wrong ir_schema_version and unknown/XState root fields.
 * @scenario Feed XState JSON, missing version, wrong version, and unknown root keys into IrJsonParser.
 * @expected IrJsonParser fails closed with E_IR_SCHEMA_VERSION or E_IR_UNKNOWN_FIELD.
 */
TEST(IrJsonAndIntegrityTest, StrictIrJson_RejectsInvalidSchemaAndUnknownFields) {
    IrJsonParser parser;

    // 1. Missing ir_schema_version (e.g. XState JSON)
    {
        FsmIr out;
        DiagnosticEngine diag;
        const std::string xstate = R"({"id": "M", "initial": "A", "context": {"x": 0}, "states": {"A": {}}})";
        EXPECT_FALSE(parser.parse(xstate, out, diag));
        EXPECT_TRUE(has_error_code(diag, "E_IR_SCHEMA_VERSION"));
    }

    // 2. Unsupported ir_schema_version
    {
        FsmIr out;
        DiagnosticEngine diag;
        const std::string bad_ver = R"({"ir_schema_version": "99.0", "name": "M", "states": {}})";
        EXPECT_FALSE(parser.parse(bad_ver, out, diag));
        EXPECT_TRUE(has_error_code(diag, "E_IR_SCHEMA_VERSION"));
    }

    // 3. Unknown root field (e.g. XState "context" or typo)
    {
        FsmIr out;
        DiagnosticEngine diag;
        const std::string unknown_key =
            R"({"ir_schema_version": "1.0", "name": "M", "bogus_field": 123, "states": {}})";
        EXPECT_FALSE(parser.parse(unknown_key, out, diag));
        EXPECT_TRUE(has_error_code(diag, "E_IR_UNKNOWN_FIELD"));
    }
}

/**
 * @brief Verify VerifyIrIntegrityPass detects all structural and datapath invariant violations.
 * @scenario Run VerifyIrIntegrityPass on IRs with empty name, duplicate IDs, cyclic hierarchy, dangling transitions,
 *           and illegal writes to read-only InPorts.
 * @expected Each violation emits the exact expected diagnostic code (E_IR_ROOT, E_IR_DUP_ID, E_IR_HIERARCHY,
 *           E_IR_DANGLING_REF, E_IR_TYPE_MISMATCH).
 */
TEST(IrJsonAndIntegrityTest, VerifyIrIntegrityPass_DetectsAllViolationClasses) {
    VerifyIrIntegrityPass pass;

    // 1. E_IR_ROOT: empty name & missing initial state
    {
        FsmIr ir;
        ir.name = "";
        ir.initial_state = "MissingStart";
        ir.add_state("Idle");
        DiagnosticEngine diag;
        EXPECT_FALSE(pass.run(ir, diag));
        EXPECT_TRUE(has_error_code(diag, "E_IR_ROOT"));
    }

    // 2. E_IR_DUP_ID: duplicate port & duplicate state FQN
    {
        FsmIr ir;
        ir.name = "DupTest";
        ir.initial_state = "S1";
        StateNode s1("S1");
        StateNode s2("S1");
        ir.states.push_back(s1);
        ir.states.push_back(s2);
        ir.ports.emplace_back("p_in", "int", PortDirection::In);
        ir.ports.emplace_back("p_in", "int", PortDirection::In);
        DiagnosticEngine diag;
        EXPECT_FALSE(pass.run(ir, diag));
        EXPECT_TRUE(has_error_code(diag, "E_IR_DUP_ID"));
    }

    // 3. E_IR_HIERARCHY: cyclic parent_state (A -> B -> A)
    {
        FsmIr ir;
        ir.name = "CycleTest";
        ir.initial_state = "A";
        StateNode a("A", "", "B");
        StateNode b("B", "", "A");
        ir.states.push_back(a);
        ir.states.push_back(b);
        DiagnosticEngine diag;
        EXPECT_FALSE(pass.run(ir, diag));
        EXPECT_TRUE(has_error_code(diag, "E_IR_HIERARCHY"));
    }

    // 4. E_IR_DANGLING_REF: transition targeting non-existent state
    {
        FsmIr ir;
        ir.name = "DanglingTest";
        ir.initial_state = "Idle";
        ir.add_state("Idle");
        ir.transitions.emplace_back("Idle", "GhostState", "EvGo");
        DiagnosticEngine diag;
        EXPECT_FALSE(pass.run(ir, diag));
        EXPECT_TRUE(has_error_code(diag, "E_IR_DANGLING_REF"));
    }

    // 5. E_IR_TYPE_MISMATCH: action writing to read-only InPort
    {
        FsmIr ir;
        ir.name = "WriteInPortTest";
        ir.initial_state = "Idle";
        ir.add_state("Idle");
        ir.add_state("Active");
        ir.add_port(PortDefinition("sensor_v", "float", PortDirection::In));
        TransitionEdge tr("Idle", "Active", "EvGo");
        ActionSignature act("bad_write");
        act.assignments.push_back(ActionAssignment{LValueTarget{"sensor_v", LValueScope::OutPort}, "5.0"});
        tr.transition_action = act;
        ir.transitions.push_back(tr);
        DiagnosticEngine diag;
        EXPECT_FALSE(pass.run(ir, diag));
        EXPECT_TRUE(has_error_code(diag, "E_IR_TYPE_MISMATCH"));
    }
}

/**
 * @brief Verify schemas/fsm_ir.schema.json exists and is registered in PassRegistry.
 */
TEST(IrJsonAndIntegrityTest, SchemaFileAndPassRegistryPresence) {
    auto pass = PassManager::create_pass_by_name("verify-ir-integrity");
    ASSERT_NE(pass, nullptr);
    EXPECT_EQ(pass->name(), "verify-ir-integrity");

    const std::filesystem::path schema_path =
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "schemas" / "fsm_ir.schema.json";
    ASSERT_TRUE(std::filesystem::exists(schema_path)) << "Missing schema at " << schema_path;
    std::ifstream ifs(schema_path);
    std::ostringstream ss;
    ss << ifs.rdbuf();
    EXPECT_NE(ss.str().find("\"ir_schema_version\""), std::string::npos);
}

}  // namespace

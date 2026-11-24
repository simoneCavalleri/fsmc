/**
 * @file test_manifest_and_diagnostics.cpp
 * @brief Tests for pure C++ SHA-256, *.manifest.json emission with cross-artifact IR hash consistency,
 * and structured JSON diagnostics (--diagnostics=json) with counterexamples and suggested fixes.
 */

#include <gtest/gtest.h>

#include <string>

#include "fsm/backend/cpp/c_abi_generator.hpp"
#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/manifest_emitter.hpp"
#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"
#include "fsm/ir/sha256.hpp"

using namespace fsm::ir;
using namespace fsm::backend;
using namespace fsm::backend::cpp;
using namespace fsm::diagnostic;

namespace {

FsmIr build_manifest_test_ir() {
    FsmIr ir;
    ir.name = "TelemetryRouter";
    ir.initial_state = "Idle";
    ir.package = "comms";
    ir.satisfies_reqs.push_back("REQ-SYS-001");

    ir.add_type(TypeDefinition::make_external("PacketFrame", "bus_types", "bus.PacketFrame", TypeKind::Struct,
                                              "Shared packet POD"));
    ir.add_service(ServiceMethodSignature("crc32_compute", {ServiceParameter("len", DataType("uint32_t"))},
                                          DataType("uint32_t"), /*pure=*/true, /*noexcept=*/true, "CRC32 helper"));

    StateNode idle("Idle");
    idle.traceability_reqs.push_back("REQ-FSM-010");
    ir.add_state(std::move(idle));
    ir.add_state("Active");

    PortDefinition p_in("rx_frame", "PacketFrame", PortDirection::In);
    ir.add_port(std::move(p_in));
    PortDefinition p_out("tx_rate_hz", "int32_t", PortDirection::Out);
    p_out.min_value = 1.0;
    p_out.max_value = 1000.0;
    p_out.physical_unit = "Hz";
    ir.add_port(std::move(p_out));

    SignalDefinition start_sig("StartStream");
    start_sig.attributes.emplace_back("channel", "uint8_t", "0");
    ir.add_signal(std::move(start_sig));
    ir.add_signal(SignalDefinition("StopStream"));

    TransitionEdge t1("Idle", "Active", "StartStream");
    t1.traceability_reqs.push_back("REQ-FSM-020");
    ir.add_transition(std::move(t1));
    ir.add_transition(TransitionEdge("Active", "Idle", "StopStream"));
    return ir;
}

}  // namespace

TEST(ManifestAndDiagnostics, Sha256_MatchesNistTestVectors) {
    EXPECT_EQ(sha256_hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256_hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(ManifestAndDiagnostics, IrSha256_IsConsistentAcrossManifestCppAndCAbi) {
    FsmIr ir = build_manifest_test_ir();
    const std::string expected_hash = FsmIrSerializer::compute_sha256(ir);
    ASSERT_EQ(expected_hash.size(), 64U);

    // Deterministic across repeated calls
    EXPECT_EQ(FsmIrSerializer::compute_sha256(ir), expected_hash);

    GeneratorOptions opts;
    opts.standalone = true;
    opts.thread_safe = false;

    const auto cpp_bundle = CppGenerator::generate_bundle(ir, opts, "telemetry_ports.hpp");
    const auto c_bundle = CAbiGenerator::generate(ir, opts, "telemetry_fsm.hpp", "telemetry_c.h");

    ManifestArtifacts artifacts;
    artifacts.ports_header = "telemetry_ports.hpp";
    artifacts.fsm_header = "telemetry_fsm.hpp";
    artifacts.c_header = "telemetry_c.h";
    artifacts.c_source = "telemetry_c.cpp";
    artifacts.standalone = false;

    ManifestVerificationSummary ver;
    ver.verified = true;
    ver.errors = 0;
    ver.warnings = 0;
    ver.properties_checked = 2;

    const std::string manifest = ManifestEmitter::emit_json(ir, artifacts, ver);

    // Exact same SHA-256 embedded in manifest, C++ FSM header (comment + constexpr), and C99 ABI header
    EXPECT_NE(manifest.find("\"ir_sha256\": \"" + expected_hash + "\""), std::string::npos);
    EXPECT_NE(cpp_bundle.fsm_header.find("IR-SHA256: " + expected_hash), std::string::npos);
    EXPECT_NE(cpp_bundle.fsm_header.find("TelemetryRouterIrSha256 = \"" + expected_hash + "\""), std::string::npos);
    EXPECT_NE(c_bundle.header.find("#define TELEMETRY_ROUTER_IR_SHA256 \"" + expected_hash + "\""), std::string::npos);

    // Verify all contract sections in manifest
    EXPECT_NE(manifest.find("\"manifest_schema_version\": \"1.0\""), std::string::npos);
    EXPECT_NE(manifest.find("\"compiler_version\": \"0.13.0\""), std::string::npos);
    EXPECT_NE(manifest.find("\"registers\": ["), std::string::npos);
    EXPECT_NE(manifest.find("\"fsm_name\": \"TelemetryRouter\""), std::string::npos);
    EXPECT_NE(manifest.find("\"ports_header\": \"telemetry_ports.hpp\""), std::string::npos);
    EXPECT_NE(manifest.find("\"c_header\": \"telemetry_c.h\""), std::string::npos);
    EXPECT_NE(manifest.find("\"name\": \"crc32_compute\""), std::string::npos);
    EXPECT_NE(manifest.find("\"module\": \"bus_types\", \"symbol\": \"bus.PacketFrame\""), std::string::npos);
    EXPECT_NE(manifest.find("\"REQ-SYS-001\""), std::string::npos);
    EXPECT_NE(manifest.find("\"REQ-FSM-010\""), std::string::npos);
    EXPECT_NE(manifest.find("\"REQ-FSM-020\""), std::string::npos);
    EXPECT_NE(manifest.find("\"verified\": true"), std::string::npos);
}

TEST(ManifestAndDiagnostics, DiagnosticEngineJson_IncludesCounterexampleAndSuggestedFixes) {
    DiagnosticEngine engine;
    Diagnostic d = Diagnostic::error("E_DEADLOCK", "Deadlock state 'Fault' has no outgoing transitions",
                                     SourceSpan{"controller.sysml", 42, 5, 5});
    d.help_suggestion = "Add a Reset transition from Fault to Idle";
    d.counterexample.push_back(DiagnosticCounterexampleStep{0, "Idle", "Start", "Active"});
    d.counterexample.push_back(DiagnosticCounterexampleStep{1, "Active", "Trip", "Fault"});
    d.suggested_fixes.push_back(SuggestedFix{"Add recovery transition", SourceSpan{"controller.sysml", 43, 1, 0},
                                             "transition from Fault accept Reset then Idle;"});
    engine.report(std::move(d));

    const std::string json = engine.render_to_format(DiagnosticFormat::Json);
    EXPECT_NE(json.find("\"code\": \"E_DEADLOCK\""), std::string::npos);
    EXPECT_NE(json.find("\"counterexample\": ["), std::string::npos);
    EXPECT_NE(json.find("\"state\": \"Active\", \"event\": \"Trip\", \"next_state\": \"Fault\""), std::string::npos);
    EXPECT_NE(json.find("\"suggested_fixes\": ["), std::string::npos);
    EXPECT_NE(json.find("\"replacement\": \"transition from Fault accept Reset then Idle;\""), std::string::npos);
}

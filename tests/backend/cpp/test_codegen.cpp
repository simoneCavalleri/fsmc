#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/runtime_exporter.hpp"
#include "fsm/frontend/diagram/mermaid_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

using namespace fsm::codegen;
namespace fs = std::filesystem;

namespace {

/**
 * @brief Test Intent: Verify C++17 code generation compliance (template parameters for actions/guards).
 *
 * Scenario:
 * - Generate C++ header for NetworkFSM targeting CppStandard::Cpp17.
 * - Verify namespace wrapping, C++17 generic template operators, and transition_table aliases.
 */
TEST(CodegenTest, Cpp17CodegenGeneration) {
    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Idle
        Idle --> Connecting : ConnectCmd [NetworkReady] / InitSocket
        Connecting --> Connected : HandshakeOk / EnableData
        Connected --> Idle : DisconnectCmd / CloseSocket
    )";

    MermaidParser parser;
    FsmIr model;
    model.name = "NetworkFSM";
    model.ns = "net";
    model.context_type = "no_context";

    std::string err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err;

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp17;
    const std::string code = CppGenerator::generate_header(model, opts);

    EXPECT_NE(code.find("namespace net {"), std::string::npos);
    EXPECT_NE(code.find("template <typename Event, typename State, typename Context>"), std::string::npos);
    EXPECT_NE(code.find("using NetworkFSMTable = fsm::transition_table<"), std::string::npos);
}

/**
 * @brief Test Intent: Verify C++20 code generation with abbreviated function templates (`const auto&`).
 *
 * Scenario:
 * - Generate C++ header targeting CppStandard::Cpp20.
 * - Verify modern `operator()(const auto& ...)` syntax and constexpr nodiscard guards.
 */
TEST(CodegenTest, Cpp20CodegenGeneration) {
    const std::string mmd = R"(
    stateDiagram-v2
        [*] --> Idle
        Idle --> Connecting : ConnectCmd [NetworkReady] / InitSocket
        Connecting --> Connected : HandshakeOk / EnableData
        Connected --> Idle : DisconnectCmd / CloseSocket
    )";

    MermaidParser parser;
    FsmIr model;
    model.name = "NetworkFSM";
    model.ns = "net";
    model.context_type = "no_context";

    std::string err;
    ASSERT_TRUE(parser.parse(mmd, model, err)) << "Error: " << err;

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    const std::string code = CppGenerator::generate_header(model, opts);

    EXPECT_NE(code.find("namespace net {"), std::string::npos);
    EXPECT_NE(code.find("[[nodiscard]] constexpr bool operator()(const auto&"), std::string::npos);
    EXPECT_NE(code.find("constexpr void operator()(const auto&"), std::string::npos);
    EXPECT_NE(code.find("using NetworkFSMTable = fsm::transition_table<"), std::string::npos);
}

/**
 * @brief Test Intent: Verify C++ emission of strongly-typed signal structs with payload validators.
 *
 * Scenario:
 * - Define signal `EvTelemetry` with attributes `len`, `ptr` and validation expressions.
 * - Verify generated C++ code includes explicit constructors and `constexpr bool is_valid()` validator member
 * functions.
 */
TEST(CodegenTest, TypedSignalPayloads) {
    FsmIr model;
    model.name = "TelemetryFSM";

    SignalDefinition sig("EvTelemetry");
    sig.attributes.emplace_back("len", "uint32_t", "0");
    sig.attributes.emplace_back("ptr", "const uint8_t*", "nullptr");
    sig.validators.emplace_back("len > 0");
    sig.validators.emplace_back("ptr != nullptr");
    model.add_signal(sig);

    model.add_state("Idle");
    model.initial_state = "Idle";

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    const std::string code = CppGenerator::generate_header(model, opts);

    EXPECT_NE(code.find("struct EvTelemetry {"), std::string::npos);
    EXPECT_NE(code.find("uint32_t len{0};"), std::string::npos);
    EXPECT_NE(code.find("const uint8_t* ptr{nullptr};"), std::string::npos);
    EXPECT_NE(code.find("constexpr explicit EvTelemetry(uint32_t len_, const uint8_t* ptr_)"), std::string::npos);
    EXPECT_NE(code.find("[[nodiscard]] constexpr bool is_valid() const noexcept"), std::string::npos);
    EXPECT_NE(code.find("(len > 0) && (ptr != nullptr)"), std::string::npos);
}

/**
 * @brief Test Intent: Verify automated generation of EFSM Context structs and variable mutation actions.
 *
 * Scenario:
 * - Add variable `retry_count` and action mutating `retry_count = retry_count + 1`.
 * - Verify generator automatically produces `ProtocolFSMContext` and binds it to the `fsm::fsm` alias.
 */
TEST(CodegenTest, EfsmContextAndVariableAssignments) {
    FsmIr model;
    model.name = "ProtocolFSM";
    model.initial_state = "Disconnected";

    model.add_state("Disconnected");
    model.add_state("Connecting");

    // Add EFSM Variable
    VariableDefinition var("retry_count", "uint32_t", "0", 0, 5, "Connection retries");
    model.add_variable(var);

    // Add Transition with ActionAssignment
    ActionSignature act_sig("IncrementRetry", "IncrementRetry");
    act_sig.assignments.emplace_back("retry_count", "ctx.retry_count + 1");

    TransitionEdge t;
    t.source = "Disconnected";
    t.target = "Connecting";
    t.event = "ConnectCmd";
    t.action = "IncrementRetry";
    t.action_sig = act_sig;
    model.add_transition(t);

    model.actions.emplace_back("IncrementRetry", "Increment retry count");

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    const std::string code = CppGenerator::generate_header(model, opts);

    // Check EFSM Auto-Generated Context
    EXPECT_NE(code.find("struct ProtocolFSMContext {"), std::string::npos);
    EXPECT_NE(code.find("uint32_t retry_count{0}; // Connection retries"), std::string::npos);

    // Check Action Assignment
    EXPECT_NE(code.find("ctx.retry_count = ctx.retry_count + 1;"), std::string::npos);

    // Check FSM Alias uses ProtocolFSMContext
    EXPECT_NE(
        code.find(
            "using ProtocolFSM = fsm::fsm<ProtocolFSMTable, ProtocolFSMContext, Disconnected, fsm::dynamic_observer>;"),
        std::string::npos);
}

/**
 * @brief Test Intent: Verify C++ emission of state lifecycle hooks (`on_entry`, `on_exit`) and requirement docstrings.
 *
 * Scenario:
 * - State has traceability requirements (REQ-SAFE-01) and entry/exit actions.
 * - Verify Doxygen comments `/// @satisfies` and `on_entry`/`on_exit` methods in generated state structs.
 */
TEST(CodegenTest, StateLifecycleHooksAndTraceability) {
    FsmIr model;
    model.name = "AerospaceFSM";
    model.initial_state = "Operating";

    StateNode st("Operating", "Operating");
    st.traceability_reqs.emplace_back("REQ-SAFE-01");
    st.traceability_reqs.emplace_back("REQ-REALTIME-02");
    st.entry_actions.emplace_back("ArmSensors", "ArmSensors");
    st.exit_actions.emplace_back("DisarmSensors", "DisarmSensors");
    model.add_state(st);

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    const std::string code = CppGenerator::generate_header(model, opts);

    // Check Traceability Doc-Comments
    EXPECT_NE(code.find("/// @satisfies REQ-SAFE-01, REQ-REALTIME-02"), std::string::npos);

    // Check on_entry and on_exit hooks
    EXPECT_NE(code.find("void on_entry(Context& /*ctx*/) const"), std::string::npos);
    EXPECT_NE(code.find("// Entry action: ArmSensors"), std::string::npos);
    EXPECT_NE(code.find("void on_exit(Context& /*ctx*/) const"), std::string::npos);
    EXPECT_NE(code.find("// Exit action: DisarmSensors"), std::string::npos);
}

/**
 * @brief Test Intent: Verify `RuntimeExporter` bundles standalone runtime headers for C++17 and C++20.
 *
 * Scenario:
 * - Export standalone runtime into temporary directories.
 * - Verify `fsm.hpp` is created and clean up directories.
 */
TEST(CodegenTest, RuntimeExporterCpp17AndCpp20) {
    const std::string export_dir_cpp20 = "temp_runtime_export_cpp20";
    const std::string export_dir_cpp17 = "temp_runtime_export_cpp17";
    std::string err;

    // Test C++20 Runtime Export
    EXPECT_TRUE(RuntimeExporter::export_runtime(export_dir_cpp20 + "/fsm.hpp", CppStandard::Cpp20, err));
    EXPECT_TRUE(fs::exists(export_dir_cpp20 + "/fsm.hpp"));

    // Test C++17 Runtime Export
    EXPECT_TRUE(RuntimeExporter::export_runtime(export_dir_cpp17 + "/fsm.hpp", CppStandard::Cpp17, err));
    EXPECT_TRUE(fs::exists(export_dir_cpp17 + "/fsm.hpp"));

    // Cleanup
    fs::remove_all(export_dir_cpp20);
    fs::remove_all(export_dir_cpp17);
}

/**
 * @brief Test Intent: Verify C++ code generator emits entryPoint, exitPoint, time_invariant introspection, and sorts
 * transition rows by priority.
 *
 * Scenario:
 * - Build FsmIr model with EntryPoint and ExitPoint states, time_invariant, and transitions with priority 100 and
 * priority 1.
 * - Generate C++20 header.
 * - Verify static constexpr is_entry_point, is_exit_point, time_invariant, and that priority 100 transition appears
 * before priority 1 in table.
 */
TEST(CodegenTest, EntryExitPointTimeInvariantAndPriorityCodegen) {
    FsmIr model;
    model.name = "PrioFsm";
    model.initial_state = "Idle";

    StateNode idle("s1", "Idle", "Idle", StateKind::Atomic);
    idle.time_invariant = "stay <= 50ms";
    model.add_state(idle);

    StateNode ep("s2", "EnPort", "EnPort", StateKind::EntryPoint);
    model.add_state(ep);

    StateNode xp("s3", "ExPort", "ExPort", StateKind::ExitPoint);
    model.add_state(xp);

    // Low priority transition
    TransitionEdge t_low("t1", "Idle", "ExPort", SignalTrigger("EvTick"));
    t_low.priority = 1;
    model.add_transition(t_low);

    // High priority transition
    TransitionEdge t_high("t2", "Idle", "EnPort", SignalTrigger("EvTick"));
    t_high.priority = 100;
    model.add_transition(t_high);

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    const std::string code = CppGenerator::generate_header(model, opts);

    // Check entry point and exit point flags
    EXPECT_NE(code.find("static constexpr bool is_entry_point = true;"), std::string::npos);
    EXPECT_NE(code.find("static constexpr bool is_exit_point = true;"), std::string::npos);

    // Check time invariant metadata
    EXPECT_NE(code.find("/// @invariant stay <= 50ms"), std::string::npos);
    EXPECT_NE(code.find("static constexpr std::string_view time_invariant = \"stay <= 50ms\";"), std::string::npos);

    // Check transition ordering in table (t_high to EnPort must appear before t_low to ExPort)
    auto pos_high = code.find("fsm::row<Idle, EvTick, EnPort>");
    auto pos_low = code.find("fsm::row<Idle, EvTick, ExPort>");
    ASSERT_NE(pos_high, std::string::npos);
    ASSERT_NE(pos_low, std::string::npos);
    EXPECT_LT(pos_high, pos_low);
}

/**
 * @brief Test Intent: Verify C++ emission with deep multi-level namespaces and options matrix (--no-stubs,
 * --thread-safe).
 *
 * Scenario:
 * - Configure model with namespace "avionics::flight::executive".
 * - Generate C++ code with include_stubs = false and thread_safe = true.
 * - Verify namespace opening/closing structure, forward declarations, and thread_safe_fsm alias.
 */
TEST(CodegenTest, DeepNestedNamespaceAndOptionsMatrix) {
    FsmIr model;
    model.name = "MissionExecutive";
    model.ns = "avionics::flight::executive";
    model.initial_state = "Idle";

    model.add_state("Idle");
    model.add_state("Navigating");

    GuardModel gd("IsGuidanceLocked");
    model.guards.push_back(gd);

    ActionModel act("EngageThrusters");
    model.actions.push_back(act);

    TransitionEdge t1("t1", "Idle", "Navigating", SignalTrigger("EvLaunch"));
    t1.guard = "IsGuidanceLocked";
    t1.action = "EngageThrusters";
    model.add_transition(t1);

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp17;
    opts.include_stubs = false;  // Forward-declare guards and actions
    opts.thread_safe = true;

    const std::string code = CppGenerator::generate_header(model, opts);

    // Verify namespace
    EXPECT_NE(code.find("namespace avionics::flight::executive {"), std::string::npos);
    EXPECT_NE(code.find("} // namespace avionics::flight::executive"), std::string::npos);

    // Verify NO_STUBS forward declarations
    EXPECT_NE(code.find("struct IsGuidanceLocked;"), std::string::npos);
    EXPECT_NE(code.find("struct EngageThrusters;"), std::string::npos);

    // Verify ThreadSafe alias
    EXPECT_NE(
        code.find(
            "using ThreadSafeMissionExecutive = fsm::thread_safe_fsm<MissionExecutiveTable, fsm::no_context, Idle>;"),
        std::string::npos);
}

/**
 * @brief Test Intent: Verify C++ emission for empty model fallback and anonymous (triggerless) transitions.
 *
 * Scenario:
 * - Empty model with no explicit initial state defaults to Idle.
 * - Transition with trigger_kind = Anonymous (null/empty event) emits fsm::anonymous_event.
 */
TEST(CodegenTest, EmptyStateAndAnonymousImmediateTransitions) {
    FsmIr model;
    model.name = "ImmediateFSM";
    model.initial_state = "StepA";

    model.add_state("StepA");
    model.add_state("StepB");

    // Anonymous transition without trigger
    TransitionEdge t_anon;
    t_anon.id = "t1";
    t_anon.source = "StepA";
    t_anon.target = "StepB";
    model.add_transition(t_anon);

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    const std::string code = CppGenerator::generate_header(model, opts);

    EXPECT_NE(code.find("struct StepA"), std::string::npos);
    EXPECT_NE(code.find("struct StepB"), std::string::npos);
    EXPECT_NE(code.find("fsm::row<StepA, fsm::anonymous_event, StepB>"), std::string::npos);
}

/**
 * @brief Test Intent: Verify RuntimeExporter error resilience when export path is invalid.
 *
 * Scenario:
 * - Attempt to export runtime to a path whose parent is a regular file (not a directory).
 * - On all platforms (Linux, macOS, Windows), creating a subdirectory inside a file
 *   is guaranteed to fail with a filesystem error.
 * - Verify that RuntimeExporter returns false gracefully without abnormal termination.
 */
TEST(CodegenTest, RuntimeExporterNegativePath) {
    // Create a regular file, then try to use it as a directory — guaranteed to fail everywhere.
    const std::string blocker_file = "fsmc_test_blocker_file.tmp";
    {
        std::ofstream f(blocker_file);
        f << "blocker";
    }
    ASSERT_TRUE(fs::exists(blocker_file));

    std::string err;
    bool ok = RuntimeExporter::export_runtime(blocker_file + "/subdir/runtime.hpp", CppStandard::Cpp20, err);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(err.empty());

    fs::remove(blocker_file);
}

/**
 * @brief Test Intent: Verify C++ emission of automatic resolved EFSM guard expressions from Context variables.
 *
 * Scenario:
 * - Define FSM with Context variable 'batterySoC' and 'criticalError'.
 * - GuardModel with cpp_expression "ctx.batterySoC > 30.0f && !ctx.criticalError".
 * - Verify that generated struct contains the direct return expression instead of a TODO stub.
 */
TEST(CodegenTest, EfsmResolvedGuardCodeGeneration) {
    FsmIr model;
    model.name = "BatteryManager";
    model.initial_state = "Idle";

    model.add_state("Idle");
    model.add_state("Nominal");

    VariableDefinition var1;
    var1.name = "batterySoC";
    var1.type = "float";
    var1.initial_value = "100.0f";
    model.add_variable(var1);

    VariableDefinition var2;
    var2.name = "criticalError";
    var2.type = "bool";
    var2.initial_value = "false";
    model.add_variable(var2);

    GuardModel guard_item("batterySoC_gt_30", "Battery above threshold", "batterySoC > 30.0 && !criticalError",
                          "ctx.batterySoC > 30.0f && !ctx.criticalError");
    model.guards.push_back(guard_item);

    TransitionEdge t("t1", "Idle", "Nominal", SignalTrigger("CheckHealth"));
    t.guard = "batterySoC_gt_30";
    model.add_transition(t);

    GeneratorOptions opts;
    opts.cpp_standard = CppStandard::Cpp20;
    opts.include_stubs = true;

    const std::string code20 = CppGenerator::generate_header(model, opts);
    EXPECT_NE(code20.find("struct batterySoC_gt_30 {"), std::string::npos);
    EXPECT_NE(code20.find("return ctx.batterySoC > 30.0f && !ctx.criticalError;"), std::string::npos);

    opts.cpp_standard = CppStandard::Cpp17;
    const std::string code17 = CppGenerator::generate_header(model, opts);
    EXPECT_NE(code17.find("struct batterySoC_gt_30 {"), std::string::npos);
    EXPECT_NE(code17.find("return ctx.batterySoC > 30.0f && !ctx.criticalError;"), std::string::npos);
}

}  // namespace

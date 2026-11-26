/**
 * @file test_differential_semantics.cpp
 * @brief Differential verification suite comparing IrSimulator (reference oracle) against generated C++17 and C++20
 * FSMs.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "fsm/backend/c99/c99_generator.hpp"
#include "fsm/backend/cpp/c_abi_generator.hpp"
#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/manifest_emitter.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/ir_simulator.hpp"

namespace fs = std::filesystem;

namespace {

/**
 * @brief Differential check: EFSM guards, register mutations, and state transitions match across IrSimulator, C++17,
 *        and C++20 generated code.
 * @scenario Run a 6-event trace through IrSimulator and identical compiled C++17 and C++20 standalone harnesses.
 * @expected IrSimulator, C++17, and C++20 produce the exact same final state and register values.
 */
TEST(DifferentialSemantics, EfsmGuardsActionsAndRegisters_MatchCpp17AndCpp20) {
    using namespace fsm::ir;
    using namespace fsm::backend::cpp;

    using namespace fsm::ir;
    using namespace fsm::backend::cpp;
    FsmIr model;
    model.name = "DiffPumpFsm";
    model.initial_state = "Idle";
    model.add_state("Idle");
    model.add_state("Running");
    model.add_state("Maintenance");

    model.add_event("Start");
    model.add_event("Tick");
    model.add_event("Stop");

    model.add_variable(VariableDefinition("ticks", DataType::int32(), "0"));
    model.add_variable(VariableDefinition("cycles", DataType::int32(), "0"));

    model.add_guard("below_limit", "", "ticks < 2", "ticks < 2");
    model.add_guard("at_limit", "", "ticks >= 2", "ticks >= 2");

    // Idle -> Running on Start (cycles += 1)
    {
        TransitionEdge t("Idle", "Running", "Start");
        ActionSignature act("inc_cycles");
        act.assignments.push_back(
            ActionAssignment{LValueTarget{"cycles", LValueScope::Register}, "1", AssignmentOp::AddAssign});
        t.transition_action = act;
        model.transitions.push_back(t);
    }
    // Running -> Running on Tick [ticks < 2] (ticks += 1)
    {
        TransitionEdge t("Running", "Running", "Tick", "below_limit");
        t.kind = TransitionEdgeKind::Internal;
        ActionSignature act("inc_ticks");
        act.assignments.push_back(
            ActionAssignment{LValueTarget{"ticks", LValueScope::Register}, "1", AssignmentOp::AddAssign});
        t.transition_action = act;
        model.transitions.push_back(t);
    }
    // Running -> Maintenance on Tick [ticks >= 2]
    model.transitions.emplace_back("Running", "Maintenance", "Tick", "at_limit");
    // Maintenance -> Idle on Stop
    model.transitions.emplace_back("Maintenance", "Idle", "Stop");

    const std::string trace = "Start\nTick\nTick\nTick\n";

    std::ostringstream sim_log;
    auto report = fsm::tools::IrSimulator::simulate_detailed(model, trace, sim_log);
    ASSERT_EQ(report.final_state, "Maintenance");
    ASSERT_EQ(static_cast<int>(report.final_registers.at("ticks")), 2);
    ASSERT_EQ(static_cast<int>(report.final_registers.at("cycles")), 1);

    // JSON export sanity check
    const std::string json_report = report.to_json();
    EXPECT_NE(json_report.find("\"final_state\": \"Maintenance\""), std::string::npos);

    // Compile and run equivalent C++17 and C++20 standalone generated FSMs
    fs::path temp_dir = fs::temp_directory_path() / "fsmc_diff_semantics_test";
    fs::create_directories(temp_dir);

    auto verify_cpp_std = [&](const std::string& std_flag, const std::string& suffix) {
        GeneratorOptions opts;
        opts.standalone = true;
        opts.freestanding = true;
        opts.target_namespace = "diff_ns";
        const std::string hdr_code = CppGenerator::generate_header(model, opts);

        fs::path hdr = temp_dir / ("diff_" + suffix + ".hpp");
        fs::path drv = temp_dir / ("driver_" + suffix + ".cpp");
        fs::path out_file = temp_dir / ("result_" + suffix + ".txt");
        {
            std::ofstream os(hdr);
            os << hdr_code;
        }
        {
            std::ofstream os(drv);
            os << "#include <cstdio>\n"
               << "#include \"" << hdr.filename().string() << "\"\n"
               << "int main() {\n"
               << "    using namespace diff_ns;\n"
               << "    DiffPumpFsm sm;\n"
               << "    sm.dispatch(Start{});\n"
               << "    sm.dispatch(Tick{});\n"
               << "    sm.dispatch(Tick{});\n"
               << "    sm.dispatch(Tick{});\n"
               << "    const char* st = sm.is_in<Maintenance>() ? \"Maintenance\" : (sm.is_in<Running>() ? \"Running\" "
                  ": \"Idle\");\n"
               << "    std::FILE* f = std::fopen(\"" << out_file.generic_string() << "\", \"w\");\n"
               << "    if (!f) return 1;\n"
               << "    std::fprintf(f, \"%s %d %d\\n\", st, sm.registers().ticks, sm.registers().cycles);\n"
               << "    std::fclose(f);\n"
               << "    return 0;\n"
               << "}\n";
        }
        std::string exe = (temp_dir / ("runner_" + suffix)).string();
        std::string cmd =
            "g++ " + std_flag + " -Wall -Wextra -Werror -I" + temp_dir.string() + " " + drv.string() + " -o " + exe;
        ASSERT_EQ(std::system(cmd.c_str()), 0) << "[" << suffix << "] Differential compile failed";
        ASSERT_EQ(std::system(exe.c_str()), 0) << "[" << suffix << "] Differential execution failed";

        std::ifstream res_in(out_file);
        std::string cpp_state;
        int cpp_ticks = -1;
        int cpp_cycles = -1;
        res_in >> cpp_state >> cpp_ticks >> cpp_cycles;

        EXPECT_EQ(cpp_state, report.final_state) << "State mismatch for " << suffix;
        EXPECT_EQ(cpp_ticks, static_cast<int>(report.final_registers.at("ticks"))) << "ticks mismatch for " << suffix;
        EXPECT_EQ(cpp_cycles, static_cast<int>(report.final_registers.at("cycles")))
            << "cycles mismatch for " << suffix;
    };

    verify_cpp_std("-std=c++17", "cpp17");
    verify_cpp_std("-std=c++20", "cpp20");

    fs::remove_all(temp_dir);
}

/**
 * @brief Verify IrSimulator HFSM LCA entry/exit ordering and shallow history restoration.
 * @scenario Transition within composite state Active (SubA -> SubB), exit to Standby, then re-enter Active with [H].
 * @expected SubA -> SubB does not exit Active; Active -> Standby exits SubB then Active; re-entry restores SubB.
 */
TEST(DifferentialSemantics, HfsmLcaEntryExitAndHistory_IrSimulator) {
    using namespace fsm::ir;

    using namespace fsm::ir;
    using namespace fsm::backend::cpp;
    FsmIr model;
    model.name = "HfsmHistoryOracle";
    model.initial_state = "Active";
    model.add_variable(VariableDefinition("active_entries", DataType::int32(), "0"));
    model.add_variable(VariableDefinition("active_exits", DataType::int32(), "0"));

    StateNode active("Active");
    active.kind = StateKind::Composite;
    active.initial_sub_state = "SubA";
    active.has_history = true;
    {
        ActionSignature on_e("enter_active");
        on_e.assignments.push_back(
            ActionAssignment{LValueTarget{"active_entries", LValueScope::Register}, "1", AssignmentOp::AddAssign});
        active.entry_actions.push_back(on_e);

        ActionSignature on_x("exit_active");
        on_x.assignments.push_back(
            ActionAssignment{LValueTarget{"active_exits", LValueScope::Register}, "1", AssignmentOp::AddAssign});
        active.exit_actions.push_back(on_x);
    }
    StateNode sub_a("SubA", "", "Active");
    StateNode sub_b("SubB", "", "Active");
    StateNode standby("Standby");

    model.add_state(active);
    model.add_state(sub_a);
    model.add_state(sub_b);
    model.add_state(standby);

    model.transitions.emplace_back("SubA", "SubB", "Next");
    model.transitions.emplace_back("Active", "Standby", "Pause");
    model.transitions.emplace_back("Standby", "Active", "Resume");

    std::ostringstream log;
    auto report = fsm::tools::IrSimulator::simulate_detailed(model, "Next\nPause\nResume\n", log);

    EXPECT_EQ(report.initial_state, "SubA");
    EXPECT_EQ(report.final_state, "SubB");  // Restored via shallow history!
    ASSERT_EQ(report.steps.size(), 3u);
    // Step 0: SubA -> SubB (LCA is Active, so Active must NOT be exited)
    EXPECT_EQ(report.steps[0].exited_states, (std::vector<std::string>{"SubA"}));
    EXPECT_EQ(report.steps[0].entered_states, (std::vector<std::string>{"SubB"}));
    // Step 1: SubB -> Standby (bottom-up exit: SubB, then Active)
    EXPECT_EQ(report.steps[1].exited_states, (std::vector<std::string>{"SubB", "Active"}));
    EXPECT_EQ(report.steps[1].entered_states, (std::vector<std::string>{"Standby"}));
    // Step 2: Standby -> Active [H] -> SubB
    EXPECT_EQ(report.steps[2].entered_states, (std::vector<std::string>{"Active", "SubB"}));
    EXPECT_EQ(static_cast<int>(report.final_registers.at("active_entries")), 2);
    EXPECT_EQ(static_cast<int>(report.final_registers.at("active_exits")), 1);
}

/**
 * @brief Verify IrSimulator deferred event queuing/recall and strict fail-closed guards.
 * @scenario Dispatch deferred event EvConfig while in Busy, then EvDone to enter Ready; also test strict_guards=true.
 * @expected EvConfig is deferred in Busy and automatically recalled upon entering Ready; unresolved guard fails closed.
 */
TEST(DifferentialSemantics, DeferredEventsRecallAndStrictFailClosedGuards) {
    using namespace fsm::ir;

    using namespace fsm::ir;
    using namespace fsm::backend::cpp;
    FsmIr model;
    model.name = "DeferredOracle";
    model.initial_state = "Busy";

    StateNode busy("Busy");
    busy.deferred_events.push_back("EvConfig");
    model.add_state(busy);
    model.add_state("Ready");
    model.add_state("Configured");

    model.transitions.emplace_back("Busy", "Ready", "EvDone");
    model.transitions.emplace_back("Ready", "Configured", "EvConfig");
    model.transitions.emplace_back("Configured", "Busy", "EvCheck", "unknown_symbolic_predicate");

    std::ostringstream log;
    fsm::tools::SimulationOptions opts;
    opts.strict_guards = true;
    auto report = fsm::tools::IrSimulator::simulate_detailed(model, "EvConfig\nEvDone\nEvCheck\n", log, opts);

    ASSERT_EQ(report.steps.size(), 4u);
    EXPECT_TRUE(report.steps[0].deferred);
    EXPECT_EQ(report.steps[1].to_state, "Ready");
    EXPECT_TRUE(report.steps[2].recalled);
    EXPECT_EQ(report.steps[2].to_state, "Configured");
    // Step 3: EvCheck has unresolved symbolic guard -> fail-closed under strict_guards
    EXPECT_FALSE(report.steps[3].handled);
    EXPECT_EQ(report.final_state, "Configured");
}

/**
 * @brief Seeded randomized differential verification: IrSimulator == C++17 == C99 ABI (with mid-trace snapshot).
 * @scenario Generate a deterministic 200-step randomized event trace over a 4-state EFSM with guarded register
 *           transitions and execute it identically across IrSimulator, generated C++17, and pure C99 ABI.
 * @expected Step-by-step state sequence matches 100% across all three execution engines, final register values match
 *           between IrSimulator and C++17, and mid-trace C99 snapshot/restore produces identical continuation.
 */
TEST(DifferentialSemantics, RandomizedTraces_MatchAcrossIrSimulatorCpp17AndC99Abi) {
    using namespace fsm::ir;
    using namespace fsm::backend::cpp;

    using namespace fsm::ir;
    using namespace fsm::backend::cpp;
    FsmIr model;
    model.name = "DiffValveFsm";
    model.package = "diff_valve";
    model.initial_state = "Idle";
    model.add_state("Idle");
    model.add_state("Charging");
    model.add_state("Active");
    model.add_state("Fault");

    model.add_event("Start");
    model.add_event("Pulse");
    model.add_event("FaultSig");
    model.add_event("Reset");

    model.add_variable(VariableDefinition("energy", DataType::int32(), "0"));
    model.add_variable(VariableDefinition("cycles", DataType::int32(), "0"));

    model.add_guard("low_energy", "", "energy < 3", "energy < 3");
    model.add_guard("full_energy", "", "energy >= 3", "energy >= 3");

    // Idle -> Charging on Start (energy = 0; cycles += 1)
    {
        TransitionEdge t("Idle", "Charging", "Start");
        ActionSignature act("begin_charge");
        act.assignments.push_back(ActionAssignment{LValueTarget{"energy", LValueScope::Register}, "0"});
        act.assignments.push_back(
            ActionAssignment{LValueTarget{"cycles", LValueScope::Register}, "1", AssignmentOp::AddAssign});
        t.transition_action = act;
        model.transitions.push_back(t);
    }
    // Charging -> Charging on Pulse [energy < 3] (energy += 1)
    {
        TransitionEdge t("Charging", "Charging", "Pulse", "low_energy");
        t.kind = TransitionEdgeKind::Internal;
        ActionSignature act("charge_step");
        act.assignments.push_back(
            ActionAssignment{LValueTarget{"energy", LValueScope::Register}, "1", AssignmentOp::AddAssign});
        t.transition_action = act;
        model.transitions.push_back(t);
    }
    // Charging -> Active on Pulse [energy >= 3]
    model.transitions.emplace_back("Charging", "Active", "Pulse", "full_energy");
    // Active -> Fault on FaultSig
    model.transitions.emplace_back("Active", "Fault", "FaultSig");
    // Charging -> Fault on FaultSig
    model.transitions.emplace_back("Charging", "Fault", "FaultSig");
    // Active -> Idle on Reset (energy = 0)
    {
        TransitionEdge t("Active", "Idle", "Reset");
        ActionSignature act("reset_active");
        act.assignments.push_back(ActionAssignment{LValueTarget{"energy", LValueScope::Register}, "0"});
        t.transition_action = act;
        model.transitions.push_back(t);
    }
    // Fault -> Idle on Reset (energy = 0)
    {
        TransitionEdge t("Fault", "Idle", "Reset");
        ActionSignature act("reset_fault");
        act.assignments.push_back(ActionAssignment{LValueTarget{"energy", LValueScope::Register}, "0"});
        t.transition_action = act;
        model.transitions.push_back(t);
    }

    // Generate deterministic 200-step randomized event trace
    constexpr std::size_t kSteps = 200;
    const std::vector<std::string> event_pool = {"Start", "Pulse", "FaultSig", "Reset"};
    std::mt19937 rng(0xF5C013U);
    std::uniform_int_distribution<std::size_t> dist(0, event_pool.size() - 1);

    std::vector<std::size_t> event_indices;
    event_indices.reserve(kSteps);
    std::ostringstream trace_ss;
    for (std::size_t i = 0; i < kSteps; ++i) {
        const std::size_t idx = dist(rng);
        event_indices.push_back(idx);
        trace_ss << event_pool[idx] << "\n";
    }

    // 1. Reference Oracle: IrSimulator
    std::ostringstream sim_log;
    const auto sim_report = fsm::tools::IrSimulator::simulate_detailed(model, trace_ss.str(), sim_log);
    ASSERT_EQ(sim_report.steps.size(), kSteps);

#if defined(_WIN32)
    GTEST_SKIP() << "POSIX cc/c++ compiler invocation skipped on Windows.";
#else
    const std::string cc = (std::system("cc --version > /dev/null 2>&1") == 0) ? "cc" : "gcc";
    const std::string cxx = (std::system("c++ --version > /dev/null 2>&1") == 0) ? "c++" : "g++";

    fs::path temp_dir = fs::temp_directory_path() / "fsmc_diff_random_test";
    fs::create_directories(temp_dir);

    GeneratorOptions opts;
    opts.standalone = true;
    opts.thread_safe = false;
    opts.target_namespace = "diff_valve";

    const auto cpp_bundle = CppGenerator::generate_bundle(model, opts, "diff_valve_ports.hpp");
    const auto c_bundle = CAbiGenerator::generate(model, opts, "diff_valve_fsm.hpp", "diff_valve_c.h");

    {
        std::ofstream f(temp_dir / "diff_valve_ports.hpp");
        f << cpp_bundle.ports_header;
    }
    {
        std::ofstream f(temp_dir / "diff_valve_fsm.hpp");
        f << cpp_bundle.fsm_header;
    }
    {
        std::ofstream f(temp_dir / "diff_valve_c.h");
        f << c_bundle.header;
    }
    {
        std::ofstream f(temp_dir / "diff_valve_c.cpp");
        f << c_bundle.source;
    }

    // Emit shared event trace C array header
    {
        std::ofstream f(temp_dir / "trace_data.h");
        f << "static const int kTraceEvents[" << kSteps << "] = {";
        for (std::size_t i = 0; i < kSteps; ++i) {
            if (i > 0) {
                f << ", ";
            }
            f << event_indices[i];
        }
        f << "};\n";
    }

    // 2. C++17 Harness
    const fs::path cpp_out = temp_dir / "cpp17_states.txt";
    {
        std::ofstream f(temp_dir / "driver_cpp17.cpp");
        f << "#include <cstdio>\n"
          << "#include \"diff_valve_fsm.hpp\"\n"
          << "#include \"trace_data.h\"\n"
          << "int main() {\n"
          << "    using namespace diff_valve;\n"
          << "    DiffValveFsm sm;\n"
          << "    std::FILE* fp = std::fopen(\"" << cpp_out.generic_string() << "\", \"w\");\n"
          << "    if (!fp) return 1;\n"
          << "    for (int i = 0; i < " << kSteps << "; ++i) {\n"
          << "        switch (kTraceEvents[i]) {\n"
          << "            case 0: sm.dispatch(Start{}); break;\n"
          << "            case 1: sm.dispatch(Pulse{}); break;\n"
          << "            case 2: sm.dispatch(FaultSig{}); break;\n"
          << "            default: sm.dispatch(Reset{}); break;\n"
          << "        }\n"
          << "        const char* st = sm.is_in<Idle>() ? \"Idle\" : (sm.is_in<Charging>() ? \"Charging\" : "
             "(sm.is_in<Active>() ? \"Active\" : \"Fault\"));\n"
          << "        std::fprintf(fp, \"%s\\n\", st);\n"
          << "    }\n"
          << "    std::fprintf(fp, \"REGS %d %d\\n\", sm.registers().energy, sm.registers().cycles);\n"
          << "    std::fclose(fp);\n"
          << "    return 0;\n"
          << "}\n";
    }

    // 3. Pure C99 ABI Harness (also exercises mid-trace snapshot/restore at step 100)
    const fs::path c99_out = temp_dir / "c99_states.txt";
    {
        std::ofstream f(temp_dir / "driver_c99.c");
        f << "#include <assert.h>\n"
          << "#include <stdio.h>\n"
          << "#include <string.h>\n"
          << "#include \"diff_valve_c.h\"\n"
          << "#include \"trace_data.h\"\n"
          << "int main(void) {\n"
          << "    diff_valve_fsm_fsm_t fsm;\n"
          << "    diff_valve_fsm_fsm_t restored;\n"
          << "    diff_valve_fsm_in_ports_t in_p;\n"
          << "    diff_valve_fsm_out_ports_t out_p;\n"
          << "    uint8_t snap[256];\n"
          << "    size_t written = 0;\n"
          << "    size_t read_sz = 0;\n"
          << "    FILE* fp = fopen(\"" << c99_out.generic_string() << "\", \"w\");\n"
          << "    if (!fp) return 1;\n"
          << "    memset(&in_p, 0, sizeof(in_p));\n"
          << "    memset(&out_p, 0, sizeof(out_p));\n"
          << "    diff_valve_fsm_init(&fsm);\n"
          << "    diff_valve_fsm_init(&restored);\n"
          << "    for (int i = 0; i < " << kSteps << "; ++i) {\n"
          << "        diff_valve_fsm_fsm_t* active = (i >= 100) ? &restored : &fsm;\n"
          << "        switch (kTraceEvents[i]) {\n"
          << "            case 0: diff_valve_fsm_dispatch_start(active, &in_p, &out_p); break;\n"
          << "            case 1: diff_valve_fsm_dispatch_pulse(active, &in_p, &out_p); break;\n"
          << "            case 2: diff_valve_fsm_dispatch_fault_sig(active, &in_p, &out_p); break;\n"
          << "            default: diff_valve_fsm_dispatch_reset(active, &in_p, &out_p); break;\n"
          << "        }\n"
          << "        fprintf(fp, \"%s\\n\", diff_valve_fsm_state_name(diff_valve_fsm_current_state(active)));\n"
          << "        if (i == 99) {\n"
          << "            assert(diff_valve_fsm_save_snapshot(&fsm, snap, sizeof(snap), &written));\n"
          << "            assert(diff_valve_fsm_restore_snapshot(&restored, snap, written, &read_sz));\n"
          << "            assert(read_sz == written);\n"
          << "        }\n"
          << "    }\n"
          << "    fclose(fp);\n"
          << "    diff_valve_fsm_destroy(&restored);\n"
          << "    diff_valve_fsm_destroy(&fsm);\n"
          << "    return 0;\n"
          << "}\n";
    }

    const fs::path cpp_exe = temp_dir / "runner_cpp17";
    const std::string cpp_cmd = cxx + " -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -I\"" +
                                temp_dir.string() + "\" \"" + (temp_dir / "driver_cpp17.cpp").string() + "\" -o \"" +
                                cpp_exe.string() + "\"";
    ASSERT_EQ(std::system(cpp_cmd.c_str()), 0) << "C++17 randomized differential compile failed";
    ASSERT_EQ(std::system(("\"" + cpp_exe.string() + "\"").c_str()), 0) << "C++17 randomized runner failed";

    const fs::path c_bridge_obj = temp_dir / "diff_valve_c.o";
    const fs::path c_client_obj = temp_dir / "driver_c99.o";
    const fs::path c99_exe = temp_dir / "runner_c99";

    const std::string bridge_cmd = cxx + " -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -I\"" +
                                   temp_dir.string() + "\" -c \"" + (temp_dir / "diff_valve_c.cpp").string() +
                                   "\" -o \"" + c_bridge_obj.string() + "\"";
    ASSERT_EQ(std::system(bridge_cmd.c_str()), 0) << "C++17 C-ABI bridge compile failed";

    const std::string cc_cmd = cc + " -std=c99 -Wall -Wextra -Werror -pedantic -I\"" + temp_dir.string() + "\" -c \"" +
                               (temp_dir / "driver_c99.c").string() + "\" -o \"" + c_client_obj.string() + "\"";
    ASSERT_EQ(std::system(cc_cmd.c_str()), 0) << "Pure C99 randomized differential compile failed";

    const std::string link_cmd = cxx + " -fno-exceptions -fno-rtti \"" + c_client_obj.string() + "\" \"" +
                                 c_bridge_obj.string() + "\" -o \"" + c99_exe.string() + "\"";
    ASSERT_EQ(std::system(link_cmd.c_str()), 0) << "C99 randomized differential link failed";
    ASSERT_EQ(std::system(("\"" + c99_exe.string() + "\"").c_str()), 0) << "C99 randomized runner failed";

    // Compare all 200 step states across IrSimulator == C++17 == C99 ABI
    std::ifstream cpp_in(cpp_out);
    std::ifstream c99_in(c99_out);
    for (std::size_t i = 0; i < kSteps; ++i) {
        std::string cpp_st;
        std::string c99_st;
        ASSERT_TRUE(static_cast<bool>(cpp_in >> cpp_st)) << "Missing C++17 state at step " << i;
        ASSERT_TRUE(static_cast<bool>(c99_in >> c99_st)) << "Missing C99 state at step " << i;
        EXPECT_EQ(cpp_st, sim_report.steps[i].to_state) << "IrSimulator vs C++17 mismatch at step " << i;
        EXPECT_EQ(c99_st, sim_report.steps[i].to_state) << "IrSimulator vs C99 ABI mismatch at step " << i;
    }

    std::string tag;
    int cpp_energy = -1;
    int cpp_cycles = -1;
    ASSERT_TRUE(static_cast<bool>(cpp_in >> tag >> cpp_energy >> cpp_cycles));
    EXPECT_EQ(tag, "REGS");
    EXPECT_EQ(cpp_energy, static_cast<int>(sim_report.final_registers.at("energy")));
    EXPECT_EQ(cpp_cycles, static_cast<int>(sim_report.final_registers.at("cycles")));

    fs::remove_all(temp_dir);
#endif
}

/**
 * @brief 3-Way Differential Verification: IrSimulator == C++17 == Native Pure-C99 (500-step randomized trace +
 *        mid-trace snapshot/restore + byte-identical determinism check).
 */
TEST(DifferentialSemantics, ThreeWayRandomizedTraces_MatchIrSimulatorCpp17AndNativeC99) {
    using namespace fsm::ir;
    using namespace fsm::backend;
    using namespace fsm::backend::cpp;
    using namespace fsm::backend::c99;

    using namespace fsm::ir;
    using namespace fsm::backend::cpp;
    FsmIr model;
    model.name = "TriadCtrl";
    model.package = "triad_pkg";
    model.initial_state = "Idle";
    model.add_state("Idle");
    model.add_state("Warmup");
    model.add_state("Active");
    model.add_state("Fault");

    model.add_event("Start");
    model.add_event("Step");
    model.add_event("Trip");
    model.add_event("Reset");

    model.add_variable(VariableDefinition("heat", DataType::int32(), "0"));
    model.add_variable(VariableDefinition("runs", DataType::int32(), "0"));

    model.add_guard("heating", "", "heat < 4", "heat < 4");
    model.add_guard("ready", "", "heat >= 4", "heat >= 4");

    {
        TransitionEdge t("Idle", "Warmup", "Start");
        ActionSignature act("start_warmup");
        act.assignments.push_back(ActionAssignment{LValueTarget{"heat", LValueScope::Register}, "0"});
        act.assignments.push_back(
            ActionAssignment{LValueTarget{"runs", LValueScope::Register}, "1", AssignmentOp::AddAssign});
        t.transition_action = act;
        model.transitions.push_back(t);
    }
    {
        TransitionEdge t("Warmup", "Warmup", "Step", "heating");
        t.kind = TransitionEdgeKind::Internal;
        ActionSignature act("inc_heat");
        act.assignments.push_back(
            ActionAssignment{LValueTarget{"heat", LValueScope::Register}, "1", AssignmentOp::AddAssign});
        t.transition_action = act;
        model.transitions.push_back(t);
    }
    model.transitions.emplace_back("Warmup", "Active", "Step", "ready");
    model.transitions.emplace_back("Warmup", "Fault", "Trip");
    model.transitions.emplace_back("Active", "Fault", "Trip");
    {
        TransitionEdge t("Active", "Idle", "Reset");
        ActionSignature act("reset_act");
        act.assignments.push_back(ActionAssignment{LValueTarget{"heat", LValueScope::Register}, "0"});
        t.transition_action = act;
        model.transitions.push_back(t);
    }
    {
        TransitionEdge t("Fault", "Idle", "Reset");
        ActionSignature act("reset_flt");
        act.assignments.push_back(ActionAssignment{LValueTarget{"heat", LValueScope::Register}, "0"});
        t.transition_action = act;
        model.transitions.push_back(t);
    }

    // Verify byte-identical determinism across repeated codegen & manifest runs
    const auto c99_a = C99Generator::generate(model, C99GeneratorOptions{"triad_ctrl.h"});
    const auto c99_b = C99Generator::generate(model, C99GeneratorOptions{"triad_ctrl.h"});
    EXPECT_EQ(c99_a.header, c99_b.header);
    EXPECT_EQ(c99_a.source, c99_b.source);
    EXPECT_EQ(ManifestEmitter::emit_json(model), ManifestEmitter::emit_json(model));

    constexpr std::size_t kSteps = 500;
    const std::vector<std::string> ev_names = {"Start", "Step", "Trip", "Reset"};
    std::mt19937 rng(0xF5C014U);
    std::uniform_int_distribution<std::size_t> dist(0, ev_names.size() - 1);

    std::vector<std::size_t> ev_seq;
    ev_seq.reserve(kSteps);
    std::ostringstream trace_ss;
    for (std::size_t i = 0; i < kSteps; ++i) {
        const std::size_t idx = dist(rng);
        ev_seq.push_back(idx);
        trace_ss << ev_names[idx] << "\n";
    }

    // 1. Oracle: IrSimulator
    std::ostringstream sim_log;
    const auto sim_report = fsm::tools::IrSimulator::simulate_detailed(model, trace_ss.str(), sim_log);
    ASSERT_EQ(sim_report.steps.size(), kSteps);

#if defined(_WIN32)
    GTEST_SKIP() << "POSIX cc/c++ compiler invocation skipped on Windows.";
#else
    const std::string cc = (std::system("cc --version > /dev/null 2>&1") == 0) ? "cc" : "gcc";
    const std::string cxx = (std::system("c++ --version > /dev/null 2>&1") == 0) ? "c++" : "g++";

    const fs::path temp_dir = fs::temp_directory_path() / "fsmc_diff_3way_test";
    fs::create_directories(temp_dir);

    GeneratorOptions opts;
    opts.standalone = true;
    opts.thread_safe = false;
    opts.target_namespace = "triad_pkg";
    const auto cpp_bundle = CppGenerator::generate_bundle(model, opts, "triad_ports.hpp");

    {
        std::ofstream f(temp_dir / "triad_ports.hpp");
        f << cpp_bundle.ports_header;
    }
    {
        std::ofstream f(temp_dir / "triad_fsm.hpp");
        f << cpp_bundle.fsm_header;
    }
    {
        std::ofstream f(temp_dir / "triad_ctrl.h");
        f << c99_a.header;
    }
    {
        std::ofstream f(temp_dir / "triad_ctrl.c");
        f << c99_a.source;
    }
    {
        std::ofstream f(temp_dir / "trace_500.h");
        f << "static const int kTrace500[" << kSteps << "] = {";
        for (std::size_t i = 0; i < kSteps; ++i) {
            if (i > 0) {
                f << ", ";
            }
            f << ev_seq[i];
        }
        f << "};\n";
    }

    // 2. C++17 Harness
    const fs::path cpp_out = temp_dir / "cpp17_out.txt";
    {
        std::ofstream f(temp_dir / "drv_cpp.cpp");
        f << "#include <cstdio>\n"
          << "#include \"triad_fsm.hpp\"\n"
          << "#include \"trace_500.h\"\n"
          << "int main() {\n"
          << "    using namespace triad_pkg;\n"
          << "    TriadCtrl sm;\n"
          << "    std::FILE* fp = std::fopen(\"" << cpp_out.generic_string() << "\", \"w\");\n"
          << "    if (!fp) return 1;\n"
          << "    for (int i = 0; i < " << kSteps << "; ++i) {\n"
          << "        switch (kTrace500[i]) {\n"
          << "            case 0: sm.dispatch(Start{}); break;\n"
          << "            case 1: sm.dispatch(Step{}); break;\n"
          << "            case 2: sm.dispatch(Trip{}); break;\n"
          << "            default: sm.dispatch(Reset{}); break;\n"
          << "        }\n"
          << "        const char* st = sm.is_in<Idle>() ? \"Idle\" : (sm.is_in<Warmup>() ? \"Warmup\" : "
             "(sm.is_in<Active>() ? \"Active\" : \"Fault\"));\n"
          << "        std::fprintf(fp, \"%s\\n\", st);\n"
          << "    }\n"
          << "    std::fprintf(fp, \"REGS %d %d\\n\", sm.registers().heat, sm.registers().runs);\n"
          << "    std::fclose(fp);\n"
          << "    return 0;\n"
          << "}\n";
    }

    // 3. Native Pure-C99 Harness (compiled ONLY with C99 compiler, mid-trace snapshot/restore at step 250)
    const fs::path c99_out = temp_dir / "c99_out.txt";
    {
        std::ofstream f(temp_dir / "drv_c99.c");
        f << "#include <assert.h>\n"
          << "#include <stdio.h>\n"
          << "#include <string.h>\n"
          << "#include \"triad_ctrl.h\"\n"
          << "#include \"trace_500.h\"\n"
          << "int main(void) {\n"
          << "    triad_ctrl_fsm_t fsm;\n"
          << "    triad_ctrl_fsm_t restored;\n"
          << "    triad_ctrl_in_ports_t in_p;\n"
          << "    triad_ctrl_out_ports_t out_p;\n"
          << "    uint8_t snap[128];\n"
          << "    size_t written = 0;\n"
          << "    size_t read_sz = 0;\n"
          << "    FILE* fp = fopen(\"" << c99_out.generic_string() << "\", \"w\");\n"
          << "    if (!fp) return 1;\n"
          << "    memset(&in_p, 0, sizeof(in_p));\n"
          << "    memset(&out_p, 0, sizeof(out_p));\n"
          << "    triad_ctrl_init(&fsm);\n"
          << "    triad_ctrl_init(&restored);\n"
          << "    for (int i = 0; i < " << kSteps << "; ++i) {\n"
          << "        triad_ctrl_fsm_t* cur = (i >= 250) ? &restored : &fsm;\n"
          << "        switch (kTrace500[i]) {\n"
          << "            case 0: triad_ctrl_dispatch_start(cur, &in_p, &out_p); break;\n"
          << "            case 1: triad_ctrl_dispatch_step(cur, &in_p, &out_p); break;\n"
          << "            case 2: triad_ctrl_dispatch_trip(cur, &in_p, &out_p); break;\n"
          << "            default: triad_ctrl_dispatch_reset(cur, &in_p, &out_p); break;\n"
          << "        }\n"
          << "        fprintf(fp, \"%s\\n\", triad_ctrl_state_name(triad_ctrl_current_state(cur)));\n"
          << "        if (i == 249) {\n"
          << "            assert(triad_ctrl_save_snapshot(&fsm, snap, sizeof(snap), &written));\n"
          << "            assert(triad_ctrl_restore_snapshot(&restored, snap, written, &read_sz));\n"
          << "            assert(read_sz == written);\n"
          << "        }\n"
          << "    }\n"
          << "    fprintf(fp, \"REGS %d %d\\n\", restored.regs.heat, restored.regs.runs);\n"
          << "    fclose(fp);\n"
          << "    triad_ctrl_destroy(&restored);\n"
          << "    triad_ctrl_destroy(&fsm);\n"
          << "    return 0;\n"
          << "}\n";
    }

    const fs::path cpp_exe = temp_dir / "runner_cpp";
    const std::string cpp_cmd = cxx + " -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -I\"" +
                                temp_dir.string() + "\" \"" + (temp_dir / "drv_cpp.cpp").string() + "\" -o \"" +
                                cpp_exe.string() + "\"";
    ASSERT_EQ(std::system(cpp_cmd.c_str()), 0) << "C++17 3-way compile failed";
    ASSERT_EQ(std::system(("\"" + cpp_exe.string() + "\"").c_str()), 0) << "C++17 3-way runner failed";

    const fs::path c99_exe = temp_dir / "runner_c99";
    const std::string c99_cmd = cc + " -std=c99 -Wall -Wextra -Werror -pedantic -I\"" + temp_dir.string() + "\" \"" +
                                (temp_dir / "triad_ctrl.c").string() + "\" \"" + (temp_dir / "drv_c99.c").string() +
                                "\" -o \"" + c99_exe.string() + "\"";
    ASSERT_EQ(std::system(c99_cmd.c_str()), 0) << "Pure C99 3-way compile failed";
    ASSERT_EQ(std::system(("\"" + c99_exe.string() + "\"").c_str()), 0) << "Pure C99 3-way runner failed";

    std::ifstream cpp_in(cpp_out);
    std::ifstream c99_in(c99_out);
    for (std::size_t i = 0; i < kSteps; ++i) {
        std::string cpp_st;
        std::string c99_st;
        ASSERT_TRUE(static_cast<bool>(cpp_in >> cpp_st)) << "Missing C++17 state at step " << i;
        ASSERT_TRUE(static_cast<bool>(c99_in >> c99_st)) << "Missing C99 state at step " << i;
        EXPECT_EQ(cpp_st, sim_report.steps[i].to_state) << "IrSimulator vs C++17 mismatch at step " << i;
        EXPECT_EQ(c99_st, sim_report.steps[i].to_state) << "IrSimulator vs Pure-C99 mismatch at step " << i;
    }

    std::string tag1;
    std::string tag2;
    int cpp_heat = -1;
    int cpp_runs = -1;
    int c99_heat = -1;
    int c99_runs = -1;
    ASSERT_TRUE(static_cast<bool>(cpp_in >> tag1 >> cpp_heat >> cpp_runs));
    ASSERT_TRUE(static_cast<bool>(c99_in >> tag2 >> c99_heat >> c99_runs));
    EXPECT_EQ(tag1, "REGS");
    EXPECT_EQ(tag2, "REGS");
    EXPECT_EQ(cpp_heat, static_cast<int>(sim_report.final_registers.at("heat")));
    EXPECT_EQ(cpp_runs, static_cast<int>(sim_report.final_registers.at("runs")));
    EXPECT_EQ(c99_heat, cpp_heat);
    EXPECT_EQ(c99_runs, cpp_runs);

    fs::remove_all(temp_dir);
#endif
}

/**
 * @brief 3-Way Differential Verification of SignalEmitOp Outbox across IrSimulator, C++17, and Native Pure-C99.
 * @scenario Execute a transition emitting a typed signal via an output port on IrSimulator, compiled C++17, and
 * compiled Pure-C99.
 * @expected All three runtimes emit the exact same signal name, port name, and numeric payload value.
 */
TEST(DifferentialSemantics, SignalEmitOp_MatchesAcrossIrSimulatorCpp17AndNativeC99) {
#if defined(_WIN32)
    GTEST_SKIP() << "POSIX C/C++ compiler invocation test skipped on Windows.";
#else
    using namespace fsm::ir;
    using namespace fsm::backend::cpp;
    FsmIr model;
    model.name = "EmitOracle";
    model.initial_state = "Idle";
    model.add_state("Idle");
    model.add_state("Alerted");
    model.add_variable(VariableDefinition("level", DataType::int32(), "42"));
    model.add_signal(SignalDefinition("EvTrigger"));
    SignalDefinition sig_alarm("EvAlarm");
    sig_alarm.attributes.push_back(SignalAttribute{"severity", DataType::int32()});
    model.add_signal(sig_alarm);

    TransitionEdge t("Idle", "Alerted", "EvTrigger");
    ActionSignature act("RaiseAlarm");
    SignalEmitOp se;
    se.signal_name = "EvAlarm";
    se.target_port = "alarm_port";
    se.arguments = {"level"};
    act.instructions.emplace_back(se, "emit alarm");
    t.transition_action = act;
    model.add_transition(t);

    // 1. IrSimulator
    std::ostringstream sim_log;
    const auto sim_rep = fsm::ir::IrSimulator::simulate_detailed(model, "EvTrigger\n", sim_log);
    ASSERT_EQ(sim_rep.final_state, "Alerted");
    ASSERT_EQ(sim_rep.steps.size(), 1U);
    ASSERT_EQ(sim_rep.steps[0].emitted_events.size(), 1U);
    EXPECT_EQ(sim_rep.steps[0].emitted_events[0].signal_name, "EvAlarm");
    EXPECT_EQ(sim_rep.steps[0].emitted_events[0].target_port, "alarm_port");
    ASSERT_EQ(sim_rep.steps[0].emitted_events[0].numeric_args.size(), 1U);
    EXPECT_EQ(static_cast<int>(sim_rep.steps[0].emitted_events[0].numeric_args[0]), 42);

    const fs::path temp_dir = fs::temp_directory_path() / "fsmc_diff_emit_3way";
    fs::create_directories(temp_dir);

    // 2. C++17 Standalone
    GeneratorOptions opts;
    opts.standalone = true;
    opts.include_stubs = false;
    {
        std::ofstream(temp_dir / "emit_oracle.hpp") << CppGenerator::generate_header(model, opts);
        std::ofstream(temp_dir / "main_cpp.cpp") << R"cpp(
#include <cassert>
#include <cstring>
#include "emit_oracle.hpp"
            int main() {
                using namespace fsm_generated;
                EmitOracle sm;
                EmitOracleInPorts in{};
                EmitOracleOutPorts out{};
                sm.dispatch(EvTrigger{}, in, out);
                assert(sm.is_in<Alerted>());
                assert(out.outbox_count == 1U);
                assert(out.outbox[0].tag == EmitOracleEmittedEventTag::EvAlarm);
                assert(std::strcmp(out.outbox[0].port, "alarm_port") == 0);
                assert(out.outbox[0].EvAlarm_payload.severity == 42);
                return 0;
            }
        )cpp";
    }
    const std::string cxx_cmd = "c++ -std=c++17 -O2 -Wall -Wextra -Werror -I\"" + temp_dir.string() + "\" \"" +
                                (temp_dir / "main_cpp.cpp").string() + "\" -o \"" + (temp_dir / "cpp_bin").string() +
                                "\"";
    ASSERT_EQ(std::system(cxx_cmd.c_str()), 0) << "C++17 SignalEmitOp compile failed";
    ASSERT_EQ(std::system(("\"" + (temp_dir / "cpp_bin").string() + "\"").c_str()), 0);

    // 3. Pure-C99
    const auto c99 = fsm::backend::c99::C99Generator::generate(model);
    {
        std::ofstream(temp_dir / "emit_oracle.h") << c99.header;
        std::ofstream(temp_dir / "emit_oracle.c") << c99.source;
        std::ofstream(temp_dir / "main_c.c") << R"c(
#include <assert.h>
#include <string.h>
#include "emit_oracle.h"
int main(void) {
    emit_oracle_fsm_t fsm;
    emit_oracle_in_ports_t in_p;
    emit_oracle_out_ports_t out_p;
    memset(&in_p, 0, sizeof(in_p));
    memset(&out_p, 0, sizeof(out_p));
    emit_oracle_init(&fsm);
    assert(emit_oracle_dispatch_ev_trigger(&fsm, &in_p, &out_p) == EMIT_ORACLE_STATUS_OK);
    assert(emit_oracle_current_state(&fsm) == EMIT_ORACLE_STATE_ALERTED);
    assert(out_p.outbox_count == 1U);
    assert(out_p.outbox[0].tag == EMIT_ORACLE_OUT_EV_ALARM);
    assert(strcmp(out_p.outbox[0].port, "alarm_port") == 0);
    assert(out_p.outbox[0].ev_alarm.severity == 42);
    return 0;
}
)c";
    }
    const std::string c99_cmd = "cc -std=c99 -O2 -Wall -Wextra -Werror -pedantic -I\"" + temp_dir.string() + "\" \"" +
                                (temp_dir / "emit_oracle.c").string() + "\" \"" + (temp_dir / "main_c.c").string() +
                                "\" -o \"" + (temp_dir / "c99_bin").string() + "\"";
    ASSERT_EQ(std::system(c99_cmd.c_str()), 0) << "Pure-C99 SignalEmitOp compile failed";
    ASSERT_EQ(std::system(("\"" + (temp_dir / "c99_bin").string() + "\"").c_str()), 0);

    fs::remove_all(temp_dir);
#endif
}

/**
 * @brief Test Intent: Verify OMG UML 2.5.1 §14.2.3.8 Local vs External transition semantics in IrSimulator and
 * CppGenerator (External exits/re-enters composite source; Local preserves composite source).
 */
TEST(DifferentialSemantics, LocalVsExternalTransitionSemantics_Uml251) {
    using namespace fsm::ir;
    auto build_model = [](TransitionEdgeKind kind) {
        FsmIr ir;
        ir.name = "LocalVsExt";
        ir.initial_state = "Active";
        ir.add_state("Active");
        ir.find_state_mut("Active")->is_composite = true;
        ir.find_state_mut("Active")->initial_sub_state = "SubA";
        ir.add_state("SubA", "Active");
        ir.add_state("SubB", "Active");
        ir.add_signal(SignalDefinition("Switch"));

        TransitionEdge tr("Active", "SubB", "Switch");
        tr.kind = kind;
        ir.add_transition(std::move(tr));
        return ir;
    };

    {
        FsmIr ext_ir = build_model(TransitionEdgeKind::External);
        std::ostringstream log;
        auto rep = IrSimulator::simulate_detailed(ext_ir, "Switch\n", log);
        ASSERT_EQ(rep.steps.size(), 1U);
        EXPECT_EQ(rep.steps[0].exited_states, (std::vector<std::string>{"SubA", "Active"}));
        EXPECT_EQ(rep.steps[0].entered_states, (std::vector<std::string>{"Active", "SubB"}));
    }

    {
        FsmIr loc_ir = build_model(TransitionEdgeKind::Local);
        std::ostringstream log;
        auto rep = IrSimulator::simulate_detailed(loc_ir, "Switch\n", log);
        ASSERT_EQ(rep.steps.size(), 1U);
        EXPECT_EQ(rep.steps[0].exited_states, (std::vector<std::string>{"SubA"}));
        EXPECT_EQ(rep.steps[0].entered_states, (std::vector<std::string>{"SubB"}));

        const std::string cpp = fsm::backend::cpp::CppGenerator::generate_header(loc_ir);
        EXPECT_NE(cpp.find("::fsm::local_row<"), std::string::npos);
    }
}

/**
 * @brief Test Intent: Verify IrSimulator advances state_elapsed_ms via `tick <ms>` trace steps and triggers
 * `TimeTrigger` (`after`) transitions.
 */
TEST(DifferentialSemantics, IrSimulator_TickAdvancesElapsedMsForTimeTriggers) {
    using namespace fsm::ir;
    FsmIr ir;
    ir.name = "BlinkerSim";
    ir.initial_state = "OffState";
    ir.add_state("OffState");
    ir.add_state("OnState");
    TransitionEdge t("OffState", "OnState", "");
    t.trigger = TimeTrigger(100U);
    ir.add_transition(std::move(t));

    std::ostringstream log;
    auto rep = IrSimulator::simulate_detailed(ir, "tick 60\ntick 50\n", log);
    ASSERT_EQ(rep.steps.size(), 2U);
    EXPECT_FALSE(rep.steps[0].handled);
    EXPECT_TRUE(rep.steps[1].handled);
    EXPECT_EQ(rep.final_state, "OnState");
}

}  // namespace

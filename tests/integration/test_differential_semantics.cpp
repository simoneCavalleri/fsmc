/**
 * @file test_differential_semantics.cpp
 * @brief Differential verification suite comparing IrSimulator (reference oracle) against generated C++17 and C++20
 * FSMs.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "tools/fsmc/ir_simulator.hpp"

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

}  // namespace

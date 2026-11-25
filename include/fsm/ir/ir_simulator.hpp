#pragma once

#include <cstddef>
#include <iosfwd>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::ir {

struct SimulatedEvent {
    std::string name;
    std::unordered_map<std::string, double> numeric_payload;
    std::unordered_map<std::string, std::string> string_payload;
};

struct EmittedSignalRecord {
    std::string signal_name;
    std::string target_port;
    std::vector<double> numeric_args;
};

struct SimulationOptions {
    bool strict_guards = false;  ///< When true, unresolved symbolic guards fail-closed (return false)
};

struct SimulationTraceStep {
    std::size_t step_index{0};
    std::string from_state;
    std::string event;
    std::string to_state;
    bool handled{false};
    bool deferred{false};
    bool recalled{false};
    bool used_symbolic_guard{false};
    std::vector<std::string> exited_states;
    std::vector<std::string> entered_states;
    std::vector<std::string> executed_actions;
    std::vector<EmittedSignalRecord> emitted_events;
    std::map<std::string, double> registers_snapshot;
};

struct SimulationReport {
    std::string model_name;
    std::string initial_state;
    std::string final_state;
    std::vector<SimulationTraceStep> steps;
    std::map<std::string, double> final_registers;

    [[nodiscard]] std::string to_json() const;
};

class IrSimulator {
  public:
    static SimulationReport simulate_detailed(const FsmIr& model, const std::string& trace_content, std::ostream& out,
                                              const SimulationOptions& sim_opts = {});

    static std::string simulate_trace(const FsmIr& model, const std::string& trace_content, std::ostream& out);
};

}  // namespace fsm::ir

namespace fsm::tools {
using fsm::ir::EmittedSignalRecord;
using fsm::ir::IrSimulator;
using fsm::ir::SimulatedEvent;
using fsm::ir::SimulationOptions;
using fsm::ir::SimulationReport;
using fsm::ir::SimulationTraceStep;
}  // namespace fsm::tools

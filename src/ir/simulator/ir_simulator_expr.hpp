#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "fsm/ir/ir_simulator.hpp"

namespace fsm::ir::simulator_detail {

std::string trim_sv(std::string_view sv);
std::vector<std::string> split_top_level(std::string_view sv, std::string_view delim);

bool eval_numeric_predicate(std::string_view expr, const std::unordered_map<std::string, double>& env,
                            bool& symbolic_fallback, const FsmIr* model = nullptr, bool strict_fail_closed = false);

std::vector<SimulatedEvent> parse_trace_input(const std::string& content);

}  // namespace fsm::ir::simulator_detail

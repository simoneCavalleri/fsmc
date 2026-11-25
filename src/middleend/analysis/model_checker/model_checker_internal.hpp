#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/efsm_interval_analysis.hpp"

namespace fsm::middleend::analysis::mc_detail {

bool eval_atom_predicate(
    std::string_view raw_atom, const std::string& state, const fsm::ir::FsmIr& ir,
    const std::unordered_map<std::string, std::unordered_map<std::string, Interval>>& state_intervals);

}  // namespace fsm::middleend::analysis::mc_detail

#pragma once

#include <string>
#include <string_view>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::frontend::formal::stateflow_detail {

fsm::ir::ActionSignature build_stateflow_action(std::string_view raw_action, const std::string& fallback_prefix);

}  // namespace fsm::frontend::formal::stateflow_detail

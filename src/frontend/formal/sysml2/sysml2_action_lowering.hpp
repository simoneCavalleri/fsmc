#pragma once

#include <string>
#include <vector>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::frontend::formal::sysml2_detail {

bool try_parse_service_call_statement(const std::string& statement, const fsm::ir::FsmIr& model,
                                      std::vector<fsm::ir::ActionAstNode>& instructions);

bool try_parse_assignment_statement(const std::string& statement, std::vector<fsm::ir::ActionAssignment>& assignments,
                                    const fsm::ir::FsmIr* model = nullptr);

}  // namespace fsm::frontend::formal::sysml2_detail

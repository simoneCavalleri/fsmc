#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::frontend::formal::sysml2_detail {

struct LocalItemSpec {
    std::string type_name;
    std::vector<std::pair<std::string, std::string>> field_bindings;
};

using LocalItemMap = std::unordered_map<std::string, LocalItemSpec>;

/// Splits an action block body into top-level statements at `;` or closing `}` of inline item blocks (depth 0).
std::vector<std::string> split_action_block_statements(std::string_view block);

/// Extracts `(optional_action_name, balanced_brace_body)` from `do [action] [Name] { ... }` in `stmt`.
bool extract_do_action_block(std::string_view stmt, std::string& out_explicit_name, std::string& out_body);

/// Parses OMG SysML v2.0 local item instantiation:
///   `item msg : EvOverTemp { attribute :>> peak_temp = sensor_temp_c; }`
bool try_parse_local_item_usage(const std::string& statement, LocalItemMap& local_items);

/// Parses OMG SysML v2.0 send statements:
///   - `send msg via link` / `send msg to link.alert` (resolving `msg` from `local_items`)
///   - `send new EvOverTemp(peak_temp = sensor_temp_c) via link` (`NamedArgumentList`)
///   - `send new EvOverTemp() via link` / positional `send new EvOverTemp(val) via link`
bool try_parse_send_statement(const std::string& statement, const LocalItemMap& local_items, fsm::ir::FsmIr& model,
                              fsm::ir::SignalEmitOp& out_emit, std::string& out_action_name,
                              std::string& out_invocation);

bool try_parse_service_call_statement(const std::string& statement, const fsm::ir::FsmIr& model,
                                      std::vector<fsm::ir::ActionAstNode>& instructions);

bool try_parse_assignment_statement(const std::string& statement, std::vector<fsm::ir::ActionAssignment>& assignments,
                                    const fsm::ir::FsmIr* model = nullptr);

}  // namespace fsm::frontend::formal::sysml2_detail

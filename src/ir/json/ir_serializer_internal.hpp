#pragma once

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::ir::json_detail {

std::string escape_json(std::string_view s);
void emit_assignments_array(std::ostream& out, const std::vector<ActionAssignment>& assignments);
void emit_string_array(std::ostream& out, const std::vector<std::string>& items);
void emit_instructions_array(std::ostream& out, const std::vector<ActionAstNode>& instructions);
void emit_action_signature_object(std::ostream& out, const ActionSignature& act);
void emit_transition_metadata(std::ostream& out, const TransitionEdge& t, const std::string& field_pad,
                              bool is_top_level = false);
void emit_state(std::ostream& out, const StateNode& state, const FsmIr& model, int indent);

}  // namespace fsm::ir::json_detail

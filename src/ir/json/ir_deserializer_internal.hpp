#pragma once

#include <string>

#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/json_dom.hpp"

namespace fsm::ir::json_detail {

ActionAssignment parse_assignment_object(const JsonValue& a_val);
ActionSignature parse_action_signature_object(const JsonValue& act_val);
void parse_single_transition_object(const JsonValue& trans_obj, const std::string& source_state,
                                    const std::string& event_name, FsmIr& model);
void parse_on_transitions(const JsonValue& on_obj, const std::string& source_state, FsmIr& model);
void parse_single_state_entry(const std::string& state_key, const JsonValue& state_data, FsmIr& model,
                              const std::string& current_parent);
void parse_states_object(const JsonValue& states_obj, FsmIr& model, const std::string& current_parent);

}  // namespace fsm::ir::json_detail

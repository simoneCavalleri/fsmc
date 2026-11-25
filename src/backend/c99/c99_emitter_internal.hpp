#pragma once

#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "fsm/backend/c99/c99_generator.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::c99::c99_detail {

struct C99Field {
    std::string name;
    std::string c_type;
    bool is_struct = false;
};

struct C99LocalStruct {
    std::string ir_name;
    std::string c_type_name;
    std::vector<C99Field> fields;
};

struct C99RegField {
    std::string name;
    std::string c_type;
    std::string init_expr;
    bool is_struct = false;
};

struct C99Context {
    std::string prefix;
    std::string upper_prefix;
    std::unordered_map<std::string, std::string> custom_c_types;
    std::unordered_set<std::string> struct_ir_names;
    std::unordered_set<std::string> in_ports;
    std::unordered_set<std::string> out_ports;
    std::unordered_set<std::string> registers;
    std::unordered_map<std::string, const fsm::ir::ServiceMethodSignature*> services;
    std::unordered_map<std::string, std::string> state_enums;
};

struct C99EmitPlan {
    const fsm::ir::FsmIr& processed;
    const C99Context& ctx;
    const std::string& header_inc;
    const std::string& ir_sha256;
    const std::vector<const fsm::ir::TypeDefinition*>& external_types;
    const std::vector<const fsm::ir::TypeDefinition*>& local_enums;
    const std::vector<C99LocalStruct>& local_structs;
    const std::vector<C99Field>& in_fields;
    const std::vector<C99Field>& out_fields;
    const std::vector<C99RegField>& reg_fields;
    const std::vector<fsm::ir::SignalDefinition>& events;
    bool has_typed_services = false;
};

std::string to_snake_case(std::string_view raw);
std::string to_upper_ascii(std::string_view s);
std::string sanitize_c_ident(std::string_view raw);
std::string lower_external_module_to_c_header(std::string_view module);
std::string lower_external_symbol_to_c_type(std::string_view symbol);
bool map_type_to_c99(const fsm::ir::DataType& dt, const C99Context& ctx, std::string& out_c_type, bool& out_is_struct);
std::string translate_expr_to_c(std::string_view expr, const C99Context& ctx,
                                const std::unordered_set<std::string>& event_attrs = {});
std::string format_lvalue_c(const fsm::ir::LValueTarget& target, const C99Context& ctx);
void emit_action_c(std::ostringstream& src, const fsm::ir::ActionSignature& act, const C99Context& ctx,
                   const std::unordered_set<std::string>& event_attrs, const std::string& indent);
std::string resolve_guard_expr_c(const fsm::ir::TransitionEdge& tr, const fsm::ir::FsmIr& model, const C99Context& ctx,
                                 const std::unordered_set<std::string>& event_attrs = {});
void emit_transition_body_c(std::ostringstream& src, const fsm::ir::TransitionEdge& tr, const fsm::ir::FsmIr& model,
                            const C99Context& ctx, const std::unordered_set<std::string>& event_attrs,
                            const std::string& indent, bool is_periodic_timer = false, uint64_t timer_dur_ms = 0U);

std::string emit_c99_header(const C99EmitPlan& plan);
std::string emit_c99_source(const C99EmitPlan& plan);

}  // namespace fsm::backend::c99::c99_detail

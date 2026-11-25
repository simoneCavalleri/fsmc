#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::cpp::emitter_detail {

std::string resolve_external_cpp_symbol(std::string_view symbol);
std::string resolve_external_cpp_include(std::string_view module_path);
bool is_ident_start(char c) noexcept;
bool is_ident_char(char c) noexcept;
std::string escape_cpp_string_literal(std::string_view text);
std::string sanitize_cpp_comment(std::string_view text);
std::string sanitize_cpp_model_name(std::string_view raw_name);
std::string resolve_action_cpp_name(const std::string& raw_name, const fsm::ir::FsmIr& model);
bool has_unresolved_identifiers(std::string_view expr);
bool has_self_constructor_call(std::string_view expr, std::string_view struct_name);
std::string qualify_enum_literal(std::string_view tok, const fsm::ir::FsmIr& model,
                                 std::string_view preferred_enum_type = "");
std::string qualify_expression(std::string expr, const fsm::ir::FsmIr& model);
bool has_member_access_prefix(const std::string& expr, std::initializer_list<std::string_view> prefixes);

}  // namespace fsm::backend::cpp::emitter_detail

#include "fsm/frontend/common/parser_interface.hpp"

#include "fsm/ir/identifier_utils.hpp"

namespace fsm::frontend {

bool is_cpp_keyword(std::string_view token) noexcept {
    return ir::is_cpp_keyword(token);
}

std::string escape_cpp_keyword(std::string_view token) {
    return ir::escape_cpp_keyword(token);
}

std::string sanitize_identifier(std::string_view str) {
    return ir::sanitize_identifier(str);
}

}  // namespace fsm::frontend

#include "fsm/frontend/common/parser_interface.hpp"

namespace fsm::frontend {

bool is_cpp_keyword(std::string_view token) noexcept {
    static constexpr std::string_view keywords[] = {"alignas",
                                                    "alignof",
                                                    "and",
                                                    "and_eq",
                                                    "asm",
                                                    "atomic_cancel",
                                                    "atomic_commit",
                                                    "atomic_noexcept",
                                                    "auto",
                                                    "bitand",
                                                    "bitor",
                                                    "bool",
                                                    "break",
                                                    "case",
                                                    "catch",
                                                    "char",
                                                    "char8_t",
                                                    "char16_t",
                                                    "char32_t",
                                                    "class",
                                                    "compl",
                                                    "concept",
                                                    "const",
                                                    "consteval",
                                                    "constexpr",
                                                    "constinit",
                                                    "const_cast",
                                                    "continue",
                                                    "co_await",
                                                    "co_return",
                                                    "co_yield",
                                                    "decltype",
                                                    "default",
                                                    "delete",
                                                    "do",
                                                    "double",
                                                    "dynamic_cast",
                                                    "else",
                                                    "enum",
                                                    "explicit",
                                                    "export",
                                                    "extern",
                                                    "false",
                                                    "float",
                                                    "for",
                                                    "friend",
                                                    "goto",
                                                    "if",
                                                    "inline",
                                                    "int",
                                                    "long",
                                                    "mutable",
                                                    "namespace",
                                                    "new",
                                                    "noexcept",
                                                    "not",
                                                    "not_eq",
                                                    "nullptr",
                                                    "operator",
                                                    "or",
                                                    "or_eq",
                                                    "private",
                                                    "protected",
                                                    "public",
                                                    "reflexpr",
                                                    "register",
                                                    "reinterpret_cast",
                                                    "requires",
                                                    "return",
                                                    "short",
                                                    "signed",
                                                    "sizeof",
                                                    "static",
                                                    "static_assert",
                                                    "static_cast",
                                                    "struct",
                                                    "switch",
                                                    "synchronized",
                                                    "template",
                                                    "this",
                                                    "thread_local",
                                                    "throw",
                                                    "true",
                                                    "try",
                                                    "typedef",
                                                    "typeid",
                                                    "typename",
                                                    "union",
                                                    "unsigned",
                                                    "using",
                                                    "virtual",
                                                    "void",
                                                    "volatile",
                                                    "wchar_t",
                                                    "while",
                                                    "xor",
                                                    "xor_eq"};
    for (std::string_view kw : keywords) {
        if (token == kw) {
            return true;
        }
    }
    return false;
}

std::string escape_cpp_keyword(std::string_view token) {
    if (is_cpp_keyword(token)) {
        return std::string(token) + "_";
    }
    return std::string(token);
}

std::string sanitize_identifier(std::string_view str) {
    std::string result;
    result.reserve(str.size());
    for (const char character : str) {
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '_') {
            result.push_back(character);
        } else if (character == ' ' || character == '-' || character == '.') {
            result.push_back('_');
        }
    }
    if (!result.empty() && result.front() >= '0' && result.front() <= '9') {
        result = "_" + result;
    }
    return result;
}

}  // namespace fsm::frontend

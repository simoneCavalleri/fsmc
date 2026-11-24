/**
 * @file fuzz_expressions_and_guards.cpp
 * @brief libFuzzer harness exercising GuardExpressionParser, ExpressionAstNode, and LtlPropertyParser.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "fsm/frontend/directive/ltl_parser.hpp"
#include "fsm/ir/expression.hpp"
#include "fsm/ir/guard.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view input(reinterpret_cast<const char*>(data), size);

    auto guard_res = fsm::ir::GuardExpressionParser::parse(input);
    if (guard_res.ast) {
        (void)guard_res.ast->to_string();
    }

    auto expr_ast = fsm::ir::ExpressionAstNode::parse(input);
    std::unordered_map<std::string, double> env{{"x", 42.0}, {"y", 0.0}};
    (void)expr_ast.evaluate_numeric(env);
    (void)expr_ast.to_string();

    auto ltl_ast = fsm::frontend::directive::LtlPropertyParser::parse(input);
    if (ltl_ast.has_value()) {
        (void)ltl_ast->to_string();
    }
    return 0;
}

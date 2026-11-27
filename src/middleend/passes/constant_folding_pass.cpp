#include "fsm/middleend/passes/constant_folding_pass.hpp"

#include <algorithm>
#include <regex>
#include <unordered_map>
#include <unordered_set>

#include "fsm/ir/expression.hpp"
#include "fsm/ir/numeric_parse.hpp"

namespace fsm::middleend::passes {

using namespace fsm::ir;
using namespace fsm::diagnostic;

bool ConstantFoldingPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    bool modified = false;

    // 0. Fold constant arithmetic initializers on variables (e.g. "2 + 3 * 4" -> "14")
    for (auto& var : ir.variables) {
        if (var.initial_value.empty()) {
            continue;
        }
        auto ast = ExpressionAstNode::parse(var.initial_value);
        if (ast.kind != ExpressionKind::RawExpression && ast.kind != ExpressionKind::IntegerLiteral &&
            ast.kind != ExpressionKind::FloatLiteral && ast.kind != ExpressionKind::BooleanLiteral) {
            std::vector<std::string> refs;
            ast.collect_references(refs);
            if (refs.empty()) {
                const std::unordered_map<std::string, double> empty_env;
                if (auto val = ast.evaluate_numeric(empty_env); val.has_value()) {
                    const double d = *val;
                    if (static_cast<double>(static_cast<int64_t>(d)) == d) {
                        var.initial_value = std::to_string(static_cast<int64_t>(d));
                    } else {
                        var.initial_value = std::to_string(d);
                    }
                    modified = true;
                }
            }
        }
    }

    // 1. Fold guard conditions
    for (auto& t : ir.transitions) {
        if (!t.guard.has_value()) {
            continue;
        }

        std::string g = ir.resolve_guard_expression(*t.guard);
        // Trim whitespace
        while (!g.empty() && (g.front() == ' ' || g.front() == '\t'))
            g.erase(g.begin());
        while (!g.empty() && (g.back() == ' ' || g.back() == '\t'))
            g.pop_back();
        if (g == "true_") {
            g = "true";
        } else if (g == "false_") {
            g = "false";
        }

        // Constant evaluation patterns
        if (g == "true" || g == "1" || g == "1 == 1" || g == "0 == 0" || g == "!false" || g == "!0") {
            t.set_guard(std::nullopt);  // Unconditional
            modified = true;
        } else if (g == "false" || g == "0" || g == "1 == 0" || g == "0 == 1" || g == "!true" || g == "!1") {
            t.set_guard("false");
            modified = true;
        } else {
            // Check simple integer comparisons: e.g. "5 > 3", "2 < 1", "4 == 4", "3 != 3"
            static const std::regex num_cmp_re(R"(^\s*(-?\d+)\s*(==|!=|<=|>=|<|>)\s*(-?\d+)\s*$)");
            std::smatch m;
            if (std::regex_match(g, m, num_cmp_re)) {
                auto left_opt = parse_int64(m[1].str());
                auto right_opt = parse_int64(m[3].str());
                if (!left_opt.has_value() || !right_opt.has_value()) {
                    continue;
                }
                const int64_t left = *left_opt;
                const std::string op = m[2].str();
                const int64_t right = *right_opt;

                bool result = false;
                if (op == "==")
                    result = (left == right);
                else if (op == "!=")
                    result = (left != right);
                else if (op == "<")
                    result = (left < right);
                else if (op == "<=")
                    result = (left <= right);
                else if (op == ">")
                    result = (left > right);
                else if (op == ">=")
                    result = (left >= right);

                if (result) {
                    t.set_guard(std::nullopt);
                } else {
                    t.set_guard("false");
                }
                modified = true;
            } else {
                auto ast = ExpressionAstNode::parse(g);
                if (ast.kind != ExpressionKind::RawExpression) {
                    std::vector<std::string> refs;
                    ast.collect_references(refs);
                    if (refs.empty()) {
                        const std::unordered_map<std::string, double> empty_env;
                        if (auto val = ast.evaluate_boolean(empty_env); val.has_value()) {
                            if (*val) {
                                t.set_guard(std::nullopt);
                            } else {
                                t.set_guard("false");
                            }
                            modified = true;
                        }
                    }
                }
            }
        }
    }

    // 2. Prune transitions with guard == "false"
    std::size_t initial_trans = ir.transitions.size();
    ir.transitions.erase(
        std::remove_if(ir.transitions.begin(), ir.transitions.end(),
                       [](const TransitionEdge& t) { return t.guard.has_value() && *t.guard == "false"; }),
        ir.transitions.end());

    if (ir.transitions.size() < initial_trans) {
        modified = true;
        diag.report(Diagnostic::info("I_CONST_FOLD", "Pruned " + std::to_string(initial_trans - ir.transitions.size()) +
                                                         " statically false transition(s)."));
    }

    // 3. Clean up unreferenced actions
    std::unordered_set<std::string> referenced_actions;
    auto collect_action_sig = [&](const ActionSignature& act) {
        if (!act.name.empty()) {
            referenced_actions.insert(act.name);
        }
        for (const auto& inst : act.instructions) {
            if (const auto* call = std::get_if<ActionCallOp>(&inst.op);
                call != nullptr && !call->function_name.empty()) {
                referenced_actions.insert(call->function_name);
            }
        }
    };
    for (const auto& t : ir.transitions) {
        if (t.condition_action.has_value()) {
            collect_action_sig(*t.condition_action);
        }
        if (t.transition_action.has_value()) {
            collect_action_sig(*t.transition_action);
        }
    }
    for (const auto& s : ir.states) {
        for (const auto& act : s.entry_actions) {
            collect_action_sig(act);
        }
        for (const auto& act : s.exit_actions) {
            collect_action_sig(act);
        }
        if (s.do_activity.has_value() && !s.do_activity->empty()) {
            referenced_actions.insert(*s.do_activity);
        }
    }

    std::size_t initial_acts = ir.actions.size();
    ir.actions.erase(std::remove_if(ir.actions.begin(), ir.actions.end(),
                                    [&](const ActionModel& a) { return referenced_actions.count(a.name) == 0; }),
                     ir.actions.end());

    if (ir.actions.size() < initial_acts) {
        modified = true;
    }

    return modified;
}

}  // namespace fsm::middleend::passes

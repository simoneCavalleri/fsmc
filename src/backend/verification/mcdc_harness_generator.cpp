#include "fsm/backend/verification/mcdc_harness_generator.hpp"

#include <regex>
#include <sstream>
#include <unordered_map>

#include "fsm/ir/guard.hpp"

namespace fsm::backend::verification {

using ir::GuardExpressionParser;

std::string McdcHarnessGenerator::trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    return std::string(s);
}

std::unique_ptr<BoolExpr> McdcHarnessGenerator::parse_simple_expr(const std::string& expr_str,
                                                                  const std::vector<std::string>& atomic_conditions) {
    // Simple recursive parser or tokenizer
    // If atomic condition matches directly
    for (const auto& cond : atomic_conditions) {
        if (expr_str == cond) {
            return std::make_unique<VarExpr>(cond);
        }
    }

    // Check for top-level OR ('||')
    int paren_depth = 0;
    for (int i = static_cast<int>(expr_str.size()) - 1; i >= 1; --i) {
        if (expr_str[static_cast<size_t>(i)] == ')')
            paren_depth++;
        else if (expr_str[static_cast<size_t>(i)] == '(')
            paren_depth--;
        else if (paren_depth == 0 && expr_str[static_cast<size_t>(i)] == '|' &&
                 expr_str[static_cast<size_t>(i - 1)] == '|') {
            auto left = parse_simple_expr(trim(expr_str.substr(0, static_cast<size_t>(i - 1))), atomic_conditions);
            auto right = parse_simple_expr(trim(expr_str.substr(static_cast<size_t>(i + 1))), atomic_conditions);
            if (left && right) {
                return std::make_unique<BinaryExpr>(BinaryExpr::Or, std::move(left), std::move(right));
            }
        }
    }

    // Check for top-level AND ('&&')
    paren_depth = 0;
    for (int i = static_cast<int>(expr_str.size()) - 1; i >= 1; --i) {
        if (expr_str[static_cast<size_t>(i)] == ')')
            paren_depth++;
        else if (expr_str[static_cast<size_t>(i)] == '(')
            paren_depth--;
        else if (paren_depth == 0 && expr_str[static_cast<size_t>(i)] == '&' &&
                 expr_str[static_cast<size_t>(i - 1)] == '&') {
            auto left = parse_simple_expr(trim(expr_str.substr(0, static_cast<size_t>(i - 1))), atomic_conditions);
            auto right = parse_simple_expr(trim(expr_str.substr(static_cast<size_t>(i + 1))), atomic_conditions);
            if (left && right) {
                return std::make_unique<BinaryExpr>(BinaryExpr::And, std::move(left), std::move(right));
            }
        }
    }

    // Check for enclosing parentheses that match each other
    if (expr_str.size() >= 2 && expr_str.front() == '(' && expr_str.back() == ')') {
        int depth = 0;
        bool outer_matches = true;
        for (std::size_t i = 0; i + 1 < expr_str.size(); ++i) {
            if (expr_str[i] == '(') {
                ++depth;
            } else if (expr_str[i] == ')') {
                --depth;
                if (depth == 0) {
                    outer_matches = false;
                    break;
                }
            }
        }
        if (outer_matches && depth == 1) {
            return parse_simple_expr(trim(expr_str.substr(1, expr_str.size() - 2)), atomic_conditions);
        }
    }

    // Check for NOT ('!') when it applies to the entire expression (e.g. !(A && B) or !A)
    if (!expr_str.empty() && expr_str.front() == '!') {
        auto sub = parse_simple_expr(trim(expr_str.substr(1)), atomic_conditions);
        if (sub) {
            return std::make_unique<NotExpr>(std::move(sub));
        }
    }

    return std::make_unique<VarExpr>(expr_str);
}

std::vector<McdcIndependencePair> McdcHarnessGenerator::compute_mcdc_pairs(const BoolExpr& expr,
                                                                           const std::vector<std::string>& conditions) {
    std::vector<McdcIndependencePair> result;
    std::size_t n = std::min<std::size_t>(conditions.size(), 16);
    std::size_t total_combinations = 1ULL << n;

    // Build truth table
    std::vector<McdcTestVector> truth_table;
    truth_table.reserve(total_combinations);

    for (std::size_t mask = 0; mask < total_combinations; ++mask) {
        std::map<std::string, bool> env;
        for (std::size_t i = 0; i < n; ++i) {
            env[conditions[i]] = ((mask >> i) & 1ULL) != 0;
        }
        bool outcome = expr.evaluate(env);
        McdcTestVector tv;
        tv.condition_values = std::move(env);
        tv.decision_outcome = outcome;
        truth_table.push_back(std::move(tv));
    }

    // For each condition, find an independence pair (T1, T2) differing only in that condition
    for (std::size_t i = 0; i < n; ++i) {
        const auto& target_cond = conditions[i];
        bool found_pair = false;

        for (std::size_t mask1 = 0; mask1 < total_combinations; ++mask1) {
            if (((mask1 >> i) & 1ULL) == 0) {
                continue;  // mask1 must have condition == true
            }
            std::size_t mask2 = mask1 ^ (1ULL << i);  // mask2 has condition == false, all others identical

            const auto& tv_true = truth_table[mask1];
            const auto& tv_false = truth_table[mask2];

            if (tv_true.decision_outcome != tv_false.decision_outcome) {
                McdcIndependencePair pair;
                pair.condition_name = target_cond;
                pair.vector_true = tv_true;
                pair.vector_true.condition_tested = target_cond;
                pair.vector_false = tv_false;
                pair.vector_false.condition_tested = target_cond;
                result.push_back(std::move(pair));
                found_pair = true;
                break;
            }
        }

        if (!found_pair) {
            // Condition might be masked / non-essential in this formula
        }
    }

    return result;
}

std::string McdcHarnessGenerator::generate_gtest_harness(const FsmIr& ir, std::string_view fsm_header_include,
                                                         std::string_view target_namespace) {
    std::ostringstream out;
    out << "// ============================================================================\n";
    out << "// Automated MC/DC Safety Verification Test Harness\n";
    out << "// Target Model: " << ir.name << "\n";
    out << "// Generated by fsmc (Finite State Machine Compiler)\n";
    out << "// ============================================================================\n\n";

    out << "#include <gtest/gtest.h>\n";
    out << "#include <string_view>\n";
    if (!fsm_header_include.empty()) {
        out << "#include \"" << fsm_header_include << "\"\n";
    }
    out << "\n";

    std::string ns = !target_namespace.empty() ? std::string(target_namespace)
                                               : (!ir.package.empty() ? ir.package : std::string("fsm_generated"));
    std::unordered_map<std::string, const ir::PortDefinition*> in_port_map;
    for (const auto& p : ir.ports) {
        if (p.is_in()) {
            in_port_map[p.name] = &p;
        }
    }

    // Helper: synthesize a concrete C++ assignment on `in.<port>` to make an atomic condition true/false.
    auto synthesize_port_assignment = [&](const std::string& cond_raw, bool want_true, std::string& out_stmt) -> bool {
        const std::string c = trim(cond_raw);
        static const std::regex rel_re(
            R"(^(?:in\.)?([A-Za-z_][A-Za-z0-9_]*)\s*(==|!=|>=|<=|>|<)\s*(-?[0-9]+(?:\.[0-9]+)?)$)");
        static const std::regex bool_re(R"(^(!?)\s*(?:in\.)?([A-Za-z_][A-Za-z0-9_]*)$)");
        std::smatch m;
        if (std::regex_match(c, m, rel_re)) {
            const std::string pname = m[1].str();
            auto pit = in_port_map.find(pname);
            if (pit == in_port_map.end()) {
                return false;
            }
            const std::string op = m[2].str();
            const double val = std::stod(m[3].str());
            const bool is_fp = pit->second->type.is_floating_point();
            double chosen = val;
            if (op == ">") {
                chosen = want_true ? (val + 1.0) : val;
            } else if (op == ">=") {
                chosen = want_true ? val : (val - 1.0);
            } else if (op == "<") {
                chosen = want_true ? (val - 1.0) : val;
            } else if (op == "<=") {
                chosen = want_true ? val : (val + 1.0);
            } else if (op == "==") {
                chosen = want_true ? val : (val + 1.0);
            } else if (op == "!=") {
                chosen = want_true ? (val + 1.0) : val;
            }
            if (is_fp) {
                out_stmt = "in." + pname + " = " + std::to_string(chosen) + ";";
            } else {
                out_stmt = "in." + pname + " = " + std::to_string(static_cast<long long>(chosen)) + ";";
            }
            return true;
        }
        if (std::regex_match(c, m, bool_re)) {
            const bool negated = !m[1].str().empty();
            const std::string pname = m[2].str();
            auto pit = in_port_map.find(pname);
            if (pit == in_port_map.end() || !pit->second->type.is_boolean()) {
                return false;
            }
            const bool v = negated ? !want_true : want_true;
            out_stmt = "in." + pname + " = " + std::string(v ? "true" : "false") + ";";
            return true;
        }
        return false;
    };

    std::size_t test_idx = 0;
    for (const auto& t : ir.transitions) {
        if (!t.guard.has_value() || t.guard->empty()) {
            continue;
        }

        std::string raw_guard = *t.guard;
        std::string diag_guard = GuardExpressionParser::to_diagram_string(raw_guard, ir.guards);
        auto parsed_guard = GuardExpressionParser::parse(diag_guard);

        if (parsed_guard.atomic_guards.size() < 2) {
            continue;
        }

        auto expr_ast = parse_simple_expr(diag_guard, parsed_guard.atomic_guards);
        if (!expr_ast) {
            continue;
        }

        std::vector<std::string> conditions;
        expr_ast->collect_variables(conditions);
        if (conditions.empty()) {
            conditions = parsed_guard.atomic_guards;
        }
        auto pairs = compute_mcdc_pairs(*expr_ast, conditions);
        if (pairs.empty()) {
            continue;
        }

        auto sanitize_ident = [](std::string_view s) {
            std::string res;
            res.reserve(s.size());
            for (char c : s) {
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
                    res.push_back(c);
                } else {
                    res.push_back('_');
                }
            }
            return res.empty() ? std::string("State") : res;
        };

        ++test_idx;
        std::string test_name = "McdcCoverage_Trans_" + std::to_string(test_idx) + "_" + sanitize_ident(t.source) +
                                "_to_" + sanitize_ident(t.target);
        out << "/**\n";
        out << " * @brief MC/DC Test Suite for Transition: " << t.source << " -> " << t.target << "\n";
        out << " * Guard: [" << diag_guard << "]\n";
        out << " * Conditions (" << conditions.size() << "): ";
        for (size_t i = 0; i < conditions.size(); ++i) {
            if (i > 0)
                out << ", ";
            out << conditions[i];
        }
        out << "\n */\n";
        out << "TEST(McdcSafetyHarness, " << test_name << ") {\n";
        out << "    // Decision evaluation lambda\n";
        std::map<std::string, std::string> param_map;
        out << "    auto evaluate_decision = [](";
        for (size_t i = 0; i < conditions.size(); ++i) {
            if (i > 0)
                out << ", ";
            std::string pname = "c" + std::to_string(i);
            param_map[conditions[i]] = pname;
            out << "bool " << pname;
        }
        out << ") -> bool {\n";
        out << "        return " << expr_ast->to_cpp_expr(param_map) << ";\n";
        out << "    };\n\n";

        for (const auto& pair : pairs) {
            out << "    // Independence pair for condition: " << pair.condition_name << "\n";
            out << "    {\n";
            out << "        // Vector 1: " << pair.condition_name << " = true\n";
            out << "        bool d1 = evaluate_decision(";
            for (size_t i = 0; i < conditions.size(); ++i) {
                if (i > 0)
                    out << ", ";
                out << (pair.vector_true.condition_values.at(conditions[i]) ? "true" : "false");
            }
            out << ");\n";
            out << "        EXPECT_EQ(d1, " << (pair.vector_true.decision_outcome ? "true" : "false") << ");\n\n";

            out << "        // Vector 2: " << pair.condition_name << " = false (all other conditions held fixed)\n";
            out << "        bool d2 = evaluate_decision(";
            for (size_t i = 0; i < conditions.size(); ++i) {
                if (i > 0)
                    out << ", ";
                out << (pair.vector_false.condition_values.at(conditions[i]) ? "true" : "false");
            }
            out << ");\n";
            out << "        EXPECT_EQ(d2, " << (pair.vector_false.decision_outcome ? "true" : "false") << ");\n";
            out << "        EXPECT_NE(d1, d2) << \"Condition '" << pair.condition_name
                << "' failed independent outcome toggle.\";\n";
            out << "    }\n\n";
        }
        out << "}\n\n";

        // When the generated FSM header is linked, also synthesize concrete FSM dispatch MC/DC vectors
        // for transitions originating from the initial state whose conditions map to InPorts.
        if (!fsm_header_include.empty() && t.source == ir.initial_state && !t.event.empty() &&
            !std::holds_alternative<ir::TimeTrigger>(t.trigger) &&
            !std::holds_alternative<ir::ChangeTrigger>(t.trigger)) {
            bool all_ports_solvable = true;
            for (const auto& cond : conditions) {
                std::string dummy;
                if (!synthesize_port_assignment(cond, true, dummy)) {
                    all_ports_solvable = false;
                    break;
                }
            }
            if (all_ports_solvable) {
                out << "TEST(McdcSafetyHarness, " << test_name << "_FsmExecution) {\n";
                for (const auto& pair : pairs) {
                    out << "    // Concrete FSM MC/DC pair for condition: " << pair.condition_name << "\n";
                    for (int vec_idx = 0; vec_idx < 2; ++vec_idx) {
                        const auto& vec = (vec_idx == 0) ? pair.vector_true : pair.vector_false;
                        out << "    {\n";
                        out << "        " << ns << "::" << ir.name << " sm;\n";
                        out << "        " << ns << "::" << ir.name << "::in_ports_type in{};\n";
                        out << "        " << ns << "::" << ir.name << "::out_ports_type out{};\n";
                        for (const auto& cond : conditions) {
                            std::string assign_stmt;
                            synthesize_port_assignment(cond, vec.condition_values.at(cond), assign_stmt);
                            out << "        " << assign_stmt << "\n";
                        }
                        out << "        auto res = sm.dispatch(" << ns << "::" << t.event << "{}, in, out);\n";
                        if (vec.decision_outcome) {
                            out << "        EXPECT_TRUE(res.is_success());\n";
                            out << "        EXPECT_TRUE(sm.is_in<" << ns << "::" << t.target << ">());\n";
                        } else {
                            out << "        EXPECT_FALSE(res.is_success());\n";
                            out << "        EXPECT_TRUE(sm.is_in<" << ns << "::" << t.source << ">());\n";
                        }
                        out << "    }\n";
                    }
                }
                out << "}\n\n";
            }
        }
    }

    if (test_idx == 0) {
        out << "TEST(McdcSafetyHarness, NoCompositeGuards) {\n";
        out << "    // Model contains no composite boolean guard expressions requiring MC/DC.\n";
        out << "    SUCCEED();\n";
        out << "}\n";
    }

    return out.str();
}

}  // namespace fsm::backend::verification

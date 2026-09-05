#pragma once

#include <algorithm>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::passes {

/**
 * @brief Middle-End Optimization Pass: Constant Folding and Dead Transition Elimination.
 *
 * Statically evaluates constant expressions in guard conditions, folds tautologies
 * (e.g. "1 == 1" -> true), eliminates contradiction transitions ("0 == 1" -> false),
 * and prunes unreachable actions.
 */
class ConstantFoldingPass {
  public:
    [[nodiscard]] static std::string name() { return "ConstantFolding"; }
    [[nodiscard]] static std::string description() {
        return "Folds constant guard expressions, eliminates dead transitions, and cleans unused actions";
    }

    bool run(FsmIr& ir, DiagnosticEngine& diag) {
        bool modified = false;

        // 1. Fold guard conditions
        for (auto& t : ir.transitions) {
            if (!t.guard.has_value()) {
                continue;
            }

            std::string g = *t.guard;
            // Trim whitespace
            while (!g.empty() && (g.front() == ' ' || g.front() == '\t'))
                g.erase(g.begin());
            while (!g.empty() && (g.back() == ' ' || g.back() == '\t'))
                g.pop_back();

            // Constant evaluation patterns
            if (g == "true" || g == "1" || g == "1 == 1" || g == "0 == 0" || g == "!false" || g == "!0") {
                t.guard = std::nullopt;  // Unconditional
                modified = true;
            } else if (g == "false" || g == "0" || g == "1 == 0" || g == "0 == 1" || g == "!true" || g == "!1") {
                t.guard = "false";
                modified = true;
            } else {
                // Check simple integer comparisons: e.g. "5 > 3", "2 < 1", "4 == 4", "3 != 3"
                std::regex num_cmp_re(R"(^\s*(-?\d+)\s*(==|!=|<=|>=|<|>)\s*(-?\d+)\s*$)");
                std::smatch m;
                if (std::regex_match(g, m, num_cmp_re)) {
                    long long left = std::stoll(m[1].str());
                    std::string op = m[2].str();
                    long long right = std::stoll(m[3].str());

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
                        t.guard = std::nullopt;
                    } else {
                        t.guard = "false";
                    }
                    modified = true;
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
            diag.report(Diagnostic::info("I_CONST_FOLD", "Pruned " +
                                                             std::to_string(initial_trans - ir.transitions.size()) +
                                                             " statically false transition(s)."));
        }

        // 3. Clean up unreferenced actions
        std::unordered_set<std::string> referenced_actions;
        for (const auto& t : ir.transitions) {
            if (t.action.has_value() && !t.action->empty()) {
                referenced_actions.insert(*t.action);
            }
            if (t.action_sig.has_value() && !t.action_sig->name.empty()) {
                referenced_actions.insert(t.action_sig->name);
            }
        }
        for (const auto& s : ir.states) {
            for (const auto& act : s.entry_actions) {
                referenced_actions.insert(act.name);
            }
            for (const auto& act : s.exit_actions) {
                referenced_actions.insert(act.name);
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
};

}  // namespace fsm::middleend::passes

namespace fsm::middleend {
using passes::ConstantFoldingPass;
}  // namespace fsm::middleend

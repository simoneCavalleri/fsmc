#pragma once

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/efsm_interval_analysis.hpp"

namespace fsm::codegen {

/**
 * @brief In-process Middle-End Analysis Pass: Evaluates guard mutual exclusivity and satisfiability.
 *
 * 1. Groups transitions sharing the same (source, event) trigger.
 * 2. Parses guard expressions to extract numeric/interval constraints on variables.
 * 3. Detects overlapping guards (non-deterministic ambiguity, warning W0301).
 * 4. Detects unsatisfiable / dead guards (warning W0302).
 */
class GuardSatisfiabilityPass {
  public:
    [[nodiscard]] static std::string name() { return "GuardSatisfiabilityAnalysis"; }
    [[nodiscard]] static std::string description() {
        return "Analyzes guard expressions for satisfiability and mutual exclusivity without external solver dependencies";
    }

    bool run(FsmIr& ir, DiagnosticEngine& diag) {
        // Group transitions by (source, event)
        std::map<std::pair<std::string, std::string>, std::vector<const TransitionEdge*>> groups;
        for (const auto& t : ir.transitions) {
            std::string src = t.source_id.empty() ? t.source : t.source_id;
            groups[{src, t.event}].push_back(&t);
        }

        for (const auto& [key, trans_list] : groups) {
            const auto& [src, evt] = key;

            // 1. Evaluate single-guard satisfiability (W0302)
            for (const auto* t : trans_list) {
                if (!t->guard.has_value() || t->guard->empty()) {
                    continue;
                }
                const std::string& g_str = *t->guard;
                auto constraints = extract_guard_constraints(g_str);
                for (const auto& [var, ival] : constraints) {
                    if (ival.is_empty()) {
                        diag.report(Diagnostic::warning(
                            "W0302",
                            "Dead guard on transition from '" + src + "' to '" + t->target + "' on event '" +
                                evt + "': guard '" + g_str + "' is unsatisfiable."));
                        break;
                    }
                }
            }

            // 2. Evaluate mutual exclusivity between pairs of guarded transitions (W0301)
            if (trans_list.size() < 2) {
                continue;
            }

            for (std::size_t i = 0; i < trans_list.size(); ++i) {
                const auto* t1 = trans_list[i];
                if (!t1->guard.has_value() || t1->guard->empty()) {
                    continue;
                }

                auto c1 = extract_guard_constraints(*t1->guard);

                for (std::size_t j = i + 1; j < trans_list.size(); ++j) {
                    const auto* t2 = trans_list[j];
                    if (!t2->guard.has_value() || t2->guard->empty()) {
                        continue;
                    }

                    // Only warn if priorities are identical (if priorities differ, execution order is deterministic)
                    if (t1->priority != t2->priority) {
                        continue;
                    }

                    auto c2 = extract_guard_constraints(*t2->guard);

                    // If identical guard strings -> always overlapping
                    if (*t1->guard == *t2->guard) {
                        diag.report(Diagnostic::warning(
                            "W0301",
                            "Potentially overlapping guards on transitions from '" + src + "' on event '" +
                                evt + "': guards ['" + *t1->guard + "'] and ['" + *t2->guard +
                                "'] may be simultaneously satisfiable."));
                        continue;
                    }

                    // Check for shared constrained variables
                    bool found_common_var = false;
                    bool provably_mutually_exclusive = false;

                    for (const auto& [var1, ival1] : c1) {
                        auto it2 = c2.find(var1);
                        if (it2 != c2.end()) {
                            found_common_var = true;
                            const auto& ival2 = it2->second;
                            auto inter = ival1.intersect_with(ival2);
                            if (inter.is_empty()) {
                                provably_mutually_exclusive = true;
                                break;
                            }
                        }
                    }

                    if (found_common_var && !provably_mutually_exclusive) {
                        diag.report(Diagnostic::warning(
                            "W0301",
                            "Potentially overlapping guards on transitions from '" + src + "' on event '" +
                                evt + "': guards ['" + *t1->guard + "'] and ['" + *t2->guard +
                                "'] may be simultaneously satisfiable."));
                    }
                }
            }
        }

        return true;
    }

  private:
    static std::string clean_number(std::string s) {
        while (!s.empty() && (s.back() == 'f' || s.back() == 'F' || s.back() == 'u' || s.back() == 'U' ||
                              s.back() == 'l' || s.back() == 'L')) {
            s.pop_back();
        }
        return s;
    }

    static std::unordered_map<std::string, Interval> extract_guard_constraints(const std::string& guard_str) {
        std::unordered_map<std::string, Interval> result;

        // Split on && or and
        std::regex and_split(R"(\s*(?:&&|\band\b)\s*)");
        std::sregex_token_iterator iter(guard_str.begin(), guard_str.end(), and_split, -1);
        std::sregex_token_iterator end;

        std::regex cmp_re(R"((?:(?:in|reg|out|cmd|event|payload)\.)?([a-zA-Z_]\w*)\s*(==|!=|>=|<=|>|<)\s*([+-]?\d+(?:\.\d+)?[fFuUlL]*))");
        std::regex bool_eq_re(R"((?:(?:in|reg|out|cmd|event|payload)\.)?([a-zA-Z_]\w*)\s*(==|!=)\s*(true|false))");

        for (; iter != end; ++iter) {
            std::string clause = *iter;
            std::smatch match;

            if (std::regex_search(clause, match, cmp_re)) {
                std::string var = match[1].str();
                std::string op = match[2].str();
                double val = 0.0;
                try {
                    val = std::stod(clean_number(match[3].str()));
                } catch (...) {
                    continue;
                }

                Interval clause_interval;
                if (op == "==") {
                    clause_interval = Interval(val, val);
                } else if (op == ">=") {
                    clause_interval = Interval(val, std::numeric_limits<double>::infinity());
                } else if (op == ">") {
                    clause_interval = Interval(val + 1e-6, std::numeric_limits<double>::infinity());
                } else if (op == "<=") {
                    clause_interval = Interval(-std::numeric_limits<double>::infinity(), val);
                } else if (op == "<") {
                    clause_interval = Interval(-std::numeric_limits<double>::infinity(), val - 1e-6);
                }

                if (result.count(var)) {
                    result[var] = result[var].intersect_with(clause_interval);
                } else {
                    result[var] = clause_interval;
                }
            } else if (std::regex_search(clause, match, bool_eq_re)) {
                std::string var = match[1].str();
                std::string op = match[2].str();
                std::string val_str = match[3].str();
                bool is_true = (val_str == "true");
                if (op == "!=") {
                    is_true = !is_true;
                }
                Interval bool_interval = is_true ? Interval(1.0, 1.0) : Interval(0.0, 0.0);
                if (result.count(var)) {
                    result[var] = result[var].intersect_with(bool_interval);
                } else {
                    result[var] = bool_interval;
                }
            }
        }

        return result;
    }
};

}  // namespace fsm::codegen

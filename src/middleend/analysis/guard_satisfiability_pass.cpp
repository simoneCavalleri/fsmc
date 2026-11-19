#include "fsm/middleend/analysis/guard_satisfiability_pass.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <regex>
#include <utility>
#include <vector>

#include "fsm/ir/numeric_parse.hpp"

namespace fsm::middleend::analysis {

using namespace fsm::ir;
using namespace fsm::diagnostic;

namespace {

struct VarConstraint {
    Interval interval;
    std::vector<double> excluded_values;

    [[nodiscard]] bool is_excluded(double v) const noexcept {
        for (double ex : excluded_values) {
            if (std::abs(ex - v) < 1e-9) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool is_empty() const noexcept {
        if (interval.is_empty()) {
            return true;
        }
        if (std::isfinite(interval.lo) && std::isfinite(interval.hi) && std::abs(interval.lo - interval.hi) < 1e-9 &&
            is_excluded(interval.lo)) {
            return true;
        }
        return false;
    }

    [[nodiscard]] bool intersects(const VarConstraint& other) const noexcept {
        Interval inter = interval.intersect_with(other.interval);
        if (inter.is_empty()) {
            return false;
        }
        if (std::isfinite(inter.lo) && std::isfinite(inter.hi) && std::abs(inter.lo - inter.hi) < 1e-9) {
            if (is_excluded(inter.lo) || other.is_excluded(inter.lo)) {
                return false;
            }
        }
        return true;
    }
};

using BranchConstraints = std::map<std::string, VarConstraint>;

std::string_view trim_sv(std::string_view sv) noexcept {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
        sv.remove_suffix(1);
    }
    return sv;
}

std::string_view strip_outer_parens(std::string_view sv) noexcept {
    sv = trim_sv(sv);
    while (sv.size() >= 2 && sv.front() == '(' && sv.back() == ')') {
        int depth = 0;
        bool wraps_all = true;
        for (size_t i = 0; i < sv.size(); ++i) {
            if (sv[i] == '(') {
                ++depth;
            } else if (sv[i] == ')') {
                --depth;
                if (depth == 0 && i + 1 < sv.size()) {
                    wraps_all = false;
                    break;
                }
            }
        }
        if (!wraps_all) {
            break;
        }
        sv = trim_sv(sv.substr(1, sv.size() - 2));
    }
    return sv;
}

bool is_word_char(char c) noexcept {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::vector<std::string> split_top_level_or(std::string_view expr) {
    expr = strip_outer_parens(expr);
    std::vector<std::string> branches;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < expr.size(); ++i) {
        if (expr[i] == '(') {
            ++depth;
        } else if (expr[i] == ')') {
            if (depth > 0) {
                --depth;
            }
        } else if (depth == 0) {
            if (i + 1 < expr.size() && expr[i] == '|' && expr[i + 1] == '|') {
                branches.emplace_back(trim_sv(expr.substr(start, i - start)));
                i += 1;
                start = i + 1;
            } else if (i + 2 <= expr.size() && expr.substr(i, 2) == "or" && (i == 0 || !is_word_char(expr[i - 1])) &&
                       (i + 2 == expr.size() || !is_word_char(expr[i + 2]))) {
                branches.emplace_back(trim_sv(expr.substr(start, i - start)));
                i += 1;
                start = i + 1;
            }
        }
    }
    branches.emplace_back(trim_sv(expr.substr(start)));
    return branches;
}

bool contains_or_operator(std::string_view clause) noexcept {
    if (clause.find("||") != std::string_view::npos) {
        return true;
    }
    for (size_t i = 0; i + 2 <= clause.size(); ++i) {
        if (clause.substr(i, 2) == "or" && (i == 0 || !is_word_char(clause[i - 1])) &&
            (i + 2 == clause.size() || !is_word_char(clause[i + 2]))) {
            return true;
        }
    }
    return false;
}

std::string clean_number_local(std::string s) {
    while (!s.empty() && (s.back() == 'f' || s.back() == 'F' || s.back() == 'u' || s.back() == 'U' || s.back() == 'l' ||
                          s.back() == 'L')) {
        s.pop_back();
    }
    return s;
}

BranchConstraints extract_branch_constraints(const std::string& branch_str) {
    BranchConstraints result;

    static const std::regex and_split(R"(\s*(?:&&|\band\b)\s*)");
    static const std::regex cmp_re(
        R"((?:(?:in|reg|out|cmd|event|payload)\.)?([a-zA-Z_]\w*)\s*(==|!=|>=|<=|>|<)\s*([+-]?\d+(?:\.\d+)?[fFuUlL]*))");
    static const std::regex bool_eq_re(
        R"((?:(?:in|reg|out|cmd|event|payload)\.)?([a-zA-Z_]\w*)\s*(==|!=)\s*(true|false))");

    std::sregex_token_iterator iter(branch_str.begin(), branch_str.end(), and_split, -1);
    std::sregex_token_iterator end;

    for (; iter != end; ++iter) {
        std::string clause = *iter;
        // Skip nested disjunctions inside conjunctions (conservative: cannot flatten without full DNF expansion)
        if (contains_or_operator(clause)) {
            continue;
        }
        std::smatch match;
        if (std::regex_search(clause, match, cmp_re)) {
            std::string var = match[1].str();
            std::string op = match[2].str();
            auto val_opt = parse_double(clean_number_local(match[3].str()));
            if (!val_opt.has_value()) {
                continue;
            }
            const double val = *val_opt;

            if (op == "!=") {
                result[var].excluded_values.push_back(val);
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
                result[var].interval = result[var].interval.intersect_with(clause_interval);
            } else {
                result[var].interval = clause_interval;
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
                result[var].interval = result[var].interval.intersect_with(bool_interval);
            } else {
                result[var].interval = bool_interval;
            }
        }
    }

    return result;
}

std::vector<BranchConstraints> extract_dnf_constraints(const std::string& guard_str) {
    auto branches = split_top_level_or(guard_str);
    std::vector<BranchConstraints> dnf;
    dnf.reserve(branches.size());
    for (const auto& b : branches) {
        dnf.push_back(extract_branch_constraints(b));
    }
    return dnf;
}

bool is_branch_unsat(const BranchConstraints& bc) noexcept {
    for (const auto& [_, vc] : bc) {
        if (vc.is_empty()) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::string GuardSatisfiabilityPass::clean_number(std::string s) {
    return clean_number_local(std::move(s));
}

std::unordered_map<std::string, Interval> GuardSatisfiabilityPass::extract_guard_constraints(
    const std::string& guard_str) {
    std::unordered_map<std::string, Interval> result;
    auto dnf = extract_dnf_constraints(guard_str);
    if (dnf.size() == 1) {
        for (const auto& [var, vc] : dnf.front()) {
            result[var] = vc.interval;
        }
    }
    return result;
}

bool GuardSatisfiabilityPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    auto get_trigger_key = [](const TransitionEdge& edge) -> std::string {
        if (!edge.event.empty()) {
            return edge.event;
        }
        std::string tn = edge.get_trigger_name();
        if (!tn.empty()) {
            return tn;
        }
        return "anonymous";
    };

    // Group transitions by (source, trigger/event)
    std::map<std::pair<std::string, std::string>, std::vector<const TransitionEdge*>> groups;
    for (const auto& t : ir.transitions) {
        std::string src = t.source;
        std::string evt = get_trigger_key(t);
        groups[{src, evt}].push_back(&t);
    }

    for (const auto& [key, trans_list] : groups) {
        const auto& [src, evt] = key;

        // 1. Evaluate single-guard satisfiability (W0302): a DNF guard is dead iff ALL branches are unsatisfiable
        for (const auto* t : trans_list) {
            if (!t->guard.has_value() || t->guard->empty()) {
                continue;
            }
            const std::string g_str = ir.resolve_guard_expression(*t->guard);
            auto dnf = extract_dnf_constraints(g_str);
            bool all_branches_unsat = !dnf.empty();
            for (const auto& branch : dnf) {
                if (!is_branch_unsat(branch)) {
                    all_branches_unsat = false;
                    break;
                }
            }
            if (all_branches_unsat) {
                diag.report(Diagnostic::warning("W0302", "Dead guard on transition from '" + src + "' to '" +
                                                             t->target + "' on event '" + evt + "': guard '" + g_str +
                                                             "' is unsatisfiable."));
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

            const std::string g1 = ir.resolve_guard_expression(*t1->guard);
            auto dnf1 = extract_dnf_constraints(g1);

            for (std::size_t j = i + 1; j < trans_list.size(); ++j) {
                const auto* t2 = trans_list[j];
                if (!t2->guard.has_value() || t2->guard->empty()) {
                    continue;
                }

                // Only warn if priorities are identical (if priorities differ, execution order is deterministic)
                if (t1->priority != t2->priority) {
                    continue;
                }

                const std::string g2 = ir.resolve_guard_expression(*t2->guard);

                // If identical guard strings -> always overlapping
                if (g1 == g2) {
                    diag.report(Diagnostic::warning(
                        "W0301", "Potentially overlapping guards on transitions from '" + src + "' on event '" + evt +
                                     "': guards ['" + g1 + "'] and ['" + g2 + "'] may be simultaneously satisfiable."));
                    continue;
                }

                auto dnf2 = extract_dnf_constraints(g2);

                // Two DNF guards overlap iff there exists a satisfiable pair of branches (b1, b2)
                // that share at least one constrained variable and are not mutually exclusive.
                bool any_pair_overlaps = false;
                for (const auto& b1 : dnf1) {
                    if (is_branch_unsat(b1)) {
                        continue;
                    }
                    for (const auto& b2 : dnf2) {
                        if (is_branch_unsat(b2)) {
                            continue;
                        }
                        bool found_common_var = false;
                        bool pair_mutually_exclusive = false;
                        for (const auto& [var1, vc1] : b1) {
                            auto it2 = b2.find(var1);
                            if (it2 != b2.end()) {
                                found_common_var = true;
                                if (!vc1.intersects(it2->second)) {
                                    pair_mutually_exclusive = true;
                                    break;
                                }
                            }
                        }
                        if (found_common_var && !pair_mutually_exclusive) {
                            any_pair_overlaps = true;
                            break;
                        }
                    }
                    if (any_pair_overlaps) {
                        break;
                    }
                }

                if (any_pair_overlaps) {
                    diag.report(Diagnostic::warning("W0301", "Potentially overlapping guards on transitions from '" +
                                                                 src + "' on event '" + evt + "': guards ['" +
                                                                 *t1->guard + "'] and ['" + *t2->guard +
                                                                 "'] may be simultaneously satisfiable."));
                }
            }
        }
    }

    return true;
}

}  // namespace fsm::middleend::analysis

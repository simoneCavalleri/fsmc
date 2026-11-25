#include <algorithm>
#include <unordered_set>

#include "fsm/middleend/analysis/model_checker.hpp"
#include "model_checker_internal.hpp"

namespace fsm::middleend::analysis {

using namespace fsm::ir;
using mc_detail::eval_atom_predicate;

std::unordered_set<std::string> ModelChecker::pre_exists(const std::unordered_set<std::string>& targets) const {
    std::unordered_set<std::string> result;
    for (const auto& [src, edges] : adj_) {
        if (edges.empty()) {
            // Stuttering terminal semantics (consistent with pre_all): terminal states self-stutter
            if (targets.count(src) > 0) {
                result.insert(src);
            }
            continue;
        }
        for (const auto& edge : edges) {
            if (targets.count(edge.target) > 0) {
                result.insert(src);
                break;
            }
        }
    }
    return result;
}

std::unordered_set<std::string> ModelChecker::pre_all(const std::unordered_set<std::string>& targets) const {
    std::unordered_set<std::string> result;
    for (const auto& [src, edges] : adj_) {
        if (edges.empty()) {
            if (targets.count(src) > 0) {
                result.insert(src);
            }
            continue;
        }
        bool all_match = true;
        for (const auto& edge : edges) {
            if (targets.count(edge.target) == 0) {
                all_match = false;
                break;
            }
        }
        if (all_match) {
            result.insert(src);
        }
    }
    return result;
}

std::unordered_set<std::string> ModelChecker::compute_sat(const PropertyAstNode& node) const {
    std::unordered_set<std::string> all_states;
    for (const auto& s : ir_.states)
        all_states.insert(s.name);
    for (const auto& t : ir_.transitions) {
        if (!t.source.empty())
            all_states.insert(t.source);
        if (!t.target.empty())
            all_states.insert(t.target);
    }
    if (!root_state_.empty())
        all_states.insert(root_state_);

    switch (node.op) {
        case TemporalOp::Atom: {
            std::unordered_set<std::string> sat;
            for (const auto& s : all_states) {
                if (eval_predicate(node, s)) {
                    sat.insert(s);
                }
            }
            return sat;
        }
        case TemporalOp::Not: {
            std::unordered_set<std::string> sat;
            if (node.children.empty()) {
                for (const auto& s : all_states) {
                    if (!eval_atom_predicate(node.atom, s, ir_, state_intervals_)) {
                        sat.insert(s);
                    }
                }
                return sat;
            }
            auto child_sat = compute_sat(node.children[0]);
            for (const auto& s : all_states) {
                if (child_sat.count(s) == 0) {
                    sat.insert(s);
                }
            }
            return sat;
        }
        case TemporalOp::And: {
            if (node.children.empty())
                return all_states;
            auto sat = compute_sat(node.children[0]);
            for (std::size_t i = 1; i < node.children.size(); ++i) {
                auto sub_sat = compute_sat(node.children[i]);
                std::unordered_set<std::string> inter;
                for (const auto& s : sat) {
                    if (sub_sat.count(s) > 0)
                        inter.insert(s);
                }
                sat = std::move(inter);
            }
            return sat;
        }
        case TemporalOp::Or: {
            std::unordered_set<std::string> sat;
            for (const auto& child : node.children) {
                auto sub_sat = compute_sat(child);
                sat.insert(sub_sat.begin(), sub_sat.end());
            }
            return sat;
        }
        case TemporalOp::Implies: {
            if (node.children.size() < 2)
                return all_states;
            auto left_sat = compute_sat(node.children[0]);
            auto right_sat = compute_sat(node.children[1]);
            std::unordered_set<std::string> sat;
            for (const auto& s : all_states) {
                if (left_sat.count(s) == 0 || right_sat.count(s) > 0) {
                    sat.insert(s);
                }
            }
            return sat;
        }
        case TemporalOp::Equivalent: {
            if (node.children.size() < 2)
                return all_states;
            auto left_sat = compute_sat(node.children[0]);
            auto right_sat = compute_sat(node.children[1]);
            std::unordered_set<std::string> sat;
            for (const auto& s : all_states) {
                bool in_left = (left_sat.count(s) > 0);
                bool in_right = (right_sat.count(s) > 0);
                if (in_left == in_right) {
                    sat.insert(s);
                }
            }
            return sat;
        }
        case TemporalOp::EX: {
            if (node.children.empty())
                return {};
            auto target_sat = compute_sat(node.children[0]);
            return pre_exists(target_sat);
        }
        case TemporalOp::AX: {
            if (node.children.empty())
                return {};
            auto target_sat = compute_sat(node.children[0]);
            return pre_all(target_sat);
        }
        case TemporalOp::EF: {
            if (node.children.empty())
                return {};
            auto sat = compute_sat(node.children[0]);
            bool changed = true;
            while (changed) {
                changed = false;
                auto pre = pre_exists(sat);
                for (const auto& s : pre) {
                    if (sat.insert(s).second) {
                        changed = true;
                    }
                }
            }
            return sat;
        }
        case TemporalOp::EG: {
            if (node.children.empty())
                return {};
            auto p_sat = compute_sat(node.children[0]);
            auto sat = p_sat;
            bool changed = true;
            while (changed) {
                changed = false;
                auto pre = pre_exists(sat);
                std::unordered_set<std::string> next_sat;
                for (const auto& s : sat) {
                    if (pre.count(s) > 0) {
                        next_sat.insert(s);
                    }
                }
                if (next_sat != sat) {
                    sat = std::move(next_sat);
                    changed = true;
                }
            }
            return sat;
        }
        case TemporalOp::AG: {
            if (node.children.empty())
                return all_states;
            auto p_sat = compute_sat(node.children[0]);
            auto sat = p_sat;
            bool changed = true;
            while (changed) {
                changed = false;
                auto pre = pre_all(sat);
                std::unordered_set<std::string> next_sat;
                for (const auto& s : sat) {
                    if (pre.count(s) > 0) {
                        next_sat.insert(s);
                    }
                }
                if (next_sat != sat) {
                    sat = std::move(next_sat);
                    changed = true;
                }
            }
            return sat;
        }
        case TemporalOp::AF: {
            if (node.children.empty())
                return {};
            auto p_sat = compute_sat(node.children[0]);
            auto sat = p_sat;
            bool changed = true;
            while (changed) {
                changed = false;
                auto pre = pre_all(sat);
                for (const auto& s : pre) {
                    if (sat.insert(s).second) {
                        changed = true;
                    }
                }
            }
            return sat;
        }
        case TemporalOp::EU: {
            if (node.children.size() < 2)
                return {};
            auto p_sat = compute_sat(node.children[0]);
            auto q_sat = compute_sat(node.children[1]);
            auto sat = q_sat;
            bool changed = true;
            while (changed) {
                changed = false;
                auto pre = pre_exists(sat);
                for (const auto& s : pre) {
                    if (p_sat.count(s) > 0 && sat.insert(s).second) {
                        changed = true;
                    }
                }
            }
            return sat;
        }
        case TemporalOp::AU: {
            if (node.children.size() < 2)
                return {};
            auto p_sat = compute_sat(node.children[0]);
            auto q_sat = compute_sat(node.children[1]);
            auto sat = q_sat;
            bool changed = true;
            while (changed) {
                changed = false;
                auto pre = pre_all(sat);
                for (const auto& s : pre) {
                    if (p_sat.count(s) > 0 && sat.insert(s).second) {
                        changed = true;
                    }
                }
            }
            return sat;
        }
        default:
            return {};
    }
}

ModelCheckResult ModelChecker::check_ctl(const FormalProperty& prop, const PropertyAstNode& node) {
    auto sat = compute_sat(node);
    if (root_state_.empty()) {
        return {true, prop.name, prop.raw_formula, prop.kind, "", {}};
    }

    if (sat.count(root_state_) > 0) {
        return {true, prop.name, prop.raw_formula, prop.kind, "", {}};
    }

    // Property is violated at root_state_
    std::string violation =
        "CTL specification '" + prop.raw_formula + "' does not hold at initial state '" + root_state_ + "'.";
    std::vector<CounterexampleStep> trace;

    // For AG(p), reconstruct path to state not in sat(p)
    if (node.op == TemporalOp::AG && !node.children.empty()) {
        auto p_sat = compute_sat(node.children[0]);
        for (const auto& s : reachable_states_) {
            if (p_sat.count(s) == 0) {
                trace =
                    reconstruct_trace(s, "State '" + s + "' violates invariant '" + node.children[0].to_string() + "'");
                break;
            }
        }
    } else if (node.op == TemporalOp::AX && !node.children.empty()) {
        auto p_sat = compute_sat(node.children[0]);
        auto it = adj_.find(root_state_);
        if (it != adj_.end()) {
            for (const auto& edge : it->second) {
                if (p_sat.count(edge.target) == 0) {
                    trace.push_back({0, root_state_, edge.event, edge.guard, "Initial state"});
                    trace.push_back(
                        {1, edge.target, "", "", "Successor state violates '" + node.children[0].to_string() + "'"});
                    break;
                }
            }
        }
    } else {
        trace.push_back({0, root_state_, "", "", "Initial state does not satisfy property"});
    }

    return {false, prop.name, prop.raw_formula, prop.kind, violation, trace};
}

}  // namespace fsm::middleend::analysis

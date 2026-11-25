#include <algorithm>
#include <functional>
#include <queue>
#include <stack>
#include <unordered_map>
#include <unordered_set>

#include "fsm/ir/expression.hpp"
#include "fsm/middleend/analysis/efsm_interval_analysis.hpp"
#include "fsm/middleend/analysis/model_checker.hpp"
#include "model_checker_internal.hpp"

namespace fsm::middleend::analysis {

using namespace fsm::ir;

namespace mc_detail {

std::string trim_str(std::string_view s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])) != 0)
        start++;
    if (start == s.size())
        return "";
    size_t end = s.size() - 1;
    while (end > start && std::isspace(static_cast<unsigned char>(s[end])) != 0)
        end--;
    return std::string(s.substr(start, end - start + 1));
}

bool eval_atom_predicate(
    std::string_view raw_atom, const std::string& state, const FsmIr& ir,
    const std::unordered_map<std::string, std::unordered_map<std::string, Interval>>& state_intervals) {
    std::string atom = trim_str(raw_atom);
    if (atom.empty())
        return true;

    // Negation prefix: !expr
    if (atom.front() == '!') {
        std::string inner = trim_str(atom.substr(1));
        if (inner.size() >= 2 && inner.front() == '(' && inner.back() == ')') {
            inner = trim_str(inner.substr(1, inner.size() - 2));
        }
        auto find_rel_op = [](const std::string& s) -> std::pair<size_t, std::string> {
            for (const auto& op : {"<=", ">=", "!=", "==", "<", ">"}) {
                size_t p = s.find(op);
                if (p != std::string::npos) {
                    return {p, op};
                }
            }
            return {std::string::npos, ""};
        };
        auto [pos, op] = find_rel_op(inner);
        if (pos != std::string::npos) {
            std::string lhs = inner.substr(0, pos);
            std::string rhs = inner.substr(pos + op.size());
            std::string inv_op;
            if (op == "==")
                inv_op = "!=";
            else if (op == "!=")
                inv_op = "==";
            else if (op == "<")
                inv_op = ">=";
            else if (op == "<=")
                inv_op = ">";
            else if (op == ">")
                inv_op = "<=";
            else if (op == ">=")
                inv_op = "<";
            return eval_atom_predicate(lhs + inv_op + rhs, state, ir, state_intervals);
        }
        return !eval_atom_predicate(inner, state, ir, state_intervals);
    }

    if (atom == "true" || atom == "1")
        return true;
    if (atom == "false" || atom == "0")
        return false;

    // Check direct state match: Atom == State
    if (atom == state)
        return true;

    // Check state comparisons: state == Name, state != Name, state = Name
    if (atom.rfind("state", 0) == 0) {
        std::string rest = trim_str(atom.substr(5));
        if (rest.rfind("==", 0) == 0) {
            std::string target = trim_str(rest.substr(2));
            return state == target;
        }
        if (rest.rfind("!=", 0) == 0) {
            std::string target = trim_str(rest.substr(2));
            return state != target;
        }
        if (rest.rfind("=", 0) == 0) {
            std::string target = trim_str(rest.substr(1));
            return state == target;
        }
    }

    // Check relational comparison operator on variables/ports: <=, >=, !=, ==, <, >
    const std::vector<std::string> ops = {"<=", ">=", "!=", "==", "<", ">"};
    for (const auto& op : ops) {
        size_t pos = atom.find(op);
        if (pos != std::string::npos) {
            std::string lhs_str = trim_str(atom.substr(0, pos));
            std::string rhs_str = trim_str(atom.substr(pos + op.size()));

            if (lhs_str == "state") {
                if (op == "==")
                    return state == rhs_str;
                if (op == "!=")
                    return state != rhs_str;
            }

            // 1. Check computed state_intervals for this state first
            auto it_state = state_intervals.find(state);
            if (it_state != state_intervals.end()) {
                auto it_var = it_state->second.find(lhs_str);
                if (it_var != it_state->second.end()) {
                    double rhs_num = 0.0;
                    bool rhs_is_num = false;
                    try {
                        rhs_num = std::stod(rhs_str);
                        rhs_is_num = true;
                    } catch (...) {
                    }
                    if (rhs_is_num) {
                        const auto& iv = it_var->second;
                        if (iv.is_empty()) {
                            return false;
                        }
                        if (op == "<")
                            return !std::isinf(iv.hi) && iv.hi < rhs_num;
                        if (op == "<=")
                            return !std::isinf(iv.hi) && iv.hi <= rhs_num;
                        if (op == ">")
                            return !std::isinf(iv.lo) && iv.lo > rhs_num;
                        if (op == ">=")
                            return !std::isinf(iv.lo) && iv.lo >= rhs_num;
                        if (op == "==")
                            return !std::isinf(iv.lo) && !std::isinf(iv.hi) && std::abs(iv.lo - rhs_num) < 1e-6 &&
                                   std::abs(iv.hi - rhs_num) < 1e-6;
                        if (op == "!=")
                            return (rhs_num < iv.lo || rhs_num > iv.hi);
                    }
                    // Soundness: variable was tracked by interval analysis, do NOT fallback to initial value!
                    return false;
                }
            }

            // 2. Look up variable or port definition fallback
            double lhs_val = 0.0;
            bool lhs_found = false;

            for (const auto& v : ir.variables) {
                if (v.name == lhs_str) {
                    try {
                        lhs_val = std::stod(v.initial_value);
                        lhs_found = true;
                    } catch (...) {
                        if (v.min_value.has_value()) {
                            lhs_val = static_cast<double>(*v.min_value);
                            lhs_found = true;
                        }
                    }
                    break;
                }
            }

            if (!lhs_found) {
                for (const auto& p : ir.ports) {
                    if (p.name == lhs_str) {
                        if (!p.default_value.empty()) {
                            try {
                                lhs_val = std::stod(p.default_value);
                                lhs_found = true;
                            } catch (...) {
                            }
                        } else if (p.min_value.has_value()) {
                            lhs_val = *p.min_value;
                            lhs_found = true;
                        }
                        break;
                    }
                }
            }

            if (!lhs_found) {
                try {
                    lhs_val = std::stod(lhs_str);
                    lhs_found = true;
                } catch (...) {
                }
            }

            double rhs_val = 0.0;
            bool rhs_found = false;
            try {
                rhs_val = std::stod(rhs_str);
                rhs_found = true;
            } catch (...) {
                for (const auto& v : ir.variables) {
                    if (v.name == rhs_str) {
                        try {
                            rhs_val = std::stod(v.initial_value);
                            rhs_found = true;
                        } catch (...) {
                        }
                        break;
                    }
                }
            }

            if (lhs_found && rhs_found) {
                if (op == "<")
                    return lhs_val < rhs_val;
                if (op == "<=")
                    return lhs_val <= rhs_val;
                if (op == ">")
                    return lhs_val > rhs_val;
                if (op == ">=")
                    return lhs_val >= rhs_val;
                if (op == "==")
                    return std::abs(lhs_val - rhs_val) < 1e-6;
                if (op == "!=")
                    return std::abs(lhs_val - rhs_val) >= 1e-6;
            }

            // String equality fallback
            if (op == "==")
                return lhs_str == rhs_str;
            if (op == "!=")
                return lhs_str != rhs_str;
        }
    }

    const auto* s = ir.find_state(state);
    if (s != nullptr) {
        if (s->fqn == atom || s->alias == atom)
            return true;
    }

    return false;
}

}  // namespace mc_detail

using mc_detail::eval_atom_predicate;

bool ModelChecker::eval_predicate(const PropertyAstNode& node, const std::string& state) const {
    if (node.op == TemporalOp::Atom) {
        return eval_atom_predicate(node.atom, state, ir_, state_intervals_);
    }
    if (node.op == TemporalOp::Not) {
        if (!node.children.empty()) {
            return !eval_predicate(node.children[0], state);
        }
        return !eval_atom_predicate(node.atom, state, ir_, state_intervals_);
    }
    if (node.op == TemporalOp::And) {
        for (const auto& child : node.children) {
            if (!eval_predicate(child, state))
                return false;
        }
        return true;
    }
    if (node.op == TemporalOp::Or) {
        for (const auto& child : node.children) {
            if (eval_predicate(child, state))
                return true;
        }
        return false;
    }
    if (node.op == TemporalOp::Implies) {
        if (node.children.size() >= 2) {
            bool left = eval_predicate(node.children[0], state);
            bool right = eval_predicate(node.children[1], state);
            return !left || right;
        }
    }
    if (node.op == TemporalOp::Equivalent) {
        if (node.children.size() >= 2) {
            bool left = eval_predicate(node.children[0], state);
            bool right = eval_predicate(node.children[1], state);
            return left == right;
        }
    }
    return false;
}

ModelCheckResult ModelChecker::check_invariant(const FormalProperty& prop, const PropertyAstNode& predicate) {
    for (const auto& s_name : reachable_states_) {
        if (!eval_predicate(predicate, s_name)) {
            std::string desc = "Invariant '" + prop.raw_formula + "' evaluated to false in state '" + s_name + "'";
            auto trace = reconstruct_trace(s_name, desc);
            return {false, prop.name, prop.raw_formula, prop.kind, desc, std::move(trace)};
        }
    }
    return {true, prop.name, prop.raw_formula, prop.kind, "", {}};
}

ModelCheckResult ModelChecker::check_persistent_response(const FormalProperty& prop, const PropertyAstNode& trigger,
                                                         const PropertyAstNode& always_target) {
    for (const auto& start_state : reachable_states_) {
        if (!eval_predicate(trigger, start_state)) {
            continue;
        }
        std::queue<std::string> q;
        std::unordered_set<std::string> visited;
        q.push(start_state);
        visited.insert(start_state);
        while (!q.empty()) {
            std::string curr = std::move(q.front());
            q.pop();
            if (!eval_predicate(always_target, curr)) {
                std::string desc = "Persistent response '" + prop.raw_formula + "' violated: after '" +
                                   trigger.to_string() + "' in state '" + start_state + "', state '" + curr +
                                   "' violates '" + always_target.to_string() + "'";
                auto trace = reconstruct_trace(curr, desc);
                return {false, prop.name, prop.raw_formula, prop.kind, desc, std::move(trace)};
            }
            if (auto it = adj_.find(curr); it != adj_.end()) {
                for (const auto& edge : it->second) {
                    if (visited.insert(edge.target).second) {
                        q.push(edge.target);
                    }
                }
            }
        }
    }
    return {true, prop.name, prop.raw_formula, prop.kind, "", {}};
}

std::unordered_set<std::string> ModelChecker::compute_af(const PropertyAstNode& target) const {
    // Least fixpoint of AF: states satisfying target, plus states with at least one successor
    // whose successors are all already in the set.
    std::unordered_set<std::string> good;
    for (const auto& s : reachable_states_) {
        if (eval_predicate(target, s)) {
            good.insert(s);
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& s : reachable_states_) {
            if (good.count(s) != 0) {
                continue;
            }
            auto it = adj_.find(s);
            if (it == adj_.end() || it->second.empty()) {
                continue;
            }
            bool all_good = true;
            for (const auto& edge : it->second) {
                if (reachable_states_.count(edge.target) != 0 && good.count(edge.target) == 0) {
                    all_good = false;
                    break;
                }
            }
            if (all_good) {
                good.insert(s);
                changed = true;
            }
        }
    }
    return good;
}

ModelCheckResult ModelChecker::check_reachability(const FormalProperty& prop, const PropertyAstNode& target) {
    if (prop.kind == PropertyKind::Reachability) {
        for (const auto& s_name : reachable_states_) {
            if (eval_predicate(target, s_name)) {
                return {true, prop.name, prop.raw_formula, prop.kind, "", {}};
            }
        }
        std::string desc = "Target condition '" + prop.raw_formula + "' is unreachable from initial state '" +
                           root_state_ + "' across all reachable states.";
        return {false, prop.name, prop.raw_formula, prop.kind, desc, {}};
    }

    // F P: on every execution from the initial state, P eventually holds.
    const auto good = compute_af(target);
    if (good.count(root_state_) != 0) {
        return {true, prop.name, prop.raw_formula, prop.kind, "", {}};
    }
    std::string desc = "Target condition '" + prop.raw_formula + "' is not inevitable from initial state '" +
                       root_state_ + "': an execution exists that never satisfies it.";
    return {false, prop.name, prop.raw_formula, prop.kind, desc, {}};
}

ModelCheckResult ModelChecker::check_response(const FormalProperty& prop, const PropertyAstNode& trigger,
                                              const PropertyAstNode& response_target) {
    // G (P -> F Q): from every P-state, ALL paths must eventually reach Q (inevitability, not mere reachability).
    const auto good = compute_af(response_target);
    for (const auto& s_name : reachable_states_) {
        if (eval_predicate(trigger, s_name) && good.count(s_name) == 0) {
            std::string desc = "State '" + s_name + "' triggered condition '" + trigger.to_string() +
                               "', but an execution exists that never reaches response target '" +
                               response_target.to_string() + "' (infinite loop or termination first).";
            auto trace = reconstruct_trace(s_name, desc);
            return {false, prop.name, prop.raw_formula, prop.kind, desc, std::move(trace)};
        }
    }
    return {true, prop.name, prop.raw_formula, prop.kind, "", {}};
}

ModelCheckResult ModelChecker::check_until(const FormalProperty& prop, const PropertyAstNode& left,
                                           const PropertyAstNode& right) {
    // Strong Until (P U Q): on every path, Q eventually holds, and P holds at every state prior to Q.
    std::unordered_set<std::string> good_states;

    for (const auto& s : reachable_states_) {
        if (eval_predicate(right, s)) {
            good_states.insert(s);
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& s : reachable_states_) {
            if (good_states.count(s) != 0)
                continue;

            if (eval_predicate(left, s)) {
                auto it = adj_.find(s);
                if (it != adj_.end() && !it->second.empty()) {
                    bool all_successors_good = true;
                    for (const auto& edge : it->second) {
                        if (reachable_states_.count(edge.target) != 0 && good_states.count(edge.target) == 0) {
                            all_successors_good = false;
                            break;
                        }
                    }
                    if (all_successors_good) {
                        good_states.insert(s);
                        changed = true;
                    }
                }
            }
        }
    }

    if (good_states.count(root_state_) != 0) {
        return {true, prop.name, prop.raw_formula, prop.kind, "", {}};
    }

    // Root state does not guarantee Until. Construct counterexample trace.
    std::string curr = root_state_;
    std::vector<std::string> ce_path = {curr};
    std::unordered_set<std::string> visited_ce = {curr};
    std::string violation_desc;

    while (true) {
        if (eval_predicate(right, curr)) {
            break;
        }
        if (!eval_predicate(left, curr)) {
            violation_desc = "State '" + curr + "' violates condition '" + left.to_string() + "' before '" +
                             right.to_string() + "' is satisfied";
            break;
        }
        auto it = adj_.find(curr);
        if (it == adj_.end() || it->second.empty()) {
            violation_desc =
                "Execution terminated in state '" + curr + "' without reaching '" + right.to_string() + "'";
            break;
        }
        std::string next_step;
        for (const auto& edge : it->second) {
            if (reachable_states_.count(edge.target) != 0 && good_states.count(edge.target) == 0) {
                next_step = edge.target;
                break;
            }
        }
        if (next_step.empty()) {
            next_step = it->second.front().target;
        }
        if (visited_ce.count(next_step) != 0) {
            violation_desc = "Execution caught in cycle without reaching '" + right.to_string() + "' (loops back to '" +
                             next_step + "')";
            ce_path.push_back(next_step);
            break;
        }
        visited_ce.insert(next_step);
        ce_path.push_back(next_step);
        curr = next_step;
    }

    if (violation_desc.empty()) {
        violation_desc = "Property '" + prop.raw_formula + "' not satisfied from initial state";
    }

    std::vector<CounterexampleStep> trace;
    for (size_t i = 0; i < ce_path.size(); ++i) {
        trace.push_back({i, ce_path[i], "", "", i == ce_path.size() - 1 ? violation_desc : "Step towards violation"});
    }
    return {false, prop.name, prop.raw_formula, prop.kind, violation_desc, std::move(trace)};
}

ModelCheckResult ModelChecker::check_next(const FormalProperty& prop, const PropertyAstNode& target) {
    auto it = adj_.find(root_state_);
    if (it == adj_.end() || it->second.empty()) {
        std::string desc = "Initial state '" + root_state_ + "' has no successor transitions for Next operator X";
        return {false, prop.name, prop.raw_formula, prop.kind, desc, reconstruct_trace(root_state_, desc)};
    }
    for (const auto& edge : it->second) {
        if (!eval_predicate(target, edge.target)) {
            std::string desc =
                "Successor state '" + edge.target + "' does not satisfy condition '" + target.to_string() + "'";
            std::vector<CounterexampleStep> trace;
            trace.push_back({0, root_state_, edge.event, edge.guard, "Initial active state"});
            trace.push_back({1, edge.target, "", "", desc});
            return {false, prop.name, prop.raw_formula, prop.kind, desc, std::move(trace)};
        }
    }
    return {true, prop.name, prop.raw_formula, prop.kind, "", {}};
}

ModelCheckResult ModelChecker::check_next_response(const FormalProperty& prop, const PropertyAstNode& trigger,
                                                   const PropertyAstNode& next_target) {
    for (const auto& s_name : reachable_states_) {
        if (eval_predicate(trigger, s_name)) {
            auto it = adj_.find(s_name);
            if (it == adj_.end() || it->second.empty()) {
                std::string desc = "State '" + s_name + "' triggered condition '" + trigger.to_string() +
                                   "', but has no successor states for Next operator";
                auto trace = reconstruct_trace(s_name, desc);
                return {false, prop.name, prop.raw_formula, prop.kind, desc, std::move(trace)};
            }
            for (const auto& edge : it->second) {
                if (!eval_predicate(next_target, edge.target)) {
                    std::string desc = "State '" + s_name + "' triggered condition '" + trigger.to_string() +
                                       "', but successor '" + edge.target + "' does not satisfy '" +
                                       next_target.to_string() + "'";
                    auto trace = reconstruct_trace(s_name, "Trigger state");
                    trace.push_back({trace.size(), edge.target, edge.event, edge.guard, desc});
                    return {false, prop.name, prop.raw_formula, prop.kind, desc, std::move(trace)};
                }
            }
        }
    }
    return {true, prop.name, prop.raw_formula, prop.kind, "", {}};
}

std::vector<ModelChecker::SccComponent> ModelChecker::find_sccs() const {
    std::vector<SccComponent> sccs;
    std::unordered_map<std::string, int> index_map;
    std::unordered_map<std::string, int> lowlink_map;
    std::unordered_set<std::string> on_stack;
    std::vector<std::string> stack;
    int index = 0;

    auto strongconnect = [&](auto& self, const std::string& v) -> void {
        index_map[v] = index;
        lowlink_map[v] = index;
        index++;
        stack.push_back(v);
        on_stack.insert(v);

        auto it = adj_.find(v);
        if (it != adj_.end()) {
            for (const auto& edge : it->second) {
                const std::string& w = edge.target;
                if (reachable_states_.count(w) == 0)
                    continue;

                if (index_map.find(w) == index_map.end()) {
                    self(self, w);
                    lowlink_map[v] = (std::min)(lowlink_map[v], lowlink_map[w]);
                } else if (on_stack.count(w) != 0) {
                    lowlink_map[v] = (std::min)(lowlink_map[v], index_map[w]);
                }
            }
        }

        if (lowlink_map[v] == index_map[v]) {
            SccComponent scc;
            while (true) {
                std::string w = stack.back();
                stack.pop_back();
                on_stack.erase(w);
                scc.states.push_back(w);
                if (w == v)
                    break;
            }
            if (scc.states.size() > 1) {
                scc.is_cyclic = true;
            } else if (!scc.states.empty()) {
                auto it_self = adj_.find(scc.states[0]);
                if (it_self != adj_.end()) {
                    for (const auto& edge : it_self->second) {
                        if (edge.target == scc.states[0]) {
                            scc.is_cyclic = true;
                            break;
                        }
                    }
                }
            }
            sccs.push_back(std::move(scc));
        }
    };

    for (const auto& s : reachable_states_) {
        if (index_map.find(s) == index_map.end()) {
            strongconnect(strongconnect, s);
        }
    }

    return sccs;
}

ModelCheckResult ModelChecker::check_infinitely_often(const FormalProperty& prop, const PropertyAstNode& target) {
    // G F P holds iff every reachable state inevitably reaches P (no infinite or terminating path avoids P).
    const auto good = compute_af(target);
    for (const auto& s : reachable_states_) {
        if (good.count(s) == 0) {
            std::string desc = "Recurrence property '" + prop.raw_formula + "' violated: an execution from state '" +
                               s + "' can loop forever or terminate without ever visiting '" + target.to_string() + "'";
            auto trace = reconstruct_trace(s, desc);
            return {false, prop.name, prop.raw_formula, prop.kind, desc, std::move(trace)};
        }
    }
    return {true, prop.name, prop.raw_formula, prop.kind, "", {}};
}

ModelCheckResult ModelChecker::check_eventually_always(const FormalProperty& prop, const PropertyAstNode& target) {
    auto sccs = find_sccs();

    // In FG P, once a persistent cycle is reached, all states in that cycle must satisfy target.
    for (const auto& scc : sccs) {
        if (!scc.is_cyclic)
            continue;

        for (const auto& s : scc.states) {
            if (!eval_predicate(target, s)) {
                std::string desc = "Persistence property '" + prop.raw_formula + "' violated: recurrent state '" + s +
                                   "' does not satisfy condition '" + target.to_string() + "'";
                auto trace = reconstruct_trace(s, desc);
                return {false, prop.name, prop.raw_formula, prop.kind, desc, std::move(trace)};
            }
        }
    }

    for (const auto& s : reachable_states_) {
        auto it = adj_.find(s);
        if (it == adj_.end() || it->second.empty()) {
            if (!eval_predicate(target, s)) {
                std::string desc = "Terminal state '" + s + "' violates condition '" + target.to_string() +
                                   "' in persistence property '" + prop.raw_formula + "'";
                auto trace = reconstruct_trace(s, desc);
                return {false, prop.name, prop.raw_formula, prop.kind, desc, std::move(trace)};
            }
        }
    }

    return {true, prop.name, prop.raw_formula, prop.kind, "", {}};
}

}  // namespace fsm::middleend::analysis

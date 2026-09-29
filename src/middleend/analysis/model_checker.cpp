#include "fsm/middleend/analysis/model_checker.hpp"

#include <algorithm>
#include <queue>
#include <sstream>

namespace fsm::middleend::analysis {

using namespace fsm::ir;
using namespace fsm::diagnostic;

std::string ModelCheckResult::format_counterexample() const {
    if (passed || counterexample_trace.empty()) {
        return "";
    }
    std::ostringstream ss;
    ss << "Counterexample execution trace:\n";
    for (const auto& step : counterexample_trace) {
        ss << "    Step " << step.step_index << ": State '" << step.state_name << "'";
        if (!step.event_name.empty()) {
            ss << " --[" << step.event_name;
            if (!step.guard_condition.empty()) {
                ss << " if " << step.guard_condition;
            }
            ss << "]-->";
        }
        if (!step.description.empty()) {
            ss << " (" << step.description << ")";
        }
        ss << "\n";
    }
    return ss.str();
}

ModelChecker::ModelChecker(const FsmIr& ir) : ir_(ir) {
    build_graph();
    EFSMIntervalAnalyzer analyzer(ir_);
    state_intervals_ = analyzer.compute_state_intervals();
}

ModelCheckResult ModelChecker::verify_property(const FormalProperty& prop) {
    if (!prop.ast.has_value()) {
        return {true, prop.name, prop.raw_formula, prop.kind, "", {}};
    }

    const auto& ast = *prop.ast;

    // 1. Until: P U Q
    if (ast.op == TemporalOp::Until && ast.children.size() >= 2) {
        return check_until(prop, ast.children[0], ast.children[1]);
    }

    // 2. Next: X P
    if (ast.op == TemporalOp::Next && !ast.children.empty()) {
        return check_next(prop, ast.children[0]);
    }

    // 3. Globally: G ( ... )
    if (ast.op == TemporalOp::Globally) {
        if (!ast.children.empty()) {
            // G (P -> F Q) or G (P -> X Q)
            if (ast.children[0].op == TemporalOp::Implies) {
                const auto& impl = ast.children[0];
                if (impl.children.size() >= 2) {
                    if (impl.children[1].op == TemporalOp::Finally) {
                        return check_response(
                            prop, impl.children[0],
                            impl.children[1].children.empty() ? impl.children[1] : impl.children[1].children[0]);
                    }
                    if (impl.children[1].op == TemporalOp::Next) {
                        return check_next_response(
                            prop, impl.children[0],
                            impl.children[1].children.empty() ? impl.children[1] : impl.children[1].children[0]);
                    }
                }
            }
            // Recurrence: G (F P) (infinitely often)
            if (ast.children[0].op == TemporalOp::Finally) {
                return check_infinitely_often(
                    prop, ast.children[0].children.empty() ? ast.children[0] : ast.children[0].children[0]);
            }
            // G (X P)
            if (ast.children[0].op == TemporalOp::Next) {
                const auto& nxt_child =
                    ast.children[0].children.empty() ? ast.children[0] : ast.children[0].children[0];
                return check_next_response(prop, PropertyAstNode("true"), nxt_child);
            }
        }
        // General Invariant: G (P)
        return check_invariant(prop, ast.children.empty() ? ast : ast.children[0]);
    }

    // 4. Finally: F ( ... )
    if (ast.op == TemporalOp::Finally) {
        if (!ast.children.empty()) {
            // Persistence: F (G P) (eventually always)
            if (ast.children[0].op == TemporalOp::Globally) {
                return check_eventually_always(
                    prop, ast.children[0].children.empty() ? ast.children[0] : ast.children[0].children[0]);
            }
        }
        // General Reachability: F (P)
        return check_reachability(prop, ast.children.empty() ? ast : ast.children[0]);
    }

    // 5. Simple Invariant / Safety
    return check_invariant(prop, ast);
}

std::vector<ModelCheckResult> ModelChecker::verify_all() {
    std::vector<ModelCheckResult> results;
    results.reserve(ir_.properties.size());
    for (const auto& prop : ir_.properties) {
        results.push_back(verify_property(prop));
    }
    return results;
}

std::vector<EFSMAnalysisFinding> ModelChecker::verify_efsm_data_paths(DiagnosticEngine& diag) {
    EFSMIntervalAnalyzer analyzer(ir_);
    return analyzer.analyze(diag);
}

void ModelChecker::build_graph() {
    root_state_ = ir_.initial_state_id.empty() ? ir_.initial_state : ir_.initial_state_id;
    if (root_state_.empty() && !ir_.states.empty()) {
        root_state_ = ir_.states.front().name;
    }

    for (const auto& t : ir_.transitions) {
        const std::string& src = t.source;
        const std::string& dst = t.target;
        if (!src.empty() && !dst.empty()) {
            GraphEdge edge;
            edge.target = dst;
            edge.event = t.event.empty() ? t.get_trigger_name() : t.event;
            edge.guard = t.guard.has_value() ? *t.guard : "";
            adj_[src].push_back(edge);
        }
    }

    // BFS Reachability and predecessor tree construction
    if (!root_state_.empty()) {
        std::queue<std::string> q;
        q.push(root_state_);
        reachable_states_.insert(root_state_);

        while (!q.empty()) {
            std::string curr = q.front();
            q.pop();

            // Composite child states
            if (const auto* s = ir_.find_state(curr)) {
                if (s->is_composite) {
                    for (const auto& sub : ir_.states) {
                        if (sub.parent_state == curr && reachable_states_.count(sub.name) == 0) {
                            reachable_states_.insert(sub.name);
                            predecessor_map_[sub.name] = {curr, {sub.name, "enter_composite", ""}};
                            q.push(sub.name);
                        }
                    }
                }
            }

            auto it = adj_.find(curr);
            if (it != adj_.end()) {
                for (const auto& edge : it->second) {
                    if (reachable_states_.count(edge.target) == 0) {
                        reachable_states_.insert(edge.target);
                        predecessor_map_[edge.target] = {curr, edge};
                        q.push(edge.target);
                    }
                }
            }
        }
    }
}

std::vector<CounterexampleStep> ModelChecker::reconstruct_trace(const std::string& target_state,
                                                                const std::string& violation_desc) const {
    std::vector<CounterexampleStep> steps;
    std::string curr = target_state;

    std::vector<std::pair<std::string, GraphEdge>> path;
    std::unordered_set<std::string> visited_trace;
    while (curr != root_state_ && predecessor_map_.count(curr) != 0) {
        if (!visited_trace.insert(curr).second) {
            break;
        }
        const auto& p = predecessor_map_.at(curr);
        path.emplace_back(p.first, p.second);
        curr = p.first;
    }
    std::reverse(path.begin(), path.end());

    std::size_t idx = 0;
    steps.push_back({idx++, root_state_, path.empty() ? "" : path[0].second.event,
                     path.empty() ? "" : path[0].second.guard,
                     root_state_ == target_state ? violation_desc : "Initial active state"});

    for (std::size_t i = 0; i < path.size(); ++i) {
        std::string state = path[i].second.target;
        std::string next_evt = (i + 1 < path.size()) ? path[i + 1].second.event : "";
        std::string next_grd = (i + 1 < path.size()) ? path[i + 1].second.guard : "";
        std::string desc = (state == target_state) ? violation_desc : "Normal transition execution";
        steps.push_back({idx++, state, next_evt, next_grd, desc});
    }

    return steps;
}

namespace {

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
        return !eval_atom_predicate(atom.substr(1), state, ir, state_intervals);
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
                if (it_var != it_state->second.end() && !it_var->second.is_empty() &&
                    (!std::isinf(it_var->second.lo) || !std::isinf(it_var->second.hi))) {
                    double rhs_num = 0.0;
                    bool rhs_is_num = false;
                    try {
                        rhs_num = std::stod(rhs_str);
                        rhs_is_num = true;
                    } catch (...) {
                    }
                    if (rhs_is_num) {
                        const auto& iv = it_var->second;
                        if (op == "<")
                            return iv.hi < rhs_num;
                        if (op == "<=")
                            return iv.hi <= rhs_num;
                        if (op == ">")
                            return iv.lo > rhs_num;
                        if (op == ">=")
                            return iv.lo >= rhs_num;
                        if (op == "==")
                            return std::abs(iv.lo - rhs_num) < 1e-6 && std::abs(iv.hi - rhs_num) < 1e-6;
                        if (op == "!=")
                            return iv.lo > rhs_num || iv.hi < rhs_num;
                    }
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
        if (s->description.find(atom) != std::string::npos)
            return true;
    }

    return false;
}

}  // namespace

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

ModelCheckResult ModelChecker::check_reachability(const FormalProperty& prop, const PropertyAstNode& target) {
    for (const auto& s_name : reachable_states_) {
        if (eval_predicate(target, s_name)) {
            return {true, prop.name, prop.raw_formula, prop.kind, "", {}};
        }
    }
    std::string desc = "Target condition '" + prop.raw_formula + "' is unreachable from initial state '" + root_state_ +
                       "' across all reachable states.";
    return {false, prop.name, prop.raw_formula, prop.kind, desc, {}};
}

ModelCheckResult ModelChecker::check_response(const FormalProperty& prop, const PropertyAstNode& trigger,
                                              const PropertyAstNode& response_target) {
    for (const auto& s_name : reachable_states_) {
        if (eval_predicate(trigger, s_name)) {
            std::unordered_set<std::string> local_visited;
            std::queue<std::string> q;
            q.push(s_name);
            local_visited.insert(s_name);
            bool found = false;

            while (!q.empty()) {
                std::string c = q.front();
                q.pop();

                if (eval_predicate(response_target, c)) {
                    found = true;
                    break;
                }

                auto it = adj_.find(c);
                if (it != adj_.end()) {
                    for (const auto& edge : it->second) {
                        if (local_visited.count(edge.target) == 0) {
                            local_visited.insert(edge.target);
                            q.push(edge.target);
                        }
                    }
                }
            }

            if (!found) {
                std::string desc = "State '" + s_name + "' triggered condition '" + trigger.to_string() +
                                   "', but response target '" + response_target.to_string() +
                                   "' is unreachable from it.";
                auto trace = reconstruct_trace(s_name, desc);
                return {false, prop.name, prop.raw_formula, prop.kind, desc, std::move(trace)};
            }
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
    auto sccs = find_sccs();

    // Check if any reachable cyclic SCC has NO state satisfying target
    for (const auto& scc : sccs) {
        if (!scc.is_cyclic)
            continue;

        bool has_target = false;
        for (const auto& s : scc.states) {
            if (eval_predicate(target, s)) {
                has_target = true;
                break;
            }
        }
        if (!has_target) {
            const std::string& rep = scc.states[0];
            std::string desc = "Recurrence property '" + prop.raw_formula + "' violated: cycle containing state '" +
                               rep + "' never visits target condition '" + target.to_string() + "'";
            auto trace = reconstruct_trace(rep, desc);
            return {false, prop.name, prop.raw_formula, prop.kind, desc, std::move(trace)};
        }
    }

    // Check for reachable terminal states without target
    for (const auto& s : reachable_states_) {
        auto it = adj_.find(s);
        if (it == adj_.end() || it->second.empty()) {
            if (!eval_predicate(target, s)) {
                std::string desc = "Terminal deadlock state '" + s + "' does not satisfy target condition '" +
                                   target.to_string() + "' in recurrence property '" + prop.raw_formula + "'";
                auto trace = reconstruct_trace(s, desc);
                return {false, prop.name, prop.raw_formula, prop.kind, desc, std::move(trace)};
            }
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

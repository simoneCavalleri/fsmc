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
}

ModelCheckResult ModelChecker::verify_property(const FormalProperty& prop) {
    if (!prop.ast.has_value()) {
        return {true, prop.name, prop.raw_formula, prop.kind, "", {}};
    }

    const auto& ast = *prop.ast;

    // 1. Safety Invariant: G (P)
    if (ast.op == TemporalOp::Globally) {
        if (!ast.children.empty() && ast.children[0].op == TemporalOp::Implies) {
            // Response pattern: G (P -> F Q)
            const auto& impl = ast.children[0];
            if (impl.children.size() >= 2 && impl.children[1].op == TemporalOp::Finally) {
                return check_response(
                    prop, impl.children[0],
                    impl.children[1].children.empty() ? impl.children[1] : impl.children[1].children[0]);
            }
        }
        // General Invariant: G (P)
        return check_invariant(prop, ast.children.empty() ? ast : ast.children[0]);
    }

    // 2. Reachability: F (P)
    if (ast.op == TemporalOp::Finally) {
        return check_reachability(prop, ast.children.empty() ? ast : ast.children[0]);
    }

    // 3. Simple Invariant / Safety
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

bool eval_atom_predicate(std::string_view raw_atom, const std::string& state, const FsmIr& ir) {
    std::string atom = trim_str(raw_atom);
    if (atom.empty())
        return true;

    // Negation prefix: !expr
    if (atom.front() == '!') {
        return !eval_atom_predicate(atom.substr(1), state, ir);
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

            // Look up variable or port
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
        return eval_atom_predicate(node.atom, state, ir_);
    }
    if (node.op == TemporalOp::Not) {
        if (!node.children.empty()) {
            return !eval_predicate(node.children[0], state);
        }
        return !eval_atom_predicate(node.atom, state, ir_);
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

}  // namespace fsm::middleend::analysis

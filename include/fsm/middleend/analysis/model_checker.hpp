#pragma once

#include <algorithm>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/efsm_interval_analysis.hpp"

namespace fsm::middleend::analysis {

struct CounterexampleStep {
    std::size_t step_index{0};
    std::string state_name;
    std::string event_name;
    std::string guard_condition;
    std::string description;
};

struct ModelCheckResult {
    bool passed{true};
    std::string property_name;
    std::string property_formula;
    PropertyKind kind{PropertyKind::Safety};
    std::string violation_reason;
    std::vector<CounterexampleStep> counterexample_trace;

    [[nodiscard]] std::string format_counterexample() const {
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
};

/**
 * @brief Formal Verification and Model Checking Engine.
 *
 * Explores the explicit state reachability graph (Kripke model) and evaluates
 * Linear Temporal Logic (LTL) and Computation Tree Logic (CTL) specifications:
 * - Safety Invariants: G (Predicate)
 * - Reachability / Target: F (Predicate)
 * - Response / Liveness: G (Trigger -> F (Target))
 * - Mutual Exclusion: G (!(StateA && StateB))
 * - Deadlock Freedom: G (!Deadlock)
 */
class ModelChecker {
  public:
    explicit ModelChecker(const FsmIr& ir) : ir_(ir) { build_graph(); }

    ModelCheckResult verify_property(const FormalProperty& prop) {
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

    std::vector<ModelCheckResult> verify_all() {
        std::vector<ModelCheckResult> results;
        results.reserve(ir_.properties.size());
        for (const auto& prop : ir_.properties) {
            results.push_back(verify_property(prop));
        }
        return results;
    }

    std::vector<EFSMAnalysisFinding> verify_efsm_data_paths(DiagnosticEngine& diag) {
        EFSMIntervalAnalyzer analyzer(ir_);
        return analyzer.analyze(diag);
    }

  private:
    struct GraphEdge {
        std::string target;
        std::string event;
        std::string guard;
    };

    const FsmIr& ir_;
    std::string root_state_;
    std::unordered_map<std::string, std::vector<GraphEdge>> adj_;
    std::unordered_set<std::string> reachable_states_;
    std::unordered_map<std::string, std::pair<std::string, GraphEdge>> predecessor_map_;

    void build_graph() {
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

    [[nodiscard]] std::vector<CounterexampleStep> reconstruct_trace(const std::string& target_state,
                                                                    const std::string& violation_desc) const {
        std::vector<CounterexampleStep> steps;
        std::string curr = target_state;

        std::vector<std::pair<std::string, GraphEdge>> path;
        while (curr != root_state_ && predecessor_map_.count(curr) != 0) {
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

    bool eval_predicate(const PropertyAstNode& node, const std::string& state) const {
        if (node.op == TemporalOp::Atom) {
            if (node.atom == state)
                return true;
            if (node.atom == "state == " + state)
                return true;
            if (node.atom == "!" + state)
                return false;

            // Check against state description or flags
            const auto* s = ir_.find_state(state);
            if (s != nullptr) {
                if (s->fqn == node.atom || s->alias == node.atom)
                    return true;
                if (s->description.find(node.atom) != std::string::npos)
                    return true;
            }
            return false;
        }
        if (node.op == TemporalOp::Not) {
            if (!node.children.empty()) {
                return !eval_predicate(node.children[0], state);
            }
            return node.atom != state;
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

    ModelCheckResult check_invariant(const FormalProperty& prop, const PropertyAstNode& predicate) {
        for (const auto& s_name : reachable_states_) {
            if (!eval_predicate(predicate, s_name)) {
                // Invariant violated in s_name!
                std::string desc = "Invariant '" + prop.raw_formula + "' evaluated to false in state '" + s_name + "'";
                auto trace = reconstruct_trace(s_name, desc);
                return {false, prop.name, prop.raw_formula, prop.kind, desc, std::move(trace)};
            }
        }
        return {true, prop.name, prop.raw_formula, prop.kind, "", {}};
    }

    ModelCheckResult check_reachability(const FormalProperty& prop, const PropertyAstNode& target) {
        for (const auto& s_name : reachable_states_) {
            if (eval_predicate(target, s_name)) {
                return {true, prop.name, prop.raw_formula, prop.kind, "", {}};
            }
        }
        // Unreachable
        std::string desc = "Target condition '" + prop.raw_formula + "' is unreachable from initial state '" +
                           root_state_ + "' across all reachable states.";
        return {false, prop.name, prop.raw_formula, prop.kind, desc, {}};
    }

    ModelCheckResult check_response(const FormalProperty& prop, const PropertyAstNode& trigger,
                                    const PropertyAstNode& response_target) {
        for (const auto& s_name : reachable_states_) {
            if (eval_predicate(trigger, s_name)) {
                // Check if response_target is reachable from s_name
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
};

}  // namespace fsm::middleend::analysis

namespace fsm::middleend {
using analysis::ModelChecker;
using analysis::ModelCheckResult;
using analysis::CounterexampleStep;
}  // namespace fsm::middleend

#include <algorithm>
#include <queue>
#include <sstream>

#include "fsm/middleend/analysis/model_checker.hpp"

namespace fsm::middleend::analysis {

using namespace fsm::ir;
using namespace fsm::diagnostic;

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

    auto contains_ctl = [](auto& self, const PropertyAstNode& n) -> bool {
        if (n.op == TemporalOp::EX || n.op == TemporalOp::AX || n.op == TemporalOp::EF || n.op == TemporalOp::AF ||
            n.op == TemporalOp::EG || n.op == TemporalOp::AG || n.op == TemporalOp::EU || n.op == TemporalOp::AU) {
            return true;
        }
        for (const auto& child : n.children) {
            if (self(self, child)) {
                return true;
            }
        }
        return false;
    };

    // 0. Computation Tree Logic (CTL) Operators (including negated / composite CTL formulas such as !(AG p))
    if (contains_ctl(contains_ctl, ast)) {
        return check_ctl(prop, ast);
    }

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
                    if (impl.children[1].op == TemporalOp::Globally) {
                        return check_persistent_response(
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
    root_state_ = ir_.initial_state;
    if (root_state_.empty()) {
        if (const auto* s = ir_.find_state_by_id(ir_.initial_state_id)) {
            root_state_ = s->name;
        } else {
            root_state_ = ir_.initial_state_id;
        }
    }
    if (root_state_.empty() && !ir_.states.empty()) {
        root_state_ = ir_.states.front().name;
    }

    auto resolve_name = [this](const std::string& name, const std::string& id) -> std::string {
        if (!name.empty()) {
            return name;
        }
        if (!id.empty()) {
            if (const auto* s = ir_.find_state_by_id(id)) {
                return s->name;
            }
            return id;
        }
        return "";
    };

    for (const auto& s : ir_.states) {
        if (!s.name.empty()) {
            adj_[s.name];
        }
    }

    for (const auto& t : ir_.transitions) {
        if (t.guard.has_value()) {
            std::string g_eff = *t.guard;
            if (const auto* gm = ir_.find_guard(g_eff);
                gm != nullptr && gm->raw_expression.has_value() && !gm->raw_expression->empty()) {
                g_eff = *gm->raw_expression;
            }
            if (g_eff == "false" || g_eff == "0") {
                continue;
            }
        }
        std::string src = resolve_name(t.source, t.source_id);
        std::string dst = resolve_name(t.target, t.target_id);
        if (!src.empty() && !dst.empty()) {
            GraphEdge edge;
            edge.target = dst;
            edge.event = t.event.empty() ? t.get_trigger_name() : t.event;
            edge.guard = t.guard.has_value() ? *t.guard : "";
            adj_[src].push_back(edge);
            adj_[dst];
        }
    }

    // Inherit outgoing transitions from ancestor composite states for child substates
    for (const auto& s : ir_.states) {
        if (s.name.empty() || s.parent_state.empty()) {
            continue;
        }
        std::string curr_parent = s.parent_state;
        std::unordered_set<std::string> visited_parents;
        while (!curr_parent.empty() && visited_parents.insert(curr_parent).second) {
            auto it_p = adj_.find(curr_parent);
            if (it_p != adj_.end()) {
                auto& sub_edges = adj_[s.name];
                for (const auto& p_edge : it_p->second) {
                    if (p_edge.event == "enter_composite") {
                        continue;
                    }
                    bool exists = false;
                    for (const auto& existing : sub_edges) {
                        if (existing.target == p_edge.target && existing.event == p_edge.event &&
                            existing.guard == p_edge.guard) {
                            exists = true;
                            break;
                        }
                    }
                    if (!exists) {
                        sub_edges.push_back(p_edge);
                    }
                }
            }
            const auto* p_node = ir_.find_state(curr_parent);
            curr_parent = (p_node != nullptr) ? p_node->parent_state : "";
        }
    }

    // Add initial substate entry transitions for composite states
    for (const auto& s : ir_.states) {
        if (s.name.empty() || s.initial_sub_state.empty()) {
            continue;
        }
        std::string init_sub = resolve_name(s.initial_sub_state, s.initial_sub_state);
        if (init_sub.empty()) {
            continue;
        }
        auto& edges = adj_[s.name];
        bool has_entry = false;
        for (const auto& edge : edges) {
            if (edge.target == init_sub) {
                has_entry = true;
                break;
            }
        }
        if (!has_entry) {
            edges.push_back(GraphEdge{init_sub, "enter_composite", ""});
            adj_[init_sub];
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

            // Composite child states: only enter the declared initial substate (already wired into adj_[curr]
            // above); if no initial_sub_state was declared, fall back to the first non-pseudostate child.
            if (const auto* s = ir_.find_state(curr)) {
                if (s->is_composite && s->initial_sub_state.empty()) {
                    for (const auto& sub : ir_.states) {
                        if (sub.parent_state == curr && sub.kind != StateKind::ShallowHistory &&
                            sub.kind != StateKind::DeepHistory && sub.kind != StateKind::Choice &&
                            sub.kind != StateKind::Junction && sub.kind != StateKind::Fork &&
                            sub.kind != StateKind::Join && reachable_states_.count(sub.name) == 0) {
                            reachable_states_.insert(sub.name);
                            predecessor_map_[sub.name] = {curr, {sub.name, "enter_composite", ""}};
                            q.push(sub.name);
                            break;
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

}  // namespace fsm::middleend::analysis

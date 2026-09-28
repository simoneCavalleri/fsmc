/**
 * @file livelock_analysis_pass.cpp
 * @brief Implementation of LivelockAnalysisPass.
 */

#include "fsm/middleend/analysis/livelock_analysis_pass.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fsm::middleend::analysis {

namespace {

bool is_zero_time_transition(const ir::TransitionEdge& edge) {
    if (!edge.event.empty()) {
        return false;
    }
    if (std::holds_alternative<ir::SignalTrigger>(edge.trigger)) {
        return std::get<ir::SignalTrigger>(edge.trigger).signal_name.empty();
    }
    if (std::holds_alternative<ir::TimeTrigger>(edge.trigger)) {
        const auto& tt = std::get<ir::TimeTrigger>(edge.trigger);
        return tt.duration_in_ms() == 0 && tt.duration_value == 0;
    }
    if (std::holds_alternative<ir::ChangeTrigger>(edge.trigger)) {
        return false;
    }
    if (std::holds_alternative<ir::AnonymousTrigger>(edge.trigger)) {
        return true;
    }
    return false;
}

std::string resolve_state_id(const ir::FsmIr& ir, const std::string& id_or_name) {
    if (auto* s = ir.find_state_by_id(id_or_name)) {
        return s->id;
    }
    if (auto* s = ir.find_state_by_name(id_or_name)) {
        return s->id;
    }
    return id_or_name;
}

}  // namespace

bool LivelockAnalysisPass::run(FsmIr& ir, DiagnosticEngine& diag) {
    // 1. Build adjacency list of zero-time transitions
    std::unordered_map<std::string, std::vector<std::string>> zero_adj;
    for (const auto& state : ir.states) {
        zero_adj[state.id] = {};
    }

    for (const auto& edge : ir.transitions) {
        if (!is_zero_time_transition(edge)) {
            continue;
        }

        std::string src = resolve_state_id(ir, edge.source_id.empty() ? edge.source : edge.source_id);
        std::string dst = resolve_state_id(ir, edge.target_id.empty() ? edge.target : edge.target_id);

        if (!src.empty() && !dst.empty()) {
            zero_adj[src].push_back(dst);
        }
    }

    // 2. Tarjan's Strongly Connected Components (SCC) or DFS cycle detection
    // Using 3-color DFS (0 = unvisited, 1 = visiting/gray, 2 = finished/black)
    std::unordered_map<std::string, int> color;
    std::vector<std::string> path;
    bool has_livelock = false;

    auto dfs = [&](auto& self, const std::string& u) -> bool {
        color[u] = 1;
        path.push_back(u);

        for (const auto& v : zero_adj[u]) {
            if (color[v] == 1) {
                // Found cycle from v to u
                std::string cycle_str;
                auto it = std::find(path.begin(), path.end(), v);
                while (it != path.end()) {
                    auto* sn = ir.find_state_by_id(*it);
                    cycle_str += (sn ? sn->name : *it) + " -> ";
                    ++it;
                }
                auto* vn = ir.find_state_by_id(v);
                cycle_str += (vn ? vn->name : v);

                diag.report(diagnostic::Diagnostic::safety_critical(
                    "E0401",
                    "Detected zero-time Zeno livelock cycle without delay or event consumption: " + cycle_str));
                return true;
            }
            if (color[v] == 0) {
                if (self(self, v))
                    return true;
            }
        }

        path.pop_back();
        color[u] = 2;
        return false;
    };

    for (const auto& state : ir.states) {
        if (color[state.id] == 0) {
            if (dfs(dfs, state.id)) {
                has_livelock = true;
            }
        }
    }

    return !has_livelock;
}

}  // namespace fsm::middleend::analysis

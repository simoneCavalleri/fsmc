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
    for (const auto& choice : ir.choice_nodes) {
        zero_adj[choice.name] = {};
    }

    for (const auto& edge : ir.transitions) {
        if (!edge.is_zero_time_eventless()) {
            continue;
        }
        // Sampled change-trigger edge detectors (`!__change_*_prev && ...`) are evaluated once per
        // discrete sampling cycle (z^-1) and cannot spin in zero-time Zeno cycles.
        if (edge.guard.has_value() && edge.guard->find("__change_") != std::string::npos) {
            continue;
        }

        std::vector<std::string> srcs;
        for (const auto& sid : edge.multi_source_ids) {
            std::string r = resolve_state_id(ir, sid);
            if (!r.empty())
                srcs.push_back(std::move(r));
        }
        for (const auto& sn : edge.source_ids) {
            std::string r = resolve_state_id(ir, sn);
            if (!r.empty() && std::find(srcs.begin(), srcs.end(), r) == srcs.end())
                srcs.push_back(std::move(r));
        }
        if (srcs.empty()) {
            std::string r = resolve_state_id(ir, edge.source_id.empty() ? edge.source : edge.source_id);
            if (!r.empty())
                srcs.push_back(std::move(r));
        }

        std::vector<std::string> dsts;
        for (const auto& tid : edge.multi_target_ids) {
            std::string r = resolve_state_id(ir, tid);
            if (!r.empty())
                dsts.push_back(std::move(r));
        }
        for (const auto& tn : edge.target_ids) {
            std::string r = resolve_state_id(ir, tn);
            if (!r.empty() && std::find(dsts.begin(), dsts.end(), r) == dsts.end())
                dsts.push_back(std::move(r));
        }
        if (dsts.empty()) {
            std::string r = resolve_state_id(ir, edge.target_id.empty() ? edge.target : edge.target_id);
            if (!r.empty())
                dsts.push_back(std::move(r));
        }

        for (const auto& s : srcs) {
            for (const auto& d : dsts) {
                zero_adj[s].push_back(d);
            }
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
    for (const auto& choice : ir.choice_nodes) {
        if (color[choice.name] == 0) {
            if (dfs(dfs, choice.name)) {
                has_livelock = true;
            }
        }
    }

    return !has_livelock;
}

}  // namespace fsm::middleend::analysis

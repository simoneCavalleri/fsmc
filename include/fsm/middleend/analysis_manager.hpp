/**
 * @file analysis_manager.hpp
 * @brief Cached middle-end analysis results with declarative pass-driven invalidation.
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/analysis/efsm_interval_analysis.hpp"

namespace fsm::middleend {

/**
 * @struct AdjacencyIndex
 * @brief Precomputed O(V + E) outgoing and incoming transition indices keyed by state name.
 */
struct AdjacencyIndex {
    std::unordered_map<std::string, std::vector<std::size_t>> outgoing;
    std::unordered_map<std::string, std::vector<std::size_t>> incoming;

    [[nodiscard]] static AdjacencyIndex build(const ir::FsmIr& ir) {
        AdjacencyIndex idx;
        idx.outgoing.reserve(ir.states.size() + ir.choice_nodes.size());
        idx.incoming.reserve(ir.states.size() + ir.choice_nodes.size());
        for (const auto& s : ir.states) {
            idx.outgoing[s.name];
            idx.incoming[s.name];
        }
        for (const auto& c : ir.choice_nodes) {
            idx.outgoing[c.name];
            idx.incoming[c.name];
        }
        for (std::size_t i = 0; i < ir.transitions.size(); ++i) {
            const auto& t = ir.transitions[i];
            if (!t.source.empty()) {
                idx.outgoing[t.source].push_back(i);
            }
            if (!t.target.empty()) {
                idx.incoming[t.target].push_back(i);
            }
        }
        return idx;
    }
};

using EFSMIntervalResult = std::unordered_map<std::string, std::unordered_map<std::string, analysis::Interval>>;

/**
 * @class AnalysisManager
 * @brief Lazily computes and caches expensive middle-end analyses across a PassManager pipeline run.
 */
class AnalysisManager {
  public:
    static constexpr std::string_view kAdjacencyAnalysis = "AdjacencyIndex";
    static constexpr std::string_view kEFSMIntervalsAnalysis = "EFSMIntervals";

    [[nodiscard]] const AdjacencyIndex& get_adjacency(const ir::FsmIr& ir) {
        if (!adjacency_.has_value()) {
            adjacency_ = AdjacencyIndex::build(ir);
            ++adjacency_computations_;
        }
        return *adjacency_;
    }

    [[nodiscard]] const EFSMIntervalResult& get_efsm_intervals(const ir::FsmIr& ir) {
        if (!efsm_intervals_.has_value()) {
            analysis::EFSMIntervalAnalyzer analyzer(ir);
            efsm_intervals_ = analyzer.compute_state_intervals();
            ++efsm_computations_;
        }
        return *efsm_intervals_;
    }

    [[nodiscard]] bool has_adjacency() const noexcept { return adjacency_.has_value(); }
    [[nodiscard]] bool has_efsm_intervals() const noexcept { return efsm_intervals_.has_value(); }

    [[nodiscard]] std::size_t adjacency_computations() const noexcept { return adjacency_computations_; }
    [[nodiscard]] std::size_t efsm_computations() const noexcept { return efsm_computations_; }

    void invalidate_all() noexcept {
        adjacency_.reset();
        efsm_intervals_.reset();
    }

    void invalidate(const std::vector<std::string>& analyses) noexcept {
        for (const auto& name : analyses) {
            if (name == "*" || name == "all") {
                invalidate_all();
                return;
            }
            if (name == kAdjacencyAnalysis) {
                adjacency_.reset();
            } else if (name == kEFSMIntervalsAnalysis) {
                efsm_intervals_.reset();
            }
        }
    }

  private:
    std::optional<AdjacencyIndex> adjacency_;
    std::optional<EFSMIntervalResult> efsm_intervals_;
    std::size_t adjacency_computations_{0};
    std::size_t efsm_computations_{0};
};

}  // namespace fsm::middleend

/**
 * @file orthogonal_product_pass.hpp
 * @brief Cartesian product expansion pass for concurrent orthogonal regions.
 */

#pragma once

#include <string>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;
using ir::StateNode;

/**
 * @class OrthogonalProductPass
 * @brief Target-Agnostic Middle-End Pass: Orthogonal Region Cartesian Product Expansion.
 *
 * Expands concurrent parallel states and orthogonal regions (S_R1 x S_R2 x ... x S_Rn)
 * into an equivalent sequential hierarchical state space, eliminating runtime concurrency
 * and multi-threaded synchronization requirements.
 */
class OrthogonalProductPass {
  public:
    static constexpr std::size_t DEFAULT_MAX_PRODUCT_STATES = 1024;
    std::size_t max_product_states{DEFAULT_MAX_PRODUCT_STATES};

    explicit OrthogonalProductPass(std::size_t max_states = DEFAULT_MAX_PRODUCT_STATES)
        : max_product_states(max_states) {}

    [[nodiscard]] static std::string name();
    [[nodiscard]] static std::string description();

    /**
     * @brief Expands parallel states in the model into sequential product states.
     * @param ir Target FsmIr to expand.
     * @param diag Diagnostic engine for error reporting.
     * @return True on success, false on failure.
     */
    bool run(FsmIr& ir, DiagnosticEngine& diag);

  private:
    struct RegionInfo {
        std::string region_id;
        std::string initial_state;
        std::vector<std::string> states;
    };

    bool expand_parallel_state(FsmIr& ir, StateNode& parent, DiagnosticEngine& diag);
};

}  // namespace fsm::middleend::passes

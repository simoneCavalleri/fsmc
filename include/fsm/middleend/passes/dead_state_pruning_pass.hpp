/**
 * @file dead_state_pruning_pass.hpp
 * @brief Unreachable state and dead transition pruning pass.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/pass_interface.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class DeadStatePruningPass
 * @brief Target-Agnostic Middle-End Pass: Dead State Elimination & Dead Transition Pruning.
 */
class DeadStatePruningPass : public ::fsm::middleend::IPass {
  public:
    explicit DeadStatePruningPass(bool enable_pruning = true) : prune_(enable_pruning) {}

void set_prune(bool enable) { prune_ = enable; }
    [[nodiscard]] bool is_prune_enabled() const noexcept { return prune_; }

    /**
     * @brief Traverses reachable states from the initial state and prunes unvisited subgraphs.
     * @param ir Target FsmIr to prune.
     * @param diag Diagnostic reporting engine.
     * @return True on success, false on failure.
     */
    [[nodiscard]] std::string name() const override { return "dead-state-pruning"; }
    [[nodiscard]] std::string description() const override { return "Prunes unreachable states and dead transitions from the state graph."; }
    [[nodiscard]] bool preserves_ir() const noexcept override { return false; }
    bool run(FsmIr& ir, DiagnosticEngine& diag) override;

  private:
    bool prune_{true};
};

}  // namespace fsm::middleend::passes

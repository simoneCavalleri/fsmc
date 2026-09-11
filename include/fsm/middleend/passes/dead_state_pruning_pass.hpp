/**
 * @file dead_state_pruning_pass.hpp
 * @brief Unreachable state and dead transition pruning pass.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class DeadStatePruningPass
 * @brief Target-Agnostic Middle-End Pass: Dead State Elimination & Dead Transition Pruning.
 */
class DeadStatePruningPass {
  public:
    explicit DeadStatePruningPass(bool enable_pruning = true) : prune_(enable_pruning) {}

    [[nodiscard]] static std::string name() { return "DeadStatePruning"; }
    [[nodiscard]] static std::string description() {
        return "Removes unreachable states and statically dead transitions from the IR graph";
    }

    void set_prune(bool enable) { prune_ = enable; }
    [[nodiscard]] bool is_prune_enabled() const noexcept { return prune_; }

    /**
     * @brief Traverses reachable states from the initial state and prunes unvisited subgraphs.
     * @param ir Target FsmIr to prune.
     * @param diag Diagnostic reporting engine.
     * @return True on success, false on failure.
     */
    bool run(FsmIr& ir, DiagnosticEngine& diag);

  private:
    bool prune_{true};
};

}  // namespace fsm::middleend::passes

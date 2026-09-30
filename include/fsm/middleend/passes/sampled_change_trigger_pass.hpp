/**
 * @file sampled_change_trigger_pass.hpp
 * @brief Lowers continuous boolean change triggers (when(pred)) into sampled edge-detector transitions.
 */

#pragma once

#include <string>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::middleend::passes {

using diagnostic::DiagnosticEngine;
using ir::FsmIr;

/**
 * @class SampledChangeTriggerPass
 * @brief Lowers ChangeTrigger into sampled edge-detector shadow registers and guards.
 *
 * For each transition with a ChangeTrigger `when(pred)`:
 * 1. Allocates a shadow boolean register `__change_<id>_prev` initialized to false.
 * 2. Rewrites the transition guard to check the edge condition:
 *    (!__change_<id>_prev && (pred))
 * 3. Appends an action effect resetting/updating `__change_<id>_prev = (pred)`.
 * 4. Replaces the trigger with an anonymous/spontaneous sampled trigger.
 */
class SampledChangeTriggerPass {
  public:
    [[nodiscard]] static std::string name() { return "SampledChangeTrigger"; }
    [[nodiscard]] static std::string description() {
        return "Lowers continuous change triggers (when) into discrete sampled edge detectors";
    }

    bool run(FsmIr& ir, DiagnosticEngine& diag);
};

}  // namespace fsm::middleend::passes

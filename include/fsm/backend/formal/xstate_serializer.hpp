/**
 * @file xstate_serializer.hpp
 * @brief Standard XState v5 JSON statechart serializer (createMachine config format).
 */

#pragma once

#include <string>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::formal {

using ir::FsmIr;

/**
 * @class XStateSerializer
 * @brief Serializes a canonical FsmIr into a standard XState v5 JSON machine definition.
 *
 * Emits standard XState v5 schema fields:
 * - `id`, `initial`, `context`, `states` (recursively nested for composite & parallel states)
 * - `type`: `"parallel"`, `"final"`, `"history"` (with `history: "shallow" | "deep"`)
 * - `entry`, `exit`, `invoke` (do-activity), `on`, `after`, `always`
 * - Transition objects with `target`, `guard`, `actions`, and `reenter` (External vs Local/Internal)
 * - Optional `meta` block for traceability requirements, deferred events, and port declarations
 */
class XStateSerializer {
  public:
    /**
     * @brief Serializes an FsmIr model into pretty-printed XState v5 JSON.
     * @param model Canonical FsmIr state machine model.
     * @return Pretty-printed JSON string conforming to XState v5 machine config structure.
     */
    [[nodiscard]] static std::string serialize(const FsmIr& model);
};

}  // namespace fsm::backend::formal

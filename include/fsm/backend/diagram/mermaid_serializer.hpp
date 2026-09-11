/**
 * @file mermaid_serializer.hpp
 * @brief Serializer converting canonical FsmIr models into Mermaid.js stateDiagram-v2 definitions.
 */

#pragma once

#include <string>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::diagram {
using ir::FsmIr;

/**
 * @class MermaidSerializer
 * @brief Emits state machine topology and transitions as Mermaid stateDiagram-v2 syntax.
 */
class MermaidSerializer {
  public:
    /**
     * @brief Serializes the FsmIr model into a Mermaid stateDiagram string.
     */
    static std::string serialize(const FsmIr& model);
};

}  // namespace fsm::backend::diagram

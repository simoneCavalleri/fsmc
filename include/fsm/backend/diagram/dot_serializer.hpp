/**
 * @file dot_serializer.hpp
 * @brief Serializer converting canonical FsmIr models into Graphviz DOT graphs.
 */

#pragma once

#include <string>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::diagram {
using ir::FsmIr;

/**
 * @class DotSerializer
 * @brief Emits state machine topology and transitions as Graphviz DOT format.
 */
class DotSerializer {
  public:
    /**
     * @brief Serializes the FsmIr model into a Graphviz DOT diagram string.
     */
    static std::string serialize(const FsmIr& model);
};

}  // namespace fsm::backend::diagram

/**
 * @file plantuml_serializer.hpp
 * @brief Serializer converting canonical FsmIr models into PlantUML state diagrams.
 */

#pragma once

#include <string>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::diagram {
using ir::FsmIr;

/**
 * @class PlantUmlSerializer
 * @brief Emits state machine topology and transitions as PlantUML syntax.
 */
class PlantUmlSerializer {
  public:
    /**
     * @brief Serializes the FsmIr model into a PlantUML state diagram string.
     */
    static std::string serialize(const FsmIr& model);
};

}  // namespace fsm::backend::diagram

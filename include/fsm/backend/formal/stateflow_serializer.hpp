/**
 * @file stateflow_serializer.hpp
 * @brief Serializer exporting canonical FsmIr models into MATLAB / Simulink Stateflow XML format.
 */

#pragma once

#include <string>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::formal {
using ir::FsmIr;

/**
 * @class StateflowSerializer
 * @brief Emits state machine model as Simulink Stateflow XML.
 */
class StateflowSerializer {
  public:
    /**
     * @brief Serializes the FsmIr model into Simulink Stateflow XML format.
     */
    static std::string serialize(const FsmIr& model);
};

}  // namespace fsm::backend::formal

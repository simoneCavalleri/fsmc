/**
 * @file cameo_serializer.hpp
 * @brief Serializer exporting canonical FsmIr models into Cameo / MagicDraw OMG UML 2.5 XMI format.
 */

#pragma once

#include <string>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::formal {
using ir::FsmIr;

/**
 * @class CameoSerializer
 * @brief Emits state machine model as OMG UML 2.5 compliant XMI XML.
 */
class CameoSerializer {
  public:
    /**
     * @brief Serializes the FsmIr model into Cameo XMI XML format.
     */
    static std::string serialize(const FsmIr& model);
};

}  // namespace fsm::backend::formal

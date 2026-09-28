/**
 * @file sysml2_serializer.hpp
 * @brief Serializer exporting canonical FsmIr models into OMG SysML v2 / KerML state definitions.
 */

#pragma once

#include <string>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::formal {
using ir::FsmIr;

/**
 * @class Sysml2Serializer
 * @brief Emits state machine model as OMG SysML v2 / KerML textual state definitions.
 */
class Sysml2Serializer {
  public:
    /**
     * @brief Serializes the FsmIr model into OMG SysML v2 textual syntax.
     */
    static std::string serialize(const FsmIr& model);
};

}  // namespace fsm::backend::formal

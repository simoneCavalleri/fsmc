/**
 * @file scxml_serializer.hpp
 * @brief Serializer exporting canonical FsmIr models into W3C SCXML XML format.
 */

#pragma once

#include <string>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::formal {
using ir::FsmIr;

/**
 * @class ScxmlSerializer
 * @brief Emits state machine model as W3C State Chart XML (SCXML).
 */
class ScxmlSerializer {
  public:
    /**
     * @brief Serializes the FsmIr model into W3C SCXML XML format.
     */
    static std::string serialize(const FsmIr& model);
};

}  // namespace fsm::backend::formal

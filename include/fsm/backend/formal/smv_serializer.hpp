/**
 * @file smv_serializer.hpp
 * @brief Serializer exporting canonical FsmIr models into NuSMV / nuXmv format.
 */

#pragma once

#include <string>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::formal {
using ir::FsmIr;

/**
 * @class SmvSerializer
 * @brief Serializer for nuXmv / NuSMV / SMV Formal Verification Language.
 *
 * Emits a complete, verifiable SMV module with:
 * - State enumerations (`VAR state : { ... };`)
 * - Event input variables (`VAR event : { ... };`)
 * - Extended state variables and input boolean guards
 * - Explicit transition case structures (`ASSIGN next(state) := case ... esac;`)
 * - Formal temporal logic specifications (`LTLSPEC`, `CTLSPEC`, `INVARSPEC`)
 */
class SmvSerializer {
  public:
    /**
     * @brief Serializes the FsmIr model into NuSMV / nuXmv module format.
     */
    static std::string serialize(const FsmIr& model);
};

}  // namespace fsm::backend::formal

#pragma once

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::cpp {

/**
 * @brief Validates that an IR model satisfies the C++ backend lowering contract.
 */
class CppBackendValidator {
  public:
    /**
     * @brief Reports every high-level construct that cannot be represented by the C++ transition table runtime.
     * @return true when the model is safe to pass to the C++ emitter.
     */
    static bool validate_model(const ir::FsmIr& model, diagnostic::DiagnosticEngine& diagnostics);
};

}  // namespace fsm::backend::cpp

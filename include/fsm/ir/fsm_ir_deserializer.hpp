/**
 * @file fsm_ir_deserializer.hpp
 * @brief Canonical JSON deserialization engine for the FsmIr AST Metamodel.
 */

#pragma once

#include <string>
#include <string_view>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::ir {

/**
 * @brief Deserializes an FsmIr abstract syntax tree from canonical JSON.
 */
class FsmIrDeserializer {
  public:
    /**
     * @brief Deserializes a standard JSON string into an FsmIr model.
     * @param json_content Raw JSON content string.
     * @param out_ir Destination FsmIr model to populate.
     * @param out_error Diagnostic error message if deserialization fails.
     * @return True if parsing succeeded, false otherwise.
     */
    static bool deserialize_json(std::string_view json_content, FsmIr& out_ir, std::string& out_error);
};

}  // namespace fsm::ir

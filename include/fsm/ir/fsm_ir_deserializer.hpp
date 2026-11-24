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
     * @brief Options controlling JSON deserialization strictness.
     */
    struct DeserializeOptions {
        /// When true (--input-format=ir-json), enforces canonical FsmIr schema version and rejects unknown/XState keys.
        bool strict_ir_json = false;
    };

    /**
     * @brief Deserializes a standard JSON string into an FsmIr model.
     * @param json_content Raw JSON content string.
     * @param out_ir Destination FsmIr model to populate.
     * @param out_error Diagnostic error message if deserialization fails.
     * @return True if parsing succeeded, false otherwise.
     */
    static bool deserialize_json(std::string_view json_content, FsmIr& out_ir, std::string& out_error);

    /**
     * @brief Deserializes a JSON string into an FsmIr model with explicit strictness options.
     */
    static bool deserialize_json(std::string_view json_content, FsmIr& out_ir, std::string& out_error,
                                 const DeserializeOptions& options);
};

}  // namespace fsm::ir

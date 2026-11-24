/**
 * @file fsm_ir_serializer.hpp
 * @brief Canonical JSON serialization engine for the FsmIr AST Metamodel.
 */

#pragma once

#include <string>
#include <string_view>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::ir {

/**
 * @brief Serializes an FsmIr abstract syntax tree to canonical JSON.
 */
class FsmIrSerializer {
  public:
    /// Canonical JSON schema version emitted in root "ir_schema_version".
    static constexpr std::string_view kIrSchemaVersion = "1.0";

    /**
     * @brief Serializes the complete FsmIr model into a standard JSON string.
     * @param ir The state machine intermediate representation to serialize.
     * @param indent_spaces Indentation width in spaces (default: 2).
     * @return Formatted JSON string representing the full AST.
     */
    static std::string serialize_json(const FsmIr& ir, int indent_spaces = 2);

    /**
     * @brief Computes the deterministic SHA-256 digest of the canonical JSON serialization of @p ir.
     */
    static std::string compute_sha256(const FsmIr& ir);
};

}  // namespace fsm::ir

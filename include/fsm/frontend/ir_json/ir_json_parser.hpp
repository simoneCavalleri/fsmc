/**
 * @file ir_json_parser.hpp
 * @brief Strict canonical FsmIr JSON ingestion parser (--input-format=ir-json).
 */

#pragma once

#include <string>
#include <string_view>

#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/fsm_ir_deserializer.hpp"

namespace fsm::frontend {

/**
 * @class IrJsonParser
 * @brief Fail-closed parser for canonical FsmIr JSON documents conforming to schemas/fsm_ir.schema.json.
 *
 * Unlike JsonParser (which also accepts XState JSON shorthands), IrJsonParser enforces
 * `ir_schema_version` and rejects unknown root fields so external SysML/MBSE/AI frontends
 * receive immediate schema validation errors.
 */
class IrJsonParser : public IParser {
  public:
    [[nodiscard]] FrontendKind kind() const noexcept override { return FrontendKind::Formal; }
    [[nodiscard]] std::string_view format_name() const noexcept override { return "ir-json"; }

    bool parse(std::string_view content, ir::FsmIr& out_ir, diagnostic::DiagnosticEngine& diag) override {
        std::string err;
        ir::FsmIrDeserializer::DeserializeOptions opts;
        opts.strict_ir_json = true;
        if (!ir::FsmIrDeserializer::deserialize_json(content, out_ir, err, opts)) {
            std::string code = "E_PARSE_IR_JSON";
            if (err.rfind("E_IR_SCHEMA_VERSION", 0) == 0) {
                code = "E_IR_SCHEMA_VERSION";
            } else if (err.rfind("E_IR_UNKNOWN_FIELD", 0) == 0) {
                code = "E_IR_UNKNOWN_FIELD";
            }
            diag.report(diagnostic::Diagnostic::error(code, err.empty() ? "Invalid canonical ir-json input" : err));
            return false;
        }
        return true;
    }
};

}  // namespace fsm::frontend

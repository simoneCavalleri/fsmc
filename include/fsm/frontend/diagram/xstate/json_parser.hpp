/**
 * @file json_parser.hpp
 * @brief Zero-overhead JSON frontend parser adapting FsmIrSerializer to the IParser interface.
 */

#pragma once

#include <string>
#include <string_view>

#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/ir/fsm_ir_deserializer.hpp"

namespace fsm::frontend {

/**
 * @class JsonParser
 * @brief Ingests standard JSON statechart / AST definitions into FsmIr.
 */
class JsonParser : public IParser {
  public:
    [[nodiscard]] FrontendKind kind() const noexcept override { return FrontendKind::Formal; }
    [[nodiscard]] std::string_view format_name() const noexcept override { return "json"; }

    bool parse(std::string_view content, ir::FsmIr& out_ir, diagnostic::DiagnosticEngine& diag) override {
        std::string err;
        if (!ir::FsmIrDeserializer::deserialize_json(content, out_ir, err)) {
            diag.report(diagnostic::Diagnostic::error("E_PARSE_JSON", err.empty() ? "JSON parse error" : err));
            return false;
        }
        return true;
    }
};

}  // namespace fsm::frontend

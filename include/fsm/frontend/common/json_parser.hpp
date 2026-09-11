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

    bool parse(std::string_view content, ir::FsmIr& out_ir, std::string& out_error) override {
        return ir::FsmIrDeserializer::deserialize_json(content, out_ir, out_error);
    }
};

}  // namespace fsm::frontend

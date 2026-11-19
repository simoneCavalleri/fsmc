/**
 * @file emitter_factory.hpp
 * @brief Factory for dynamic backend diagram and format serializer selection.
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend {

using ir::FsmIr;

/**
 * @class EmitterFactory
 * @brief Registry and dispatcher for diagram and formal serialization backends.
 */
class EmitterFactory {
  public:
    /**
     * @brief Emits the IR model into the requested target or diagram format (e.g. "cpp", "plantuml", "mermaid", "dot",
     * "json").
     */
    [[nodiscard]] static std::string emit(const FsmIr& ir, std::string_view format);

    /**
     * @brief Emits the IR model into the requested textual diagram format (e.g. "plantuml", "mermaid", "dot", "json").
     */
    [[nodiscard]] static std::string emit_diagram(const FsmIr& ir, std::string_view format);

    /**
     * @brief Enumerates all registered serialization format identifiers.
     */
    [[nodiscard]] static std::vector<std::string_view> supported_formats() noexcept;

    /**
     * @brief Checks whether a format identifier is supported by the emitter factory.
     */
    [[nodiscard]] static bool is_supported_format(std::string_view format_name) noexcept;
};

}  // namespace fsm::backend

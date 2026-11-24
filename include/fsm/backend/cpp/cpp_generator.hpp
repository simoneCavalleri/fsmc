/**
 * @file cpp_generator.hpp
 * @brief High-level code generator synthesizing modern, zero-allocation C++ FSM implementations.
 */

#pragma once

#include <string>
#include <string_view>

#include "fsm/backend/cpp/cpp_options.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::cpp {

using ir::FsmIr;

/**
 * @struct GeneratedCppBundle
 * @brief Split C++ header pair separating pure I/O interfaces from the FSM runtime engine.
 */
struct GeneratedCppBundle {
    std::string ports_header;  ///< Pure interface header (*_ports.hpp) — zero fsm.hpp dependency
    std::string fsm_header;    ///< State machine engine header (*_fsm.hpp)
};

/**
 * @class CppGenerator
 * @brief Top-level code synthesis engine generating complete or split C++ statechart headers.
 */
class CppGenerator {
  public:
    /**
     * @brief Synthesizes a single-file C++ header containing types, transition table, and runtime wrapper.
     * @param model Canonical FsmIr model.
     * @param options Target generation settings (C++17/20, standalone mode, thread safety).
     * @return Formatted C++ header code string.
     */
    static std::string generate_header(const FsmIr& model, const GeneratorOptions& options = {});

    /**
     * @brief Synthesizes a split header pair (*_ports.hpp + *_fsm.hpp).
     *
     * The generated ports header contains external type includes, enums, structs, InPorts/OutPorts,
     * Registers, Services/ServicesContract, and Events with zero dependency on fsm.hpp.
     * The generated FSM header includes @p ports_header_include and the FSM runtime.
     *
     * @param model Canonical FsmIr model.
     * @param options Target generation settings.
     * @param ports_header_include Include path written into the FSM header (e.g. "motor_ports.hpp").
     * @return Split pair of generated header strings.
     */
    static GeneratedCppBundle generate_bundle(const FsmIr& model, const GeneratorOptions& options = {},
                                              std::string_view ports_header_include = "");
};

}  // namespace fsm::backend::cpp

/**
 * @file cpp_generator.hpp
 * @brief High-level code generator synthesizing modern, zero-allocation C++ FSM implementations.
 */

#pragma once

#include <string>

#include "fsm/backend/cpp/cpp_options.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::cpp {

using ir::FsmIr;

/**
 * @class CppGenerator
 * @brief Top-level code synthesis engine generating complete, self-contained C++ statechart headers.
 */
class CppGenerator {
  public:
    /**
     * @brief Synthesizes a production C++ header containing types, transition table, and runtime wrapper.
     * @param model Canonical FsmIr model.
     * @param options Target generation settings (C++17/20, standalone mode, thread safety).
     * @return Formatted C++ header code string.
     */
    static std::string generate_header(const FsmIr& model, const GeneratorOptions& options = {});
};

}  // namespace fsm::backend::cpp

/**
 * @file cpp_model_emitter.hpp
 * @brief Stream-based modular code emitter for individual C++ code artifacts.
 */

#pragma once

#include <ostream>

#include "fsm/backend/cpp/cpp_options.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::cpp {

using ir::FsmIr;

/**
 * @class CppModelEmitter
 * @brief Modular stream emitter writing out individual C++ state machine components.
 */
class CppModelEmitter {
  public:
    /** @brief Emits enum declarations to output stream. */
    static void emit_enums(std::ostream& out, const FsmIr& model);

    /** @brief Emits struct definitions to output stream. */
    static void emit_structs(std::ostream& out, const FsmIr& model);

    /** @brief Emits domain data models and context state. */
    static void emit_domain_structures(std::ostream& out, const FsmIr& model);

    /** @brief Emits strongly-typed event structs. */
    static void emit_events(std::ostream& out, const FsmIr& model);

    /** @brief Emits state tags and hierarchy descriptors. */
    static void emit_states(std::ostream& out, const FsmIr& model);

    /** @brief Emits guard callable functors and evaluation functions. */
    static void emit_guards(std::ostream& out, const FsmIr& model, const GeneratorOptions& options);

    /** @brief Emits action callable functors and effect handlers. */
    static void emit_actions(std::ostream& out, const FsmIr& model, const GeneratorOptions& options);

    /** @brief Emits constexpr static transition table lookup matrix. */
    static void emit_transition_table(std::ostream& out, const FsmIr& model, const GeneratorOptions& options);

    /** @brief Emits convenience type aliases and statechart bindings. */
    static void emit_fsm_aliases(std::ostream& out, const FsmIr& model, const GeneratorOptions& options);

    /** @brief Emits complete statechart definition. */
    static void emit_model(std::ostream& out, const FsmIr& model, const GeneratorOptions& options);
};

}  // namespace fsm::backend::cpp

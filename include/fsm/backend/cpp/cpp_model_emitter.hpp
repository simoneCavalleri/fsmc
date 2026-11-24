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
    /** @brief Emits #include directives for externally bound POD/enum types. */
    static void emit_external_includes(std::ostream& out, const FsmIr& model);

    /** @brief Emits enum declarations (or external using aliases) to output stream. */
    static void emit_enums(std::ostream& out, const FsmIr& model);

    /** @brief Emits struct definitions (or external using aliases) to output stream. */
    static void emit_structs(std::ostream& out, const FsmIr& model);

    /** @brief Emits domain data models and context state (zero fsm.hpp dependency). */
    static void emit_domain_structures(std::ostream& out, const FsmIr& model);

    /** @brief Emits strongly-typed event structs. */
    static void emit_events(std::ostream& out, const FsmIr& model);

    /** @brief Emits the pure ports/types/events/services interface for *_ports.hpp. */
    static void emit_ports_interface(std::ostream& out, const FsmIr& model);

    /** @brief Emits state tags and hierarchy descriptors. */
    static void emit_states(std::ostream& out, const FsmIr& model);

    /** @brief Emits state tags and hierarchy descriptors with emission options. */
    static void emit_states(std::ostream& out, const FsmIr& model, const GeneratorOptions& options);

    /** @brief Emits guard callable functors and evaluation functions. */
    static void emit_guards(std::ostream& out, const FsmIr& model, const GeneratorOptions& options);

    /** @brief Emits action callable functors and effect handlers. */
    static void emit_actions(std::ostream& out, const FsmIr& model, const GeneratorOptions& options);

    /** @brief Emits constexpr static transition table lookup matrix. */
    static void emit_transition_table(std::ostream& out, const FsmIr& model, const GeneratorOptions& options);

    /** @brief Emits convenience type aliases and statechart bindings. */
    static void emit_fsm_aliases(std::ostream& out, const FsmIr& model, const GeneratorOptions& options);

    /** @brief Emits the state machine engine (guards, actions, states, transition table, aliases) for *_fsm.hpp. */
    static void emit_fsm_engine(std::ostream& out, const FsmIr& model, const GeneratorOptions& options);

    /** @brief Emits complete statechart definition (ports interface + FSM engine). */
    static void emit_model(std::ostream& out, const FsmIr& model, const GeneratorOptions& options);
};

}  // namespace fsm::backend::cpp

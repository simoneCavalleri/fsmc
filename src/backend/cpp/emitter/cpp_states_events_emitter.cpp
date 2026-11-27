#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "cpp_emitter_utils.hpp"
#include "fsm/backend/cpp/cpp_model_emitter.hpp"
#include "fsm/ir/fsm_ir_serializer.hpp"
#include "fsm/ir/guard.hpp"
#include "fsm/ir/identifier_utils.hpp"

namespace fsm::backend::cpp {

using namespace fsm::ir;
using namespace emitter_detail;

void CppModelEmitter::emit_events(std::ostream& out, const FsmIr& model) {
    if (model.signals.empty()) {
        return;
    }

    out << "// ============================================================================\n";
    out << "// Events & Signals\n";
    out << "// ============================================================================\n\n";

    // Collect names used exclusively as TimeTrigger names (emitted via ::fsm::after_ms<N> / ::fsm::every_ms<N>)
    std::unordered_set<std::string> discrete_signal_names;
    std::unordered_set<std::string> time_trigger_names;
    for (const auto& tr : model.transitions) {
        if (std::holds_alternative<TimeTrigger>(tr.trigger)) {
            const std::string tname = tr.get_trigger_name();
            if (!tname.empty()) {
                time_trigger_names.insert(tname);
            }
        } else if (!tr.event.empty()) {
            discrete_signal_names.insert(tr.event);
        }
    }

    for (const auto& sig : model.signals) {
        if (sig.name == "anonymous_event" || sig.name == "completion_event") {
            continue;
        }
        if (sig.attributes.empty() && time_trigger_names.count(sig.name) != 0U &&
            discrete_signal_names.count(sig.name) == 0U) {
            continue;
        }
        std::string event_type_name = escape_cpp_keyword(sig.name);
        if (sig.attributes.empty()) {
            out << "/**\n";
            out << " * @struct " << event_type_name << "\n";
            out << " * @brief Signal trigger '" << sig.name << "'.\n";
            if (!sig.description.empty()) {
                out << " * @details " << sig.description << "\n";
            }
            out << " */\n";
            out << "struct " << event_type_name << " {\n";
            out << "    static constexpr std::string_view name = \"" << sig.name << "\";\n";

            if (!sig.description.empty()) {
                out << "    // Description: " << sig.description << "\n";
            }
            out << "};\n\n";
        } else {
            out << "/**\n";
            out << " * @struct " << event_type_name << "\n";
            out << " * @brief Parameterized signal payload for '" << sig.name << "'.\n";
            if (!sig.description.empty()) {
                out << " * @details " << sig.description << "\n";
            }
            out << " */\n";
            out << "struct " << event_type_name << " {\n";
            out << "    static constexpr std::string_view name = \"" << sig.name << "\";\n";
            for (const auto& attr : sig.attributes) {
                if (attr.default_value.empty()) {
                    out << "    " << attr.type.to_cpp_type() << " " << attr.name << "{};\n";
                } else {
                    out << "    " << attr.type.to_cpp_type() << " " << attr.name << "{" << attr.default_value << "};\n";
                }
            }
            if (!sig.attributes.empty()) {
                const bool has_non_literal_attr = std::any_of(sig.attributes.begin(), sig.attributes.end(),
                                                              [](const auto& attr) { return attr.type.is_string(); });
                const std::string constexpr_kw = has_non_literal_attr ? "" : "constexpr ";
                out << "\n    " << constexpr_kw << event_type_name << "() = default;\n";
                out << "\n    " << constexpr_kw << "explicit " << event_type_name << "(";
                for (std::size_t i = 0; i < sig.attributes.size(); ++i) {
                    if (i > 0)
                        out << ", ";
                    out << sig.attributes[i].type.to_cpp_type() << " " << sig.attributes[i].name << "_";
                }
                out << ") : ";
                for (std::size_t i = 0; i < sig.attributes.size(); ++i) {
                    if (i > 0)
                        out << ", ";
                    out << sig.attributes[i].name << "(std::move(" << sig.attributes[i].name << "_))";
                }
                out << " {}\n";
            }
            if (!sig.validators.empty()) {
                out << "\n    /**\n";
                out << "     * @brief Validates signal payload attributes against constraints.\n";
                out << "     * @return true if payload is valid.\n";
                out << "     */\n";
                out << "    [[nodiscard]] constexpr bool is_valid() const noexcept {\n";
                out << "        return ";
                for (std::size_t i = 0; i < sig.validators.size(); ++i) {
                    if (i > 0)
                        out << " && ";
                    out << "(" << sig.validators[i] << ")";
                }
                out << ";\n";
                out << "    }\n";
            }
            out << "};\n\n";
        }
    }

    // Emit ADL run-to-completion internal signal drain helper when model emits portless internal trigger signals
    const auto emitted_summary = collect_emitted_signals(model);
    if (!emitted_summary.unique_signals.empty()) {
        std::unordered_set<std::string> trigger_signals;
        for (const auto& tr : model.transitions) {
            if (!tr.event.empty() && !std::holds_alternative<TimeTrigger>(tr.trigger)) {
                trigger_signals.insert(tr.event);
            }
        }
        std::vector<std::string> internal_emitted;
        for (const auto& sname : emitted_summary.unique_signals) {
            if (trigger_signals.count(sname) != 0U) {
                internal_emitted.push_back(sname);
            }
        }
        if (!internal_emitted.empty()) {
            const std::string safe_model_name = sanitize_cpp_model_name(model.name);
            out << "template <typename FsmT, typename InT, typename SrvT>\n";
            out << "inline void fsm_drain_internal_events(FsmT& sm, const InT& in, " << safe_model_name
                << "OutPorts& out, SrvT& srv) {\n";
            out << "    for (std::size_t _iter = 0; _iter < " << safe_model_name
                << "OutPorts::kMaxOutbox; ++_iter) {\n";
            out << "        std::size_t _found = " << safe_model_name << "OutPorts::kMaxOutbox;\n";
            out << "        for (std::size_t _i = 0; _i < out.outbox_count; ++_i) {\n";
            out << "            if (out.outbox[_i].port != nullptr && out.outbox[_i].port[0] != '\\0') {\n";
            out << "                continue;\n";
            out << "            }\n";
            out << "            if (";
            for (std::size_t k = 0; k < internal_emitted.size(); ++k) {
                if (k > 0) {
                    out << " || ";
                }
                out << "out.outbox[_i].tag == " << safe_model_name
                    << "EmittedEventTag::" << escape_cpp_keyword(internal_emitted[k]);
            }
            out << ") {\n";
            out << "                _found = _i;\n";
            out << "                break;\n";
            out << "            }\n";
            out << "        }\n";
            out << "        if (_found >= out.outbox_count) {\n";
            out << "            break;\n";
            out << "        }\n";
            out << "        const auto _ev = out.outbox[_found];\n";
            out << "        for (std::size_t _j = _found + 1; _j < out.outbox_count; ++_j) {\n";
            out << "            out.outbox[_j - 1] = out.outbox[_j];\n";
            out << "        }\n";
            out << "        --out.outbox_count;\n";
            out << "        switch (_ev.tag) {\n";
            for (const auto& isig : internal_emitted) {
                const std::string esc = escape_cpp_keyword(isig);
                out << "            case " << safe_model_name << "EmittedEventTag::" << esc << ": {\n";
                out << "                " << esc << " _raised{};\n";
                if (const auto* sdef = model.find_signal(isig)) {
                    for (const auto& attr : sdef->attributes) {
                        const std::string af = escape_cpp_keyword(attr.name);
                        out << "                _raised." << af << " = _ev." << esc << "_payload." << af << ";\n";
                    }
                }
                out << "                sm.dispatch_direct_ports(_raised, in, out, srv);\n";
                out << "                break;\n";
                out << "            }\n";
            }
            out << "            default:\n";
            out << "                break;\n";
            out << "        }\n";
            out << "    }\n";
            out << "}\n\n";
        }
    }
}

void CppModelEmitter::emit_states(std::ostream& out, const FsmIr& model) {
    emit_states(out, model, GeneratorOptions{});
}

void CppModelEmitter::emit_states(std::ostream& out, const FsmIr& model, const GeneratorOptions& options) {
    if (model.states.empty()) {
        return;
    }

    out << "// ============================================================================\n";
    out << "// States\n";
    out << "// ============================================================================\n\n";

    // Forward declare all states for recursive parent_type support
    for (const auto& state_item : model.states) {
        out << "struct " << escape_cpp_keyword(state_item.name) << ";\n";
    }
    out << "\n";

    for (const auto& state_item : model.states) {
        if (state_item.is_composite) {
            std::vector<std::string> sub_states;
            for (const auto& s : model.states) {
                if (s.parent_state == state_item.name) {
                    sub_states.push_back(s.name);
                }
            }
            out << "/// @brief Composite State: " << state_item.name << "\n";
            if (!sub_states.empty()) {
                out << "/// Contains substates: ";
                for (std::size_t i = 0; i < sub_states.size(); ++i) {
                    if (i > 0)
                        out << ", ";
                    out << sub_states[i];
                }
                out << "\n";
            }
            if (!state_item.initial_sub_state.empty()) {
                out << "/// Initial substate: " << state_item.initial_sub_state << "\n";
            }
            out << "\n";
        }
        if (!state_item.traceability_reqs.empty()) {
            out << "/// @satisfies ";
            for (std::size_t i = 0; i < state_item.traceability_reqs.size(); ++i) {
                if (i > 0)
                    out << ", ";
                out << state_item.traceability_reqs[i];
            }
            out << "\n";
        }
        std::string state_struct_name = escape_cpp_keyword(state_item.name);
        out << "/**\n";
        out << " * @struct " << state_struct_name << "\n";
        out << " * @brief State representation for '" << sanitize_cpp_comment(state_item.name) << "'.\n";
        if (!state_item.description.empty()) {
            out << " * @details " << sanitize_cpp_comment(state_item.description) << "\n";
        }
        for (const auto& req : state_item.traceability_reqs) {
            out << " * @trace " << sanitize_cpp_comment(req) << "\n";
            out << " * @satisfies " << sanitize_cpp_comment(req) << "\n";
        }
        out << " */\n";
        out << "struct " << state_struct_name << " {\n";
        out << "    static constexpr std::string_view name = \"" << escape_cpp_string_literal(state_item.name)
            << "\";\n";
        if (!state_item.parent_state.empty()) {
            out << "    static constexpr std::string_view parent = \""
                << escape_cpp_string_literal(state_item.parent_state) << "\";\n";
            out << "    using parent_type = " << escape_cpp_keyword(state_item.parent_state) << ";\n";
        }
        if (!state_item.deferred_events.empty()) {
            out << "    using deferred_events = ::fsm::type_list<";
            for (std::size_t i = 0; i < state_item.deferred_events.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << escape_cpp_keyword(state_item.deferred_events[i]);
            }
            out << ">;\n";
        }

        auto emit_lifecycle_hook = [&](const char* doc_comment, const char* method_name, const char* var_prefix,
                                       const std::vector<std::string>& action_names) {
            out << "\n    /** @brief " << doc_comment << " */\n";
            out << "    template <typename InPorts, typename OutPorts, typename Registers, typename Services";
            if (!options.include_stubs) {
                for (std::size_t i = 0; i < action_names.size(); ++i) {
                    out << ", typename Act" << i << " = " << resolve_action_cpp_name(action_names[i], model);
                }
            }
            out << ">\n";
            out << "    void " << method_name
                << "(const InPorts& in, OutPorts& out, Registers& reg, Services& srv) const {\n";
            for (std::size_t i = 0; i < action_names.size(); ++i) {
                std::string act_type = options.include_stubs ? resolve_action_cpp_name(action_names[i], model)
                                                             : ("Act" + std::to_string(i));
                std::string var_name =
                    std::string(var_prefix) + (action_names.size() > 1 || std::string_view(var_prefix) != "activity"
                                                   ? ("_" + std::to_string(i))
                                                   : "");
                out << "        auto " << var_name << " = " << act_type << "{};\n";
                out << "        ::fsm::call_action(" << var_name << ", in, out, reg, srv);\n";
            }
            out << "    }\n";
        };

        // Entry lifecycle actions
        if (!state_item.entry_actions.empty()) {
            std::vector<std::string> names;
            names.reserve(state_item.entry_actions.size());
            for (const auto& act : state_item.entry_actions) {
                names.push_back(act.name);
            }
            emit_lifecycle_hook("State entry lifecycle hook", "on_enter", "entry_action", names);
        }

        // Exit lifecycle actions
        if (!state_item.exit_actions.empty()) {
            std::vector<std::string> names;
            names.reserve(state_item.exit_actions.size());
            for (const auto& act : state_item.exit_actions) {
                names.push_back(act.name);
            }
            emit_lifecycle_hook("State exit lifecycle hook", "on_exit", "exit_action", names);
        }

        // Continuous do_activity hook
        if (state_item.do_activity.has_value() && !state_item.do_activity->empty()) {
            emit_lifecycle_hook("State continuous do_activity hook", "do_activity", "activity",
                                {*state_item.do_activity});
        }

        if (state_item.kind == StateKind::EntryPoint) {
            out << "    static constexpr bool is_entry_point = true;\n";
        } else if (state_item.kind == StateKind::ExitPoint) {
            out << "    static constexpr bool is_exit_point = true;\n";
        }
        if (state_item.time_invariant.has_value() && !state_item.time_invariant->empty()) {
            out << "    /// @invariant " << *state_item.time_invariant << "\n";
            out << "    static constexpr std::string_view time_invariant = \"" << *state_item.time_invariant << "\";\n";
            std::uint64_t max_ms =
                to_milliseconds(state_item.time_invariant->duration, state_item.time_invariant->unit);
            out << "    static constexpr std::uint64_t max_stay_duration_ms = " << max_ms << "ULL;\n";
        }

        if (!state_item.description.empty()) {
            out << "    // Description: " << sanitize_cpp_comment(state_item.description) << "\n";
        }
        out << "};\n\n";
    }
}

}  // namespace fsm::backend::cpp

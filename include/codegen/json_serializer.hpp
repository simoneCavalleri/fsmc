#pragma once

#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "fsm_model.hpp"

namespace fsm::codegen {

class JsonSerializer {
  public:
    static std::string serialize(const FsmModel& model) {
        std::ostringstream out;
        out << "{\n";
        out << "  \"id\": \"" << (model.name.empty() ? "StateMachine" : model.name) << "\",\n";
        if (!model.initial_state.empty()) {
            out << "  \"initial\": \"" << model.initial_state << "\",\n";
        }
        out << "  \"states\": {\n";

        // Group top-level states
        std::vector<const StateModel*> top_states;
        for (const auto& s : model.states) {
            if (s.parent_state.empty()) {
                top_states.push_back(&s);
            }
        }

        for (size_t i = 0; i < top_states.size(); ++i) {
            emit_state(out, *top_states[i], model, 2);
            if (i + 1 < top_states.size()) {
                out << ",";
            }
            out << "\n";
        }

        out << "  }\n";
        out << "}\n";
        return out.str();
    }

  private:
    static void emit_state(std::ostream& out, const StateModel& state, const FsmModel& model, size_t indent) {
        std::string pad(indent, ' ');
        out << pad << "\"" << state.name << "\": {\n";

        bool need_comma = false;

        // Initial sub-state if composite
        if (!state.initial_sub_state.empty()) {
            out << pad << "  \"initial\": \"" << state.initial_sub_state << "\"";
            need_comma = true;
        }

        // Deferred events
        if (!state.deferred_events.empty()) {
            if (need_comma) {
                out << ",\n";
            }
            out << pad << "  \"defer\": [";
            for (size_t d = 0; d < state.deferred_events.size(); ++d) {
                out << "\"" << state.deferred_events[d] << "\"";
                if (d + 1 < state.deferred_events.size()) {
                    out << ", ";
                }
            }
            out << "]";
            need_comma = true;
        }

        // Outgoing transitions for this state
        std::vector<const TransitionModel*> state_trans;
        for (const auto& t : model.transitions) {
            if (t.source == state.name) {
                state_trans.push_back(&t);
            }
        }

        if (!state_trans.empty()) {
            if (need_comma) {
                out << ",\n";
            }
            out << pad << "  \"on\": {\n";
            for (size_t t_idx = 0; t_idx < state_trans.size(); ++t_idx) {
                const auto* t = state_trans[t_idx];
                out << pad << "    \"" << (t->event.empty() ? "EVENT" : t->event) << "\": {\n";
                out << pad << "      \"target\": \"" << t->target << "\"";
                if (t->guard && !t->guard->empty()) {
                    out << ",\n" << pad << "      \"guard\": \"" << escape_json(*t->guard) << "\"";
                }
                if (t->action && !t->action->empty()) {
                    out << ",\n" << pad << "      \"action\": \"" << escape_json(*t->action) << "\"";
                }
                out << "\n" << pad << "    }";
                if (t_idx + 1 < state_trans.size()) {
                    out << ",";
                }
                out << "\n";
            }
            out << pad << "  }";
            need_comma = true;
        }

        // Child states if composite
        std::vector<const StateModel*> child_states;
        for (const auto& s : model.states) {
            if (s.parent_state == state.name) {
                child_states.push_back(&s);
            }
        }

        if (!child_states.empty()) {
            if (need_comma) {
                out << ",\n";
            }
            out << pad << "  \"states\": {\n";
            for (size_t c_idx = 0; c_idx < child_states.size(); ++c_idx) {
                emit_state(out, *child_states[c_idx], model, indent + 4);
                if (c_idx + 1 < child_states.size()) {
                    out << ",";
                }
                out << "\n";
            }
            out << pad << "  }";
        }

        out << "\n" << pad << "}";
    }

    static std::string escape_json(const std::string& input) {
        std::string res;
        for (char c : input) {
            if (c == '"') {
                res += "\\\"";
            } else if (c == '\\') {
                res += "\\\\";
            } else {
                res += c;
            }
        }
        return res;
    }
};

}  // namespace fsm::codegen

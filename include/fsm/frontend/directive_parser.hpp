#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

class DirectiveParser {
  public:
    // Strips whitespace and extracts directive key/args
    static bool is_directive(std::string_view line) {
        std::string trimmed = trim(line);
        return trimmed.find("@fsm:") != std::string::npos;
    }

    static std::string extract_directive_body(std::string_view line) {
        std::string trimmed = trim(line);
        auto pos = trimmed.find("@fsm:");
        if (pos == std::string::npos)
            return "";
        return trim(trimmed.substr(pos + 5));
    }

    // Parses @fsm:state [history=shallow|deep] [satisfies=["REQ-1", "REQ-2"]] [do_activity="async_worker"]
    static bool parse_state_directive(std::string_view body, StateNode& state) {
        std::string str(body);
        if (str.rfind("state", 0) == 0) {
            str = trim(str.substr(5));
        }
        // history=shallow / deep
        auto h_pos = str.find("history=");
        if (h_pos != std::string::npos) {
            auto val = extract_quoted_or_word(str, h_pos + 8);
            if (val == "deep") {
                state.kind = StateKind::DeepHistory;
            } else if (val == "shallow") {
                state.kind = StateKind::ShallowHistory;
            }
        }
        // do_activity="..."
        auto act_pos = str.find("do_activity=");
        if (act_pos != std::string::npos) {
            state.do_activity = extract_quoted_or_word(str, act_pos + 12);
        }
        // satisfies=["REQ-1", "REQ-2"]
        auto req_pos = str.find("satisfies=");
        if (req_pos != std::string::npos) {
            auto list = extract_array_items(str, req_pos + 10);
            for (auto& item : list) {
                state.traceability_reqs.push_back(std::move(item));
            }
        }
        return true;
    }

    // Parses @fsm:defer [Ev1, Ev2]
    static bool parse_defer_directive(std::string_view body, StateNode& state) {
        std::string str(body);
        if (str.rfind("defer", 0) == 0) {
            str = trim(str.substr(5));
        }
        auto list = extract_array_items(str, 0);
        for (auto& ev : list) {
            if (!ev.empty() && std::find(state.deferred_events.begin(), state.deferred_events.end(), ev) ==
                                   state.deferred_events.end()) {
                state.deferred_events.push_back(std::move(ev));
            }
        }
        return true;
    }

    // Parses @fsm:signal EvPacketRecv{uint32_t len, const uint8_t* ptr} validator="len > 0 && ptr != nullptr"
    static std::optional<SignalDefinition> parse_signal_directive(std::string_view body) {
        std::string str = trim(body);
        if (str.empty())
            return std::nullopt;

        if (str.rfind("signal", 0) == 0) {
            str = trim(str.substr(6));
        }

        SignalDefinition sig;
        auto brace_open = str.find('{');
        auto brace_close = str.find('}');

        if (brace_open != std::string::npos && brace_close != std::string::npos && brace_close > brace_open) {
            sig.name = trim(str.substr(0, brace_open));
            std::string attr_list = str.substr(brace_open + 1, brace_close - brace_open - 1);
            std::istringstream iss(attr_list);
            std::string token;
            while (std::getline(iss, token, ',')) {
                token = trim(token);
                if (token.empty())
                    continue;
                auto last_space = token.find_last_of(" \t*&");
                if (last_space != std::string::npos) {
                    std::string type = trim(token.substr(0, last_space + 1));
                    std::string name = trim(token.substr(last_space + 1));
                    sig.attributes.emplace_back(name, type);
                } else {
                    sig.attributes.emplace_back(token, "int");
                }
            }
        } else {
            // Signal without attributes, e.g. @fsm:signal EvSensorReady
            auto first_space = str.find_first_of(" \t");
            if (first_space != std::string::npos) {
                sig.name = trim(str.substr(0, first_space));
            } else {
                sig.name = str;
            }
        }

        // Parse validator="..."
        auto v_pos = str.find("validator=");
        if (v_pos != std::string::npos) {
            std::string val = extract_quoted_or_word(str, v_pos + 10);
            if (!val.empty()) {
                sig.validators.push_back(std::move(val));
            }
        }

        return sig;
    }

    // Parses @fsm:trans [id="<hash>"] [guard_ast="..."] [action_sig="..."]
    static bool parse_trans_directive(std::string_view body, TransitionEdge& trans) {
        std::string str(body);
        if (str.rfind("trans", 0) == 0) {
            str = trim(str.substr(5));
        }
        auto id_pos = str.find("id=");
        if (id_pos != std::string::npos) {
            trans.id = extract_quoted_or_word(str, id_pos + 3);
        }
        auto g_pos = str.find("guard_ast=");
        if (g_pos != std::string::npos) {
            std::string guard_expr = extract_quoted_or_word(str, g_pos + 10);
            trans.guard_ast = GuardAstNode(guard_expr);
        }
        auto a_pos = str.find("action_sig=");
        if (a_pos != std::string::npos) {
            std::string act = extract_quoted_or_word(str, a_pos + 11);
            trans.action_sig = ActionSignature(act, act);
        }
        return true;
    }

  private:
    static std::string trim(std::string_view s) {
        auto start = s.find_first_not_of(" \t\r\n'#%/");
        if (start == std::string::npos)
            return "";
        auto end = s.find_last_not_of(" \t\r\n;");
        return std::string(s.substr(start, end - start + 1));
    }

    static std::string extract_quoted_or_word(const std::string& str, std::size_t start) {
        if (start >= str.size())
            return "";
        while (start < str.size() && (str[start] == ' ' || str[start] == '\t')) {
            ++start;
        }
        if (start >= str.size())
            return "";
        if (str[start] == '"') {
            auto end = str.find('"', start + 1);
            if (end != std::string::npos) {
                return str.substr(start + 1, end - start - 1);
            }
            return str.substr(start + 1);
        }
        auto end = str.find_first_of(" \t\r\n];,", start);
        if (end != std::string::npos) {
            return str.substr(start, end - start);
        }
        return str.substr(start);
    }

    static std::vector<std::string> extract_array_items(const std::string& str, std::size_t start) {
        std::vector<std::string> items;
        auto open_bracket = str.find('[', start);
        auto close_bracket = str.find(']', open_bracket);
        if (open_bracket == std::string::npos || close_bracket == std::string::npos) {
            return items;
        }
        std::string inner = str.substr(open_bracket + 1, close_bracket - open_bracket - 1);
        std::istringstream iss(inner);
        std::string token;
        while (std::getline(iss, token, ',')) {
            std::string t = trim(token);
            if (t.front() == '"' && t.back() == '"' && t.size() >= 2) {
                t = t.substr(1, t.size() - 2);
            }
            if (!t.empty()) {
                items.push_back(std::move(t));
            }
        }
        return items;
    }
};

}  // namespace fsm::codegen

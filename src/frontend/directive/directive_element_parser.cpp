#include <algorithm>
#include <regex>
#include <sstream>

#include "directive_utils.hpp"
#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/ir/guard.hpp"
#include "fsm/ir/identifier_utils.hpp"

namespace fsm::frontend::directive {

using namespace fsm::ir;
using namespace detail;

bool DirectiveParser::is_directive(std::string_view line) {
    std::string trimmed = trim(line);
    return trimmed.find("@fsm:") != std::string::npos;
}

std::string DirectiveParser::extract_directive_body(std::string_view line) {
    std::string trimmed = trim(line);
    auto pos = trimmed.find("@fsm:");
    if (pos == std::string::npos)
        return "";
    return trim(trimmed.substr(pos + 5));
}

bool DirectiveParser::parse_state_directive(std::string_view body, StateNode& state) {
    std::string str(body);
    if (str.rfind("state", 0) == 0) {
        str = trim(str.substr(5));
    }
    auto h_pos = str.find("history=");
    if (h_pos != std::string::npos) {
        auto val = extract_quoted_or_word(str, h_pos + 8);
        if (val == "deep") {
            state.kind = StateKind::DeepHistory;
        } else if (val == "shallow") {
            state.kind = StateKind::ShallowHistory;
        }
    }
    auto k_pos = str.find("kind=");
    if (k_pos != std::string::npos) {
        auto val = extract_quoted_or_word(str, k_pos + 5);
        state.kind = state_kind_from_string(val);
    }
    auto inv_pos = str.find("invariant=");
    if (inv_pos == std::string::npos) {
        inv_pos = str.find("time_invariant=");
        if (inv_pos != std::string::npos) {
            inv_pos += 15;
        }
    } else {
        inv_pos += 10;
    }
    if (inv_pos != std::string::npos) {
        state.time_invariant = extract_quoted_or_word(str, inv_pos);
    }
    auto act_pos = str.find("do_activity=");
    if (act_pos != std::string::npos) {
        state.do_activity = extract_quoted_or_word(str, act_pos + 12);
    }
    auto sub_pos = str.find("submachine=");
    if (sub_pos != std::string::npos) {
        state.submachine = SubmachineRef(extract_quoted_or_word(str, sub_pos + 11));
    }
    auto req_pos = str.find("satisfies=");
    if (req_pos != std::string::npos) {
        auto list = extract_array_items(str, req_pos + 10);
        for (auto& item : list) {
            if (std::find(state.traceability_reqs.begin(), state.traceability_reqs.end(), item) ==
                state.traceability_reqs.end()) {
                state.traceability_reqs.push_back(std::move(item));
            }
        }
    }
    return true;
}

bool DirectiveParser::parse_state_directive(std::string_view body, FsmIr& model,
                                            const std::vector<std::string>& parent_stack) {
    std::string str(body);
    if (str.rfind("state", 0) == 0) {
        str = trim(str.substr(5));
    }
    std::string st_name;
    auto n_pos = str.find("name=");
    if (n_pos != std::string::npos) {
        st_name = extract_quoted_or_word(str, n_pos + 5);
    }
    StateNode* st = nullptr;
    if (!st_name.empty()) {
        st = model.find_state_mut(st_name);
        if (st == nullptr) {
            st = &model.add_state(st_name, parent_stack.empty() ? "" : parent_stack.back());
        }
    } else if (!parent_stack.empty()) {
        st = model.find_state_mut(parent_stack.back());
    }
    if (st != nullptr) {
        return parse_state_directive(body, *st);
    }
    return false;
}

bool DirectiveParser::parse_defer_directive(std::string_view body, StateNode& state) {
    std::string str(body);
    if (str.rfind("defer", 0) == 0) {
        str = trim(str.substr(5));
    }
    auto list = extract_array_items(str, 0);
    for (auto& ev : list) {
        if (!ev.empty() &&
            std::find(state.deferred_events.begin(), state.deferred_events.end(), ev) == state.deferred_events.end()) {
            state.deferred_events.push_back(std::move(ev));
        }
    }
    return true;
}

bool DirectiveParser::parse_submachine_directive(std::string_view body, StateNode& state) {
    std::string str(body);
    if (str.rfind("submachine", 0) == 0) {
        str = trim(str.substr(10));
    }
    if (str.empty()) {
        return false;
    }
    std::string target_name;
    std::string uri;
    auto n_pos = str.find("name=");
    if (n_pos != std::string::npos) {
        target_name = extract_quoted_or_word(str, n_pos + 5);
    } else {
        target_name = extract_quoted_or_word(str, 0);
    }
    auto u_pos = str.find("uri=");
    if (u_pos != std::string::npos) {
        uri = extract_quoted_or_word(str, u_pos + 4);
    }
    if (target_name.empty()) {
        return false;
    }
    state.submachine = SubmachineRef(target_name, uri);
    return true;
}

bool DirectiveParser::parse_trans_directive(std::string_view body, TransitionEdge& trans) {
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
    if (a_pos == std::string::npos) {
        a_pos = str.find("transition_action=");
        if (a_pos != std::string::npos) {
            a_pos += 18;
        } else {
            a_pos = str.find("action=");
            if (a_pos != std::string::npos) {
                a_pos += 7;
            }
        }
        if (a_pos != std::string::npos) {
            std::string act = extract_quoted_or_word(str, a_pos);
            trans.transition_action = ActionSignature(act, act);
        }
    } else {
        std::string act = extract_quoted_or_word(str, a_pos + 11);
        trans.transition_action = ActionSignature(act, act);
    }
    auto p_pos = str.find("priority=");
    if (p_pos == std::string::npos) {
        p_pos = str.find("prio=");
        if (p_pos != std::string::npos) {
            p_pos += 5;
        }
    } else {
        p_pos += 9;
    }
    if (p_pos != std::string::npos) {
        std::string p_str = extract_quoted_or_word(str, p_pos);
        if (!p_str.empty()) {
            try {
                trans.priority = static_cast<std::uint32_t>(std::stoul(p_str));
            } catch (...) {
            }
        }
    }
    return true;
}

}  // namespace fsm::frontend::directive

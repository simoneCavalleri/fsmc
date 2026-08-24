#pragma once

#include <cctype>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/frontend/guard_parser.hpp"
#include "fsm/frontend/parser_interface.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

class Sysml2Parser : public IParser {
  public:
    [[nodiscard]] static std::string format_name() { return "sysml2"; }

    bool parse(std::string_view content, FsmIr& model, std::string& error_message) override {
        std::string content_str(content);
        std::istringstream stream(content_str);
        std::string line;
        size_t line_number = 0;

        std::vector<std::string> state_stack;
        std::string accumulated_stmt;

        auto flush_stmt = [&](size_t current_line, bool is_block_open) -> bool {
            accumulated_stmt = trim(accumulated_stmt);
            if (!accumulated_stmt.empty()) {
                if (!process_statement(accumulated_stmt, model, state_stack, error_message, current_line,
                                       is_block_open)) {
                    return false;
                }
                accumulated_stmt.clear();
            }
            return true;
        };

        while (std::getline(stream, line)) {
            line_number++;
            std::string clean_line = strip_comments(line);
            clean_line = trim(clean_line);

            if (clean_line.empty()) {
                continue;
            }

            for (const char character : clean_line) {
                if (character == '{') {
                    if (!flush_stmt(line_number, true)) {
                        return false;
                    }
                } else if (character == ';') {
                    if (!flush_stmt(line_number, false)) {
                        return false;
                    }
                } else if (character == '}') {
                    if (!flush_stmt(line_number, false)) {
                        return false;
                    }
                    if (!state_stack.empty()) {
                        state_stack.pop_back();
                    }
                } else {
                    accumulated_stmt += character;
                }
            }
            if (!accumulated_stmt.empty() && accumulated_stmt.back() != ' ') {
                accumulated_stmt += ' ';
            }
        }

        if (!flush_stmt(line_number, false)) {
            return false;
        }

        if (model.states.empty()) {
            error_message = "SysML v2 parser: No states found in input.";
            return false;
        }

        if (model.initial_state.empty() && !model.states.empty()) {
            model.initial_state = model.states.front().name;
        }

        return true;
    }

  private:
    static std::string strip_comments(const std::string& line) {
        const auto line_comment_pos = line.find("//");
        std::string result = (line_comment_pos != std::string::npos) ? line.substr(0, line_comment_pos) : line;

        const auto block_comment_start = result.find("/*");
        if (block_comment_start != std::string::npos) {
            const auto block_comment_end = result.find("*/", block_comment_start + 2);
            if (block_comment_end != std::string::npos) {
                result.erase(block_comment_start, block_comment_end - block_comment_start + 2);
            } else {
                result.erase(block_comment_start);
            }
        }
        return result;
    }

    static std::string trim(const std::string& str) {
        const auto start = str.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            return "";
        }
        const auto end = str.find_last_not_of(" \t\r\n");
        return str.substr(start, end - start + 1);
    }

    static std::string normalize_whitespace(const std::string& str) {
        std::string result;
        bool in_space = false;
        for (const char character : str) {
            if (std::isspace(static_cast<unsigned char>(character)) != 0) {
                if (!in_space) {
                    result += ' ';
                    in_space = true;
                }
            } else {
                result += character;
                in_space = false;
            }
        }
        return trim(result);
    }

    static bool process_statement(const std::string& raw_stmt, FsmIr& model, std::vector<std::string>& state_stack,
                                  std::string& error_message, size_t line_number, bool is_block_open = false) {
        const std::string stmt = normalize_whitespace(raw_stmt);
        if (stmt.empty()) {
            return true;
        }

        // 1. state def <Name>
        static const std::regex state_def_regex(R"(^(?:state\s+def|package)\s+([A-Za-z_][A-Za-z0-9_]*))",
                                                std::regex::optimize);
        std::smatch match;
        if (std::regex_search(stmt, match, state_def_regex)) {
            if (model.name.empty() || model.name == "MyStateMachine" || model.name == "GeneratedFSM") {
                model.name = sanitize_identifier(match[1].str());
            }
            return true;
        }

        // 2. Choice node declaration: state <ChoiceName> <<choice>> or state <ChoiceName> :> Choice
        static const std::regex choice_regex(
            R"(^state\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:<<choice>>|<<junction>>|:>\s*Choice|:>\s*Junction))",
            std::regex::optimize);
        if (std::regex_search(stmt, match, choice_regex)) {
            const std::string choice_name = sanitize_identifier(match[1].str());
            model.add_choice_node(choice_name);
            return true;
        }

        // 3. state <Name> (composite or leaf)
        static const std::regex state_decl_regex(R"(^state\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
        if (std::regex_search(stmt, match, state_decl_regex)) {
            const std::string state_name = sanitize_identifier(match[1].str());
            const std::string parent_name = state_stack.empty() ? "" : state_stack.back();
            model.add_state(state_name, parent_name);

            if (!state_stack.empty()) {
                auto* parent = model.find_state_mut(state_stack.back());
                if (parent != nullptr) {
                    parent->is_composite = true;
                    if (parent->initial_sub_state.empty()) {
                        parent->initial_sub_state = state_name;
                    }
                }
            }
            if (is_block_open) {
                state_stack.push_back(state_name);
            }
            return true;
        }

        // 4. entry; then <State>; or initial; then <State>; or entry then <State>;
        static const std::regex initial_regex(R"(^(?:entry|initial)(?:\s*;)?\s*then\s+([A-Za-z_][A-Za-z0-9_]*))",
                                              std::regex::optimize);
        if (std::regex_search(stmt, match, initial_regex)) {
            const std::string init_target = sanitize_identifier(match[1].str());
            if (state_stack.empty()) {
                model.initial_state = init_target;
            } else {
                auto* parent = model.find_state_mut(state_stack.back());
                if (parent != nullptr) {
                    parent->initial_sub_state = init_target;
                }
            }
            return true;
        }

        // 5. Defer event: defer <Event>;
        static const std::regex defer_regex(R"(^defer\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
        if (std::regex_search(stmt, match, defer_regex)) {
            const std::string evt_name = sanitize_identifier(match[1].str());
            model.add_event(evt_name);
            if (!state_stack.empty()) {
                auto* curr_state = model.find_state_mut(state_stack.back());
                if (curr_state != nullptr) {
                    curr_state->deferred_events.push_back(evt_name);
                }
            }
            return true;
        }

        // 6. Transition clause: transition [name] [first S1] [accept E] [if G] [do A] [then S2]
        if (starts_with(stmt, "transition")) {
            return parse_transition_statement(stmt, model, state_stack, error_message, line_number);
        }

        return true;
    }

    static bool parse_transition_statement(const std::string& stmt, FsmIr& model,
                                           const std::vector<std::string>& state_stack, std::string& /*error_message*/,
                                           size_t /*line_number*/) {
        std::string source;
        std::string target;
        std::string event;
        std::string guard;
        std::string action;

        static const std::regex first_regex(R"(\b(?:first|from)\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
        static const std::regex accept_regex(R"(\b(?:accept|when)\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
        static const std::regex if_regex(R"(\bif\s+([A-Za-z0-9_!&\|\(\)\s]+?)(?=\s+(?:do|then|to|;|$)))",
                                         std::regex::optimize);
        static const std::regex do_regex(R"(\bdo\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
        static const std::regex then_regex(R"(\b(?:then|to)\s+([A-Za-z_][A-Za-z0-9_\[\]\*]*))", std::regex::optimize);

        std::smatch match;
        if (std::regex_search(stmt, match, first_regex)) {
            source = sanitize_identifier(match[1].str());
        } else if (!state_stack.empty()) {
            source = state_stack.back();
        }

        if (std::regex_search(stmt, match, accept_regex)) {
            event = sanitize_identifier(match[1].str());
        }

        if (std::regex_search(stmt, match, if_regex)) {
            auto parsed = GuardExpressionParser::parse(match[1].str());
            if (!parsed.cpp_type.empty()) {
                guard = parsed.cpp_type;
                for (const auto& atomic : parsed.atomic_guards) {
                    model.add_guard(atomic);
                }
            }
        }

        if (std::regex_search(stmt, match, do_regex)) {
            action = sanitize_identifier(match[1].str());
        }

        bool target_is_history = false;
        bool target_is_deep_history = false;

        if (std::regex_search(stmt, match, then_regex)) {
            const std::string raw_target = match[1].str();
            if (raw_target.find("[H*]") != std::string::npos ||
                raw_target.find("[deep_history]") != std::string::npos) {
                target_is_deep_history = true;
                target_is_history = true;
                target = sanitize_identifier(raw_target.substr(0, raw_target.find('[')));
            } else if (raw_target.find("[H]") != std::string::npos ||
                       raw_target.find("[history]") != std::string::npos) {
                target_is_history = true;
                target = sanitize_identifier(raw_target.substr(0, raw_target.find('[')));
            } else {
                target = sanitize_identifier(raw_target);
            }
        }

        if (source.empty() && !target.empty()) {
            source = target;
        }
        if (target.empty() && !source.empty()) {
            target = source;  // internal transition
        }

        if (source.empty() || target.empty()) {
            return true;
        }

        TransitionEdge trans;
        trans.source = source;
        trans.target = target;
        trans.event = event;
        if (!guard.empty()) {
            trans.guard = guard;
        }
        if (!action.empty()) {
            trans.action = action;
        }
        trans.target_is_history = target_is_history;
        trans.target_is_deep_history = target_is_deep_history;

        if (source == target && !event.empty() && stmt.find("then") == std::string::npos &&
            stmt.find("to") == std::string::npos) {
            trans.kind = TransitionEdgeKind::Internal;
        } else {
            trans.kind = TransitionEdgeKind::External;
        }

        if (!model.is_choice_node(source)) {
            model.add_state(source);
        }
        if (!model.is_choice_node(target)) {
            model.add_state(target);
            if (target_is_history) {
                auto* target_state = model.find_state_mut(target);
                if (target_state != nullptr) {
                    target_state->has_history = true;
                    if (target_is_deep_history) {
                        target_state->has_deep_history = true;
                    }
                }
            }
        }

        if (!event.empty()) {
            model.add_event(event);
        }
        if (!action.empty()) {
            model.add_action(action);
        }

        model.add_transition(std::move(trans));
        return true;
    }
};

}  // namespace fsm::codegen

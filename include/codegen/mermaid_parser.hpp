#pragma once

#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "guard_parser.hpp"
#include "parser_interface.hpp"

namespace fsm::codegen {

class MermaidParser : public IParser {
  public:
    bool parse(std::string_view content, FsmModel& out_model, std::string& out_error) override {
        std::istringstream stream(std::string{content});
        std::string line;
        size_t line_num = 0;
        std::vector<std::string> parent_stack;

        while (std::getline(stream, line)) {
            ++line_num;
            const std::string_view trimmed = trim(line);

            if (trimmed.empty()) {
                continue;
            }
            if (starts_with(trimmed, "%%")) {
                continue;  // Mermaid comment
            }
            if (starts_with(trimmed, "stateDiagram") || starts_with(trimmed, "stateDiagram-v2")) {
                continue;
            }

            // Block closing: }
            if (trimmed == "}") {
                if (!parent_stack.empty()) {
                    parent_stack.pop_back();
                }
                continue;
            }

            // Choice pseudostate: state ChoiceName <<choice>>
            if (starts_with(trimmed, "state ") && (trimmed.find("<<choice>>") != std::string_view::npos ||
                                                   trimmed.find("<<junction>>") != std::string_view::npos)) {
                parse_choice_definition(trimmed, out_model);
                continue;
            }

            // Composite state block opening: state CompositeName {
            if (starts_with(trimmed, "state ") && trimmed.back() == '{') {
                const std::string parent_name = parse_composite_state_header(trimmed, out_model, parent_stack);
                if (!parent_name.empty()) {
                    parent_stack.push_back(parent_name);
                }
                continue;
            }

            // Parse state descriptions: state "Description" as StateName
            if (starts_with(trimmed, "state ")) {
                parse_state_definition(trimmed, out_model, parent_stack);
                continue;
            }

            // Deferred events: StateName : defer EventName
            if (trimmed.find(": defer ") != std::string_view::npos ||
                trimmed.find(":defer ") != std::string_view::npos) {
                parse_deferred_event(trimmed, out_model);
                continue;
            }

            // Parse transitions: StateA --> StateB : Event [Guard] / Action
            if (trimmed.find("-->") != std::string_view::npos) {
                if (!parse_transition_line(trimmed, out_model, out_error, line_num, parent_stack)) {
                    return false;
                }
                continue;
            }

            // Internal transition lines: StateName : Event [Guard] / Action
            if (trimmed.find(':') != std::string_view::npos && trimmed.find("-->") == std::string_view::npos) {
                parse_internal_transition(trimmed, out_model);
                continue;
            }
        }

        if (out_model.states.empty() && out_model.choice_nodes.empty()) {
            out_error = "No valid states or transitions found in Mermaid diagram.";
            return false;
        }

        return true;
    }

  private:
    static std::string parse_composite_state_header(std::string_view line, FsmModel& model,
                                                    const std::vector<std::string>& parent_stack) {
        static const std::regex composite_regex(R"(state\s+([a-zA-Z0-9_]+)\s*\{)");
        const std::string line_str{line};
        std::smatch match;
        if (std::regex_search(line_str, match, composite_regex)) {
            const std::string name = sanitize_identifier(match[1].str());
            const std::string parent = parent_stack.empty() ? "" : parent_stack.back();
            model.add_state(name, parent);
            if (auto* state = model.find_state_mut(name)) {
                state->is_composite = true;
            }
            return name;
        }
        return "";
    }

    static void parse_choice_definition(std::string_view line, FsmModel& model) {
        static const std::regex choice_regex(R"(state\s+([a-zA-Z0-9_]+)\s*<<(choice|junction)>>)");
        const std::string line_str{line};
        std::smatch match;
        if (std::regex_search(line_str, match, choice_regex)) {
            const std::string choice_name = sanitize_identifier(match[1].str());
            model.add_choice_node(choice_name);
        }
    }

    static void parse_state_definition(std::string_view line, FsmModel& model,
                                       const std::vector<std::string>& parent_stack) {
        const std::string parent = parent_stack.empty() ? "" : parent_stack.back();

        static const std::regex state_alias_regex(R"(state\s+\"([^\"]+)\"\s+as\s+([a-zA-Z0-9_]+))");
        const std::string line_str{line};
        std::smatch match;
        if (std::regex_search(line_str, match, state_alias_regex)) {
            const std::string description = match[1].str();
            const std::string name = sanitize_identifier(match[2].str());
            model.add_state(name, parent);
            if (auto* state = model.find_state_mut(name)) {
                state->description = description;
            }
            return;
        }

        static const std::regex state_simple_regex(R"(state\s+([a-zA-Z0-9_]+))");
        if (std::regex_search(line_str, match, state_simple_regex)) {
            const std::string name = sanitize_identifier(match[1].str());
            model.add_state(name, parent);
        }
    }

    static void parse_deferred_event(std::string_view line, FsmModel& model) {
        const auto colon_pos = line.find(':');
        if (colon_pos == std::string_view::npos) {
            return;
        }
        const std::string state_name = sanitize_identifier(trim(line.substr(0, colon_pos)));
        std::string_view rest = trim(line.substr(colon_pos + 1));

        if (starts_with(rest, "[defer]")) {
            rest = trim(rest.substr(7));
        } else if (starts_with(rest, "defer:") || starts_with(rest, "defer ")) {
            rest = trim(rest.substr(6));
        } else {
            const auto defer_pos = rest.find("defer ");
            if (defer_pos != std::string_view::npos) {
                rest = trim(rest.substr(defer_pos + 6));
            } else {
                return;
            }
        }

        const std::string event_name = sanitize_identifier(rest);
        if (!event_name.empty()) {
            model.add_state(state_name);
            model.add_event(event_name);
            if (auto* state = model.find_state_mut(state_name)) {
                state->deferred_events.push_back(event_name);
            }
        }
    }

    static void parse_internal_transition(std::string_view line, FsmModel& model) {
        const auto colon_pos = line.find(':');
        if (colon_pos == std::string_view::npos) {
            return;
        }

        const std::string state_name = sanitize_identifier(trim(line.substr(0, colon_pos)));
        std::string label = std::string(trim(line.substr(colon_pos + 1)));

        if (state_name.empty() || label.empty()) {
            return;
        }

        std::optional<std::string> action_name;
        const auto slash_pos = label.find('/');
        if (slash_pos != std::string::npos) {
            const std::string act = std::string(trim(label.substr(slash_pos + 1)));
            if (!act.empty()) {
                action_name = sanitize_identifier(act);
            }
            label = label.substr(0, slash_pos);
        }

        std::optional<std::string> guard_name;
        const auto open_bracket = label.find('[');
        const auto close_bracket = label.find(']', open_bracket);
        if (open_bracket != std::string::npos && close_bracket != std::string::npos) {
            const std::string grd = std::string(trim(label.substr(open_bracket + 1, close_bracket - open_bracket - 1)));
            if (!grd.empty()) {
                auto parsed = GuardExpressionParser::parse(grd);
                if (!parsed.cpp_type.empty()) {
                    guard_name = parsed.cpp_type;
                    for (const auto& atomic : parsed.atomic_guards) {
                        model.add_guard(atomic);
                    }
                }
            }
            label = label.substr(0, open_bracket) + label.substr(close_bracket + 1);
        }

        const std::string event_name = sanitize_identifier(trim(label));
        if (event_name.empty()) {
            return;
        }

        model.add_state(state_name);
        model.add_event(event_name);
        if (action_name) {
            model.add_action(*action_name);
        }

        TransitionModel trans;
        trans.source = state_name;
        trans.target = state_name;
        trans.event = event_name;
        trans.guard = guard_name;
        trans.action = action_name;
        trans.kind = TransitionKind::Internal;

        model.add_transition(std::move(trans));
    }

    static bool parse_transition_line(std::string_view line, FsmModel& model, std::string& out_error, size_t line_num,
                                      const std::vector<std::string>& parent_stack) {
        const auto arrow_pos = line.find("-->");
        if (arrow_pos == std::string_view::npos) {
            return false;
        }

        const std::string_view src_part = trim(line.substr(0, arrow_pos));
        const std::string_view rest = trim(line.substr(arrow_pos + 3));

        std::string_view dst_part;
        std::string_view label_part;

        const auto colon_pos = rest.find(':');
        if (colon_pos != std::string_view::npos) {
            dst_part = trim(rest.substr(0, colon_pos));
            label_part = trim(rest.substr(colon_pos + 1));
        } else {
            dst_part = trim(rest);
        }

        const std::string current_parent = parent_stack.empty() ? "" : parent_stack.back();

        // Check for initial state [*] --> InitialState
        if (src_part == "[*]") {
            std::string dst = sanitize_identifier(dst_part);
            if (!dst.empty() && dst_part != "[*]") {
                if (!current_parent.empty()) {
                    if (auto* parent = model.find_state_mut(current_parent)) {
                        parent->initial_sub_state = dst;
                    }
                    model.add_state(dst, current_parent);
                } else {
                    model.initial_state = dst;
                    model.add_state(dst);
                }
            }
            return true;
        }

        const std::string src = sanitize_identifier(src_part);

        // Check for history pseudo-target: State[H] or State[H*]
        bool is_history = false;
        bool is_deep_history = false;
        std::string dst_raw{dst_part};

        if (ends_with(dst_raw, "[H*]")) {
            is_deep_history = true;
            is_history = true;
            dst_raw = dst_raw.substr(0, dst_raw.size() - 4);
        } else if (ends_with(dst_raw, "[H]")) {
            is_history = true;
            dst_raw = dst_raw.substr(0, dst_raw.size() - 3);
        }

        std::string dst = sanitize_identifier(dst_raw);

        // Check for final state State --> [*]
        if (dst_part == "[*]") {
            dst = "Final";
        }

        if (src.empty() || dst.empty()) {
            out_error = "Error at line " + std::to_string(line_num) +
                        ": invalid source or target state in transition: " + std::string{line};
            return false;
        }

        // Parse Event, Guard, Action from label:
        // Format: EventName [GuardName] / ActionName
        std::string event_name = "AnonymousEvent";
        std::optional<std::string> guard_name;
        std::optional<std::string> action_name;

        if (!label_part.empty()) {
            std::string label{label_part};

            // Check for Action: / ActionName
            const auto slash_pos = label.find('/');
            if (slash_pos != std::string::npos) {
                const std::string act = std::string(trim(label.substr(slash_pos + 1)));
                if (!act.empty()) {
                    action_name = sanitize_identifier(act);
                }
                label = label.substr(0, slash_pos);
            }

            // Check for Guard: [GuardName]
            const auto open_bracket = label.find('[');
            const auto close_bracket = label.find(']', open_bracket);
            if (open_bracket != std::string::npos && close_bracket != std::string::npos) {
                const std::string grd =
                    std::string(trim(label.substr(open_bracket + 1, close_bracket - open_bracket - 1)));
                if (!grd.empty()) {
                    auto parsed = GuardExpressionParser::parse(grd);
                    if (!parsed.cpp_type.empty()) {
                        guard_name = parsed.cpp_type;
                        for (const auto& atomic : parsed.atomic_guards) {
                            model.add_guard(atomic);
                        }
                    }
                }
                label = label.substr(0, open_bracket) + label.substr(close_bracket + 1);
            }

            // Remaining is EventName
            const std::string evt = std::string(trim(label));
            if (!evt.empty()) {
                event_name = sanitize_identifier(evt);
            }
        }

        if (!model.is_choice_node(src)) {
            model.add_state(src, current_parent);
        }
        if (!model.is_choice_node(dst)) {
            model.add_state(dst, current_parent);
            if (is_history) {
                if (auto* state = model.find_state_mut(dst)) {
                    state->has_history = true;
                    if (is_deep_history) {
                        state->has_deep_history = true;
                    }
                }
            }
        }

        model.add_event(event_name);
        if (action_name) {
            model.add_action(*action_name);
        }

        TransitionModel trans;
        trans.source = src;
        trans.target = dst;
        trans.event = event_name;
        trans.guard = guard_name;
        trans.action = action_name;
        trans.kind = TransitionKind::External;
        trans.target_is_history = is_history;
        trans.target_is_deep_history = is_deep_history;

        model.add_transition(std::move(trans));

        if (model.initial_state.empty() && !model.is_choice_node(src)) {
            model.initial_state = src;
        }

        return true;
    }
};

}  // namespace fsm::codegen

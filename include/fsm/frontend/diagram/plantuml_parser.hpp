#pragma once

#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/frontend/common/parser_interface.hpp"
#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/frontend/directive/guard_parser.hpp"

namespace fsm::frontend::diagram {

class PlantUmlParser : public IParser {
  public:
    [[nodiscard]] FrontendKind kind() const noexcept override { return FrontendKind::Diagram; }
    [[nodiscard]] std::string_view format_name() const noexcept override { return "plantuml"; }

    bool parse(std::string_view content, FsmIr& out_model, std::string& out_error) override {
        std::istringstream stream(std::string{content});
        std::string line;
        size_t line_num = 0;
        bool in_block_comment = false;
        std::vector<std::string> parent_stack;

        while (std::getline(stream, line)) {
            ++line_num;
            const std::string_view trimmed = trim(line);

            if (trimmed.empty()) {
                continue;
            }

            if (starts_with(trimmed, "/'")) {
                in_block_comment = true;
            }
            if (in_block_comment) {
                if (trimmed.find("'/") != std::string_view::npos) {
                    in_block_comment = false;
                }
                continue;
            }

            if (DirectiveParser::is_directive(trimmed)) {
                std::string body = DirectiveParser::extract_directive_body(trimmed);
                if (DirectiveParser::parse_model_directive(body, out_model)) {
                    continue;
                } else if (body.rfind("state", 0) == 0) {
                    DirectiveParser::parse_state_directive(body, out_model, parent_stack);
                } else if (!parent_stack.empty()) {
                    auto* st = out_model.find_state_mut(parent_stack.back());
                    if (st != nullptr) {
                        if (body.rfind("defer", 0) == 0) {
                            DirectiveParser::parse_defer_directive(body, *st);
                        }
                    }
                }
                continue;
            }

            if (starts_with(trimmed, "@startuml")) {
                if (trimmed.size() > 9) {
                    std::string cand = std::string(trim(trimmed.substr(9)));
                    if (!cand.empty() && cand.find('(') == std::string::npos && cand.find('\"') == std::string::npos &&
                        cand.find(' ') == std::string::npos) {
                        out_model.name = cand;
                    }
                }
                continue;
            }

            if (starts_with(trimmed, "'") || starts_with(trimmed, "@enduml") || starts_with(trimmed, "title ")) {
                continue;
            }

            if (starts_with(trimmed, "note ") || starts_with(trimmed, "note\t")) {
                continue;
            }

            // Block closing: }
            if (trimmed == "}") {
                if (!parent_stack.empty()) {
                    parent_stack.pop_back();
                }
                continue;
            }

            // Pseudostates: state Name <<choice|junction|fork|join|entryPoint|exitPoint>>
            if (starts_with(trimmed, "state ") && trimmed.find("<<") != std::string_view::npos) {
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

            // State alias definitions: state "Description" as StateName
            if (starts_with(trimmed, "state ")) {
                parse_state_definition(trimmed, out_model, parent_stack);
                continue;
            }

            // Deferred events: StateName : defer EventName
            if (trimmed.find(": defer ") != std::string_view::npos ||
                trimmed.find(":defer ") != std::string_view::npos) {
                parse_deferred_event(trimmed, out_model, parent_stack);
                continue;
            }

            // Transition lines: Source --> Target or Source -> Target
            if (trimmed.find("->") != std::string_view::npos) {
                if (!parse_transition_line(trimmed, out_model, out_error, line_num, parent_stack)) {
                    return false;
                }
                continue;
            }

            // Internal transition lines: StateName : Event [Guard] / Action
            if (trimmed.find(':') != std::string_view::npos && trimmed.find("->") == std::string_view::npos) {
                parse_internal_transition(trimmed, out_model, parent_stack);
                continue;
            }
        }

        if (out_model.states.empty() && out_model.choice_nodes.empty()) {
            out_error = "No valid states or transitions found in PlantUML diagram.";
            return false;
        }

        out_model.normalize_hierarchy();
        return true;
    }

  private:
    static std::string parse_composite_state_header(std::string_view line, FsmIr& model,
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

    static void parse_choice_definition(std::string_view line, FsmIr& model) {
        static const std::regex choice_regex(
            R"(state\s+([a-zA-Z0-9_]+)\s*<<(choice|junction|fork|join|entryPoint|exitPoint)>>)");
        const std::string line_str{line};
        std::smatch match;
        if (std::regex_search(line_str, match, choice_regex)) {
            const std::string name = sanitize_identifier(match[1].str());
            const std::string kind_str = match[2].str();
            if (kind_str == "choice" || kind_str == "junction") {
                model.add_choice_node(name);
            } else if (kind_str == "entryPoint") {
                model.add_or_get_state(name, "", StateKind::EntryPoint);
            } else if (kind_str == "exitPoint") {
                model.add_or_get_state(name, "", StateKind::ExitPoint);
            } else if (kind_str == "fork") {
                model.add_or_get_state(name, "", StateKind::Fork);
            } else if (kind_str == "join") {
                model.add_or_get_state(name, "", StateKind::Join);
            }
        }
    }

    static void parse_state_definition(std::string_view line, FsmIr& model,
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

    static void parse_deferred_event(std::string_view line, FsmIr& model,
                                     const std::vector<std::string>& parent_stack) {
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
            const std::string current_parent = parent_stack.empty() ? "" : parent_stack.back();
            model.add_state(state_name, current_parent);
            model.add_event(event_name);
            if (auto* state = model.find_state_mut(state_name)) {
                state->deferred_events.push_back(event_name);
            }
        }
    }

    static void parse_internal_transition(std::string_view line, FsmIr& model,
                                          const std::vector<std::string>& parent_stack) {
        const auto colon_pos = line.find(':');
        if (colon_pos == std::string_view::npos) {
            return;
        }

        const std::string state_name = sanitize_identifier(trim(line.substr(0, colon_pos)));
        std::string label = std::string(trim(line.substr(colon_pos + 1)));

        if (state_name.empty() || label.empty()) {
            return;
        }

        std::string parent_for_state;
        if (!parent_stack.empty()) {
            if (parent_stack.back() == state_name) {
                parent_for_state = (parent_stack.size() >= 2) ? parent_stack[parent_stack.size() - 2] : "";
            } else {
                parent_for_state = parent_stack.back();
            }
        }
        const std::string trans_scope = parent_stack.empty() ? "" : parent_stack.back();

        // Check for invariant / stay_duration directives
        if (starts_with(label, "invariant ") || starts_with(label, "time_invariant ") || starts_with(label, "stay ") ||
            starts_with(label, "stay_duration ")) {
            size_t sp = label.find(' ');
            std::string inv_body = std::string(trim(label.substr(sp + 1)));
            model.add_state(state_name, parent_for_state);
            if (auto* st = model.find_state_mut(state_name)) {
                st->time_invariant = inv_body;
            }
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

        model.add_state(state_name, parent_for_state);

        // Native PlantUML lifecycle hooks: entry, exit, do
        if (event_name == "entry" && action_name) {
            model.add_action(*action_name);
            if (auto* st = model.find_state_mut(state_name)) {
                st->entry_actions.push_back(ActionSignature{*action_name});
            }
            return;
        }
        if (event_name == "exit" && action_name) {
            model.add_action(*action_name);
            if (auto* st = model.find_state_mut(state_name)) {
                st->exit_actions.push_back(ActionSignature{*action_name});
            }
            return;
        }
        if (event_name == "do" && action_name) {
            if (auto* st = model.find_state_mut(state_name)) {
                st->do_activity = *action_name;
            }
            return;
        }

        model.add_event(event_name);
        if (action_name) {
            model.add_action(*action_name);
        }

        TransitionEdge trans;
        trans.source = state_name;
        trans.target = state_name;
        trans.event = event_name;
        trans.guard = guard_name;
        trans.action = action_name;
        trans.kind = TransitionEdgeKind::Internal;
        trans.parent_scope = trans_scope;

        model.add_transition(std::move(trans));
    }

    static bool parse_transition_line(std::string_view line, FsmIr& model, std::string& out_error, size_t line_num,
                                      const std::vector<std::string>& parent_stack) {
        size_t arrow_pos = line.find("-->");
        size_t arrow_len = 3;
        if (arrow_pos == std::string_view::npos) {
            arrow_pos = line.find("->");
            arrow_len = 2;
        }
        if (arrow_pos == std::string_view::npos) {
            return false;
        }

        const std::string_view src_part = trim(line.substr(0, arrow_pos));
        const std::string_view rest = trim(line.substr(arrow_pos + arrow_len));

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

        // Check for initial state [*] -> InitialState
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

        // Check for final state State -> [*]
        if (dst_part == "[*]") {
            dst = "Final";
        }

        if (src.empty() || dst.empty()) {
            out_error = "Error at line " + std::to_string(line_num) +
                        ": invalid source or target in transition: " + std::string{line};
            return false;
        }

        // Parse Event, Guard, Action, Priority from label:
        // Format: EventName (prio=1) [GuardName] / ActionName
        std::string event_name;
        std::optional<std::string> guard_name;
        std::optional<std::string> action_name;
        std::uint32_t priority = 0;

        if (!label_part.empty()) {
            std::string label{label_part};

            // Check for Priority: (prio=N) or [prio=N] or (priority=N) or [priority=N]
            static const std::regex prio_regex(R"(\[(?:prio|priority)=(\d+)\]|\((?:prio|priority)=(\d+)\))");
            std::smatch prio_match;
            if (std::regex_search(label, prio_match, prio_regex)) {
                std::string p_str = prio_match[1].matched ? prio_match[1].str() : prio_match[2].str();
                try {
                    priority = static_cast<std::uint32_t>(std::stoul(p_str));
                } catch (...) {
                }
                label = std::regex_replace(label, prio_regex, "");
            }

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
            const auto close_bracket = label.find(']');
            if (open_bracket != std::string::npos && close_bracket != std::string::npos &&
                close_bracket > open_bracket) {
                std::string raw_guard = label.substr(open_bracket + 1, close_bracket - open_bracket - 1);
                auto parsed = GuardExpressionParser::parse(raw_guard);
                if (!parsed.cpp_type.empty()) {
                    guard_name = parsed.cpp_type;
                    for (const auto& atomic : parsed.atomic_guards) {
                        model.add_guard(atomic);
                    }
                } else {
                    std::string g = std::string(trim(raw_guard));
                    if (!g.empty()) {
                        guard_name = sanitize_identifier(g);
                        model.add_guard(*guard_name);
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

        if (!event_name.empty()) {
            model.add_event(event_name);
        }
        if (action_name) {
            model.add_action(*action_name);
        }

        TransitionEdge trans;
        trans.source = src;
        trans.target = dst;
        trans.event = event_name;
        trans.guard = guard_name;
        trans.action = action_name;
        trans.kind = TransitionEdgeKind::External;
        trans.target_is_history = is_history;
        trans.target_is_deep_history = is_deep_history;
        trans.parent_scope = current_parent;
        trans.priority = priority;

        model.add_transition(std::move(trans));

        if (model.initial_state.empty() && !model.is_choice_node(src)) {
            model.initial_state = src;
        }

        return true;
    }
};

}  // namespace fsm::frontend::diagram

namespace fsm::frontend {
using diagram::PlantUmlParser;
}  // namespace fsm::frontend

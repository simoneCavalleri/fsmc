#pragma once

#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/frontend/directive_parser.hpp"
#include "fsm/frontend/guard_parser.hpp"
#include "fsm/frontend/parser_interface.hpp"

namespace fsm::codegen {

class MermaidParser : public IParser {
  public:
    [[nodiscard]] FrontendKind kind() const noexcept override { return FrontendKind::Diagram; }
    [[nodiscard]] std::string_view format_name() const noexcept override { return "mermaid"; }

    bool parse(std::string_view content, FsmIr& out_model, std::string& out_error) override {
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

            if (DirectiveParser::is_directive(trimmed)) {
                std::string body = DirectiveParser::extract_directive_body(trimmed);
                if (body.rfind("var", 0) == 0 || body.rfind("variable", 0) == 0) {
                    if (auto var = DirectiveParser::parse_variable_directive(body)) {
                        out_model.add_variable(std::move(*var));
                    }
                } else if (body.rfind("property", 0) == 0) {
                    if (auto prop = DirectiveParser::parse_property_directive(body)) {
                        out_model.add_property(std::move(*prop));
                    }
                } else if (body.rfind("signal", 0) == 0) {
                    if (auto sig = DirectiveParser::parse_signal_directive(body)) {
                        out_model.add_signal(std::move(*sig));
                    }
                } else if (body.rfind("state", 0) == 0) {
                    // Check if state name is provided
                    auto n_pos = body.find("name=");
                    if (n_pos != std::string::npos) {
                        std::string state_name = DirectiveParser::extract_directive_body(body);
                        auto eq_pos = body.find('=', n_pos);
                        auto sp_pos = body.find_first_of(" \t", eq_pos + 1);
                        std::string sname = body.substr(
                            eq_pos + 1, sp_pos == std::string::npos ? std::string::npos : (sp_pos - eq_pos - 1));
                        if (!sname.empty() && sname.front() == '"' && sname.back() == '"') {
                            sname = sname.substr(1, sname.size() - 2);
                        }
                        auto& st = out_model.add_or_get_state(sname);
                        DirectiveParser::parse_state_directive(body, st);
                    } else if (!parent_stack.empty()) {
                        if (auto* st = out_model.find_state_mut(parent_stack.back())) {
                            DirectiveParser::parse_state_directive(body, *st);
                        }
                    }
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

            // Note block parsing for composite/leaf states (single-line or multi-line)
            if (starts_with(trimmed, "note right of ") || starts_with(trimmed, "note left of ") ||
                starts_with(trimmed, "note ")) {
                size_t p = trimmed.find(" of ");
                std::string target_state;
                size_t colon_pos = trimmed.find(':');
                if (p != std::string_view::npos) {
                    size_t len = (colon_pos != std::string_view::npos && colon_pos > p + 4) ? (colon_pos - (p + 4))
                                                                                            : std::string_view::npos;
                    target_state = sanitize_identifier(trim(trimmed.substr(p + 4, len)));
                }

                auto parse_note_content = [&](std::string_view note_line) {
                    if (target_state.empty()) {
                        return;
                    }
                    auto* st = out_model.find_state_mut(target_state);
                    if (st == nullptr) {
                        return;
                    }
                    std::string line_s(note_line);
                    std::stringstream ss(line_s);
                    std::string segment;
                    while (std::getline(ss, segment, ',')) {
                        std::string_view seg = trim(segment);
                        if (seg.find("entry /") != std::string_view::npos) {
                            size_t ap = seg.find("entry /");
                            std::string act = sanitize_identifier(trim(seg.substr(ap + 7)));
                            if (!act.empty()) {
                                out_model.add_action(act);
                                st->entry_actions.push_back(ActionSignature{act});
                            }
                        } else if (seg.find("exit /") != std::string_view::npos) {
                            size_t ap = seg.find("exit /");
                            std::string act = sanitize_identifier(trim(seg.substr(ap + 6)));
                            if (!act.empty()) {
                                out_model.add_action(act);
                                st->exit_actions.push_back(ActionSignature{act});
                            }
                        } else if (seg.find("do /") != std::string_view::npos) {
                            size_t ap = seg.find("do /");
                            std::string act = sanitize_identifier(trim(seg.substr(ap + 4)));
                            if (!act.empty()) {
                                st->do_activity = act;
                            }
                        } else if (seg.find("defer ") != std::string_view::npos) {
                            size_t ap = seg.find("defer ");
                            std::string evt = sanitize_identifier(trim(seg.substr(ap + 6)));
                            if (!evt.empty()) {
                                out_model.add_event(evt);
                                st->deferred_events.push_back(evt);
                            }
                        }
                    }
                };

                if (colon_pos != std::string_view::npos) {
                    parse_note_content(trimmed.substr(colon_pos + 1));
                } else {
                    while (std::getline(stream, line)) {
                        std::string_view note_line = trim(line);
                        if (note_line == "end note" || starts_with(note_line, "end note") || note_line == "endnote") {
                            break;
                        }
                        parse_note_content(note_line);
                    }
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

            // Parse state descriptions: state "Description" as StateName
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

            // Parse transitions: StateA --> StateB : Event [Guard] / Action
            if (trimmed.find("-->") != std::string_view::npos) {
                if (!parse_transition_line(trimmed, out_model, out_error, line_num, parent_stack)) {
                    return false;
                }
                continue;
            }

            // Internal transition lines: StateName : Event [Guard] / Action
            if (trimmed.find(':') != std::string_view::npos && trimmed.find("-->") == std::string_view::npos) {
                parse_internal_transition(trimmed, out_model, parent_stack);
                continue;
            }
        }

        if (out_model.states.empty() && out_model.choice_nodes.empty()) {
            out_error = "No valid states or transitions found in Mermaid diagram.";
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

                std::string text = description;
                static const std::regex br_regex(R"(<br\s*/?>|<hr\s*/?>|\\n)", std::regex::icase);
                text = std::regex_replace(text, br_regex, "\n");
                static const std::regex tag_regex(R"(<[^>]+>)");
                text = std::regex_replace(text, tag_regex, "");
                std::istringstream iss(text);
                std::string item;
                while (std::getline(iss, item)) {
                    item = trim(item);
                    if (item.empty())
                        continue;
                    if (item.find("entry /") != std::string::npos) {
                        size_t p = item.find("entry /");
                        size_t end_p = item.find(')', p);
                        std::string act =
                            item.substr(p + 7, (end_p != std::string::npos ? end_p : item.size()) - (p + 7));
                        act = sanitize_identifier(trim(act));
                        if (!act.empty()) {
                            model.add_action(act);
                            state->entry_actions.push_back(ActionSignature{act});
                        }
                    } else if (item.find("exit /") != std::string::npos) {
                        size_t p = item.find("exit /");
                        size_t end_p = item.find(')', p);
                        std::string act =
                            item.substr(p + 6, (end_p != std::string::npos ? end_p : item.size()) - (p + 6));
                        act = sanitize_identifier(trim(act));
                        if (!act.empty()) {
                            model.add_action(act);
                            state->exit_actions.push_back(ActionSignature{act});
                        }
                    } else if (item.find("do /") != std::string::npos) {
                        size_t p = item.find("do /");
                        size_t end_p = item.find(')', p);
                        std::string act =
                            item.substr(p + 4, (end_p != std::string::npos ? end_p : item.size()) - (p + 4));
                        act = sanitize_identifier(trim(act));
                        if (!act.empty()) {
                            state->do_activity = act;
                        }
                    } else if (item.find("defer ") != std::string::npos) {
                        size_t p = item.find("defer ");
                        size_t end_p = item.find(')', p);
                        std::string evt =
                            item.substr(p + 6, (end_p != std::string::npos ? end_p : item.size()) - (p + 6));
                        evt = sanitize_identifier(trim(evt));
                        if (!evt.empty()) {
                            model.add_event(evt);
                            state->deferred_events.push_back(evt);
                        }
                    }
                }
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

        const std::string current_parent = parent_stack.empty() ? "" : parent_stack.back();
        model.add_state(state_name, current_parent);

        // Native Mermaid lifecycle hooks: entry, exit
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
        trans.parent_scope = current_parent;

        model.add_transition(std::move(trans));
    }

    static bool parse_transition_line(std::string_view line, FsmIr& model, std::string& out_error, size_t line_num,
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

        // Parse Event, Guard, Action, Priority from label:
        // Format: EventName (prio=1) [GuardName] / ActionName
        std::string event_name = "AnonymousEvent";
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

}  // namespace fsm::codegen

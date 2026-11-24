#include "fsm/frontend/diagram/plantuml_parser.hpp"

#include <regex>
#include <sstream>

#include "fsm/frontend/diagram/diagram_action_parser.hpp"
#include "fsm/frontend/diagram/diagram_parser_common.hpp"
#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/frontend/directive/guard_parser.hpp"

namespace fsm::frontend::diagram {

using namespace fsm::ir;
using directive::DirectiveParser;

bool PlantUmlParser::parse(std::string_view content, FsmIr& out_ir, diagnostic::DiagnosticEngine& diag) {
    std::string err;
    if (!parse_impl(content, out_ir, err)) {
        report_parse_error(diag, "E_PARSE_PLANTUML", err);
        return false;
    }
    return true;
}

bool PlantUmlParser::parse_impl(std::string_view content, FsmIr& out_model, std::string& out_error) {
    std::istringstream stream(std::string{content});
    std::string line;
    size_t line_num = 0;
    bool in_block_comment = false;
    bool pending_local_transition = false;
    std::vector<std::string> parent_stack;
    std::vector<size_t> scope_state_start_idx;
    std::vector<std::vector<size_t>> scope_region_splits;
    std::vector<bool> scope_is_parallel;
    std::string multiline_buffer;
    int multiline_brace_imbalance = 0;

    while (std::getline(stream, line)) {
        ++line_num;

        if (multiline_brace_imbalance > 0) {
            multiline_buffer += "\n" + line;
            multiline_brace_imbalance += DiagramActionParser::brace_imbalance(line);
            if (multiline_brace_imbalance <= 0) {
                multiline_brace_imbalance = 0;
                line = multiline_buffer;
                multiline_buffer.clear();
            } else {
                continue;
            }
        }

        const std::string_view trimmed_initial = trim(line);
        if (trimmed_initial.empty()) {
            continue;
        }

        if (starts_with(trimmed_initial, "/'")) {
            in_block_comment = true;
        }
        if (in_block_comment) {
            if (trimmed_initial.find("'/") != std::string_view::npos) {
                in_block_comment = false;
            }
            continue;
        }

        // If line contains an action with open unclosed braces, start multiline accumulation
        if (!starts_with(trimmed_initial, "state ") &&
            DiagramActionParser::find_action_slash(trimmed_initial) != std::string_view::npos) {
            int imb = DiagramActionParser::brace_imbalance(trimmed_initial);
            if (imb > 0) {
                multiline_buffer = line;
                multiline_brace_imbalance = imb;
                continue;
            }
        }

        std::string processed_line{trimmed_initial};
        if (!parent_stack.empty() &&
            (starts_with(trimmed_initial, "entry /") || starts_with(trimmed_initial, "entry/") ||
             starts_with(trimmed_initial, "exit /") || starts_with(trimmed_initial, "exit/") ||
             starts_with(trimmed_initial, "do /") || starts_with(trimmed_initial, "do/"))) {
            processed_line = parent_stack.back() + " : " + std::string(trimmed_initial);
        }
        const std::string_view trimmed = trim(processed_line);

        if (DirectiveParser::is_directive(trimmed)) {
            std::string body = DirectiveParser::extract_directive_body(trimmed);
            if (body == "local") {
                pending_local_transition = true;
                continue;
            }
            if (DirectiveParser::parse_model_directive(body, out_model)) {
                continue;
            } else if (body.rfind("state", 0) == 0) {
                DirectiveParser::parse_state_directive(body, out_model, parent_stack);
            } else {
                std::string target_state;
                auto fsm_pos = trimmed.find("@fsm:");
                auto colon_pos = trimmed.find(':');
                if (colon_pos != std::string_view::npos && colon_pos < fsm_pos) {
                    std::string prefix = std::string(trim(trimmed.substr(0, colon_pos)));
                    if (prefix.rfind("state ", 0) == 0) {
                        prefix = std::string(trim(prefix.substr(6)));
                    }
                    target_state = prefix;
                } else if (!parent_stack.empty()) {
                    target_state = parent_stack.back();
                }

                if (!target_state.empty()) {
                    auto* st = out_model.find_state_mut(target_state);
                    if (st == nullptr) {
                        std::string parent_for_state = parent_stack.empty() ? "" : parent_stack.back();
                        if (parent_for_state == target_state) {
                            parent_for_state = "";
                        }
                        st = &out_model.add_state(target_state, parent_for_state);
                    }
                    if (body.rfind("defer", 0) == 0) {
                        DirectiveParser::parse_defer_directive(body, *st);
                    } else if (body.rfind("submachine", 0) == 0) {
                        DirectiveParser::parse_submachine_directive(body, *st);
                    } else {
                        DirectiveParser::parse_state_directive(body, *st);
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

        if (starts_with(trimmed, "note ") || starts_with(trimmed, "note\t") || trimmed == "note") {
            const bool is_inline_note =
                (trimmed.find(':') != std::string_view::npos) ||
                (starts_with(trimmed, "note \"") && trimmed.find(" as ") != std::string_view::npos);
            if (!is_inline_note) {
                while (std::getline(stream, line)) {
                    ++line_num;
                    std::string_view note_line = trim(line);
                    if (note_line == "end note" || note_line == "endnote" || starts_with(note_line, "end note ") ||
                        starts_with(note_line, "endnote ")) {
                        break;
                    }
                }
            }
            continue;
        }

        // Orthogonal region separator: -- or ||
        if ((trimmed == "--" || trimmed == "||") && !parent_stack.empty()) {
            scope_is_parallel.back() = true;
            scope_region_splits.back().push_back(out_model.states.size());
            continue;
        }

        // Block closing: }
        if (trimmed == "}") {
            if (!parent_stack.empty()) {
                const std::string closing_parent = parent_stack.back();
                const size_t start_idx = scope_state_start_idx.back();
                const auto splits = scope_region_splits.back();
                const bool is_par = scope_is_parallel.back();
                parent_stack.pop_back();
                scope_state_start_idx.pop_back();
                scope_region_splits.pop_back();
                scope_is_parallel.pop_back();

                if (is_par) {
                    std::vector<std::pair<size_t, size_t>> ranges;
                    size_t prev = start_idx;
                    for (size_t sp : splits) {
                        ranges.emplace_back(prev, sp);
                        prev = sp;
                    }
                    ranges.emplace_back(prev, out_model.states.size());

                    std::vector<OrthogonalRegion> regions;
                    for (size_t r_idx = 0; r_idx < ranges.size(); ++r_idx) {
                        const auto [r_begin, r_end] = ranges[r_idx];
                        std::vector<std::string> direct_children;
                        for (size_t si = r_begin; si < r_end && si < out_model.states.size(); ++si) {
                            if (out_model.states[si].parent_state == closing_parent) {
                                direct_children.push_back(out_model.states[si].name);
                            }
                        }
                        if (direct_children.empty()) {
                            continue;
                        }
                        if (direct_children.size() == 1) {
                            const auto* child = out_model.find_state(direct_children[0]);
                            if (child != nullptr && child->is_composite) {
                                OrthogonalRegion reg;
                                reg.id = child->name;
                                reg.name = child->name;
                                reg.initial_state_id = child->initial_sub_state;
                                regions.push_back(std::move(reg));
                                continue;
                            }
                        }
                        OrthogonalRegion reg;
                        reg.id = closing_parent + "_Region" + std::to_string(r_idx + 1);
                        reg.name = reg.id;
                        reg.initial_state_id = direct_children.front();
                        reg.state_ids = std::move(direct_children);
                        regions.push_back(std::move(reg));
                    }
                    if (auto* parent = out_model.find_state_mut(closing_parent)) {
                        parent->kind = StateKind::Parallel;
                        parent->is_composite = true;
                        parent->orthogonal_regions = std::move(regions);
                        for (const auto& reg : parent->orthogonal_regions) {
                            for (auto& st : out_model.states) {
                                if (st.parent_state == reg.id || st.parent_state == reg.name ||
                                    std::find(reg.state_ids.begin(), reg.state_ids.end(), st.name) !=
                                        reg.state_ids.end()) {
                                    st.pinned_parent = true;
                                }
                            }
                        }
                    }
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
                scope_state_start_idx.push_back(out_model.states.size());
                scope_region_splits.emplace_back();
                scope_is_parallel.push_back(false);
            }
            continue;
        }

        // State alias definitions: state "Description" as StateName
        if (starts_with(trimmed, "state ")) {
            parse_state_definition(trimmed, out_model, parent_stack);
            continue;
        }

        // Deferred events: StateName : defer EventName
        if (trimmed.find(": defer ") != std::string_view::npos || trimmed.find(":defer ") != std::string_view::npos) {
            parse_deferred_event(trimmed, out_model, parent_stack);
            continue;
        }

        // Transition lines: Source --> Target or Source -> Target
        if (trimmed.find("->") != std::string_view::npos) {
            const size_t prev_transitions = out_model.transitions.size();
            if (!parse_transition_line(trimmed, out_model, out_error, line_num, parent_stack)) {
                return false;
            }
            if (pending_local_transition && out_model.transitions.size() > prev_transitions) {
                out_model.transitions.back().kind = TransitionEdgeKind::Local;
                pending_local_transition = false;
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

std::string PlantUmlParser::parse_composite_state_header(std::string_view line, FsmIr& model,
                                                         const std::vector<std::string>& parent_stack) {
    static const std::regex composite_regex(R"regex(state\s+(?:"[^"]*"\s+as\s+)?([a-zA-Z0-9_]+)\s*\{)regex");
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

void PlantUmlParser::parse_choice_definition(std::string_view line, FsmIr& model) {
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

void PlantUmlParser::parse_state_definition(std::string_view line, FsmIr& model,
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

void PlantUmlParser::parse_deferred_event(std::string_view line, FsmIr& model,
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
        std::string current_parent;
        if (!parent_stack.empty()) {
            if (parent_stack.back() == state_name) {
                current_parent = (parent_stack.size() >= 2) ? parent_stack[parent_stack.size() - 2] : "";
            } else {
                current_parent = parent_stack.back();
            }
        }
        model.add_state(state_name, current_parent);
        model.add_event(event_name);
        if (auto* state = model.find_state_mut(state_name)) {
            state->deferred_events.push_back(event_name);
        }
    }
}

void PlantUmlParser::parse_internal_transition(std::string_view line, FsmIr& model,
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

    std::string raw_action;
    const auto slash_pos = DiagramActionParser::find_action_slash(label);
    if (slash_pos != std::string::npos) {
        raw_action = std::string(trim(label.substr(slash_pos + 1)));
        label = label.substr(0, slash_pos);
    }

    std::string raw_guard;
    std::size_t open_bracket = std::string::npos;
    std::size_t close_bracket = std::string::npos;
    if (detail::extract_balanced_square_brackets(label, open_bracket, close_bracket)) {
        raw_guard = std::string(trim(label.substr(open_bracket + 1, close_bracket - open_bracket - 1)));
        label = label.substr(0, open_bracket) + label.substr(close_bracket + 1);
    }

    const std::string raw_event = std::string(trim(label));
    const std::string event_name = sanitize_identifier(raw_event);
    if (event_name.empty()) {
        return;
    }

    std::optional<ActionSignature> parsed_action;
    if (!raw_action.empty()) {
        std::string fallback = "action_" + state_name + "_" + event_name;
        if (raw_event == "entry" || event_name == "entry") {
            fallback = "entry_" + state_name;
        } else if (raw_event == "exit" || event_name == "exit") {
            fallback = "exit_" + state_name;
        }
        parsed_action = DiagramActionParser::parse_action_block(raw_action, fallback);
        detail::disambiguate_synthesized_action(model, *parsed_action);
    }

    model.add_state(state_name, parent_for_state);

    // Native PlantUML lifecycle hooks: entry, exit, do
    if ((raw_event == "entry" || event_name == "entry") && parsed_action) {
        model.add_action(parsed_action->name);
        if (auto* st = model.find_state_mut(state_name)) {
            st->entry_actions.push_back(*parsed_action);
        }
        return;
    }
    if ((raw_event == "exit" || event_name == "exit") && parsed_action) {
        model.add_action(parsed_action->name);
        if (auto* st = model.find_state_mut(state_name)) {
            st->exit_actions.push_back(*parsed_action);
        }
        return;
    }
    if ((raw_event == "do" || event_name == "do" || event_name == "do_") && parsed_action) {
        if (auto* st = model.find_state_mut(state_name)) {
            st->do_activity = parsed_action->name;
        }
        return;
    }

    model.add_event(event_name);
    if (parsed_action) {
        model.add_action(parsed_action->name);
    }

    TransitionEdge trans;
    trans.source = state_name;
    trans.target = state_name;
    trans.event = event_name;
    detail::apply_parsed_guard(model, trans, raw_guard, state_name, state_name);
    if (parsed_action) {
        trans.transition_action = *parsed_action;
    }
    trans.kind = TransitionEdgeKind::Internal;
    trans.parent_scope = trans_scope;

    model.add_transition(std::move(trans));
}

bool PlantUmlParser::parse_transition_line(std::string_view line, FsmIr& model, std::string& out_error, size_t line_num,
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
    if (is_history && dst.empty() && !current_parent.empty()) {
        dst = current_parent;
    }

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
    std::string raw_event_change_expr;
    std::string raw_guard;
    std::optional<ActionSignature> parsed_action;
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
        std::string raw_action;
        const auto slash_pos = DiagramActionParser::find_action_slash(label);
        if (slash_pos != std::string::npos) {
            raw_action = std::string(trim(label.substr(slash_pos + 1)));
            label = label.substr(0, slash_pos);
        }

        // Check for Guard: [GuardName]
        std::size_t open_bracket = std::string::npos;
        std::size_t close_bracket = std::string::npos;
        if (detail::extract_balanced_square_brackets(label, open_bracket, close_bracket)) {
            raw_guard = std::string(trim(label.substr(open_bracket + 1, close_bracket - open_bracket - 1)));
            label = label.substr(0, open_bracket) + label.substr(close_bracket + 1);
        }

        // Remaining is EventName (or ChangeTrigger when(...))
        const std::string evt = std::string(trim(label));
        if (!evt.empty()) {
            static const std::regex when_ev_regex(R"(^when\s*\((.+)\)$)", std::regex::icase);
            std::smatch wm;
            if (std::regex_match(evt, wm, when_ev_regex)) {
                raw_event_change_expr = std::string(trim(wm[1].str()));
            } else {
                event_name = sanitize_identifier(evt);
            }
        }

        if (!raw_action.empty()) {
            const std::string fallback = "action_" + src + "_" + dst + (event_name.empty() ? "" : "_" + event_name);
            parsed_action = DiagramActionParser::parse_action_block(raw_action, fallback);
            detail::disambiguate_synthesized_action(model, *parsed_action);
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
    if (parsed_action) {
        model.add_action(parsed_action->name);
    }

    TransitionEdge trans;
    trans.source = src;
    trans.target = dst;
    trans.event = event_name;
    if (!raw_event_change_expr.empty()) {
        trans.trigger = ChangeTrigger(raw_event_change_expr, true);
    } else if (!event_name.empty()) {
        static const std::regex time_ev_regex(R"(^(after|every)_([0-9]+)(ms|us|s|m)$)", std::regex::icase);
        std::smatch tm;
        if (std::regex_match(event_name, tm, time_ev_regex)) {
            std::string kw = tm[1].str();
            std::transform(kw.begin(), kw.end(), kw.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const TimeTriggerKind tkind = (kw == "every") ? TimeTriggerKind::Every : TimeTriggerKind::After;
            const std::uint64_t val = static_cast<std::uint64_t>(std::stoull(tm[2].str()));
            std::string u = tm[3].str();
            std::transform(u.begin(), u.end(), u.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (u == "us") {
                trans.trigger = TimeTrigger(tkind, val, TimeUnit::Microseconds);
            } else if (u == "s") {
                trans.trigger = TimeTrigger(tkind, val, TimeUnit::Seconds);
            } else if (u == "m") {
                trans.trigger = TimeTrigger(tkind, val, TimeUnit::Minutes);
            } else {
                trans.trigger = TimeTrigger(tkind, val, TimeUnit::Milliseconds);
            }
        }
    }
    detail::apply_parsed_guard(model, trans, raw_guard, src, dst);
    if (parsed_action) {
        trans.transition_action = *parsed_action;
    }
    trans.kind = TransitionEdgeKind::External;
    trans.target_is_history = is_history;
    trans.target_is_deep_history = is_deep_history;
    trans.parent_scope = current_parent;
    trans.priority = priority;

    model.add_transition(std::move(trans));

    return true;
}

}  // namespace fsm::frontend::diagram

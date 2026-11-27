#include "fsm/frontend/diagram/mermaid/mermaid_parser.hpp"

#include <regex>
#include <sstream>

#include "fsm/frontend/diagram/common/diagram_action_parser.hpp"
#include "fsm/frontend/diagram/common/diagram_parser_common.hpp"
#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/frontend/directive/guard_parser.hpp"

namespace fsm::frontend::diagram {

using namespace fsm::ir;
using directive::DirectiveParser;

bool MermaidParser::parse(std::string_view content, FsmIr& out_ir, diagnostic::DiagnosticEngine& diag) {
    std::string err;
    if (!parse_impl(content, out_ir, err)) {
        report_parse_error(diag, "E_PARSE_MERMAID", err);
        return false;
    }
    return true;
}

bool MermaidParser::parse_impl(std::string_view content, FsmIr& out_model, std::string& out_error) {
    std::istringstream stream(std::string{content});
    std::string line;
    size_t line_num = 0;
    bool pending_local_transition = false;
    bool pending_history_transition = false;
    bool pending_deep_history_transition = false;
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

        // YAML frontmatter title / diagram title
        if (starts_with(trimmed, "---") || starts_with(trimmed, "title:")) {
            auto t_pos = trimmed.find("title:");
            if (t_pos != std::string_view::npos) {
                std::string tname = std::string(trim(trimmed.substr(t_pos + 6)));
                auto dash_pos = tname.find("---");
                if (dash_pos != std::string::npos) {
                    tname = std::string(trim(tname.substr(0, dash_pos)));
                }
                if (!tname.empty()) {
                    out_model.name = sanitize_identifier(tname);
                }
            }
            continue;
        }

        if (DirectiveParser::is_directive(trimmed)) {
            std::string body = DirectiveParser::extract_directive_body(trimmed);
            if (body == "local") {
                pending_local_transition = true;
                continue;
            }
            if (body.rfind("history", 0) == 0) {
                pending_history_transition = true;
                pending_deep_history_transition = (body.find("deep") != std::string::npos);
                continue;
            }
            bool handled = false;
            if (body.rfind("name", 0) == 0 || body.rfind("fsm", 0) == 0) {
                auto eq = body.find('=');
                if (eq != std::string::npos) {
                    out_model.name = sanitize_identifier(trim(body.substr(eq + 1)));
                    handled = true;
                }
            } else if (DirectiveParser::parse_model_directive(body, out_model)) {
                handled = true;
            } else if (body.rfind("state", 0) == 0) {
                // Check if state name is provided
                auto n_pos = body.find("name=");
                if (n_pos != std::string::npos) {
                    auto eq_pos = body.find('=', n_pos);
                    auto sp_pos = body.find_first_of(" \t", eq_pos + 1);
                    std::string sname = body.substr(
                        eq_pos + 1, sp_pos == std::string::npos ? std::string::npos : (sp_pos - eq_pos - 1));
                    if (!sname.empty() && sname.front() == '"' && sname.back() == '"') {
                        sname = sname.substr(1, sname.size() - 2);
                    }
                    auto& st = out_model.add_or_get_state(sname);
                    handled = DirectiveParser::parse_state_directive(body, st);
                } else if (!parent_stack.empty()) {
                    if (auto* st = out_model.find_state_mut(parent_stack.back())) {
                        handled = DirectiveParser::parse_state_directive(body, *st);
                    }
                }
            } else if (!parent_stack.empty()) {
                auto* st = out_model.find_state_mut(parent_stack.back());
                if (st != nullptr) {
                    if (body.rfind("defer", 0) == 0) {
                        handled = DirectiveParser::parse_defer_directive(body, *st);
                    }
                }
            }
            if (!handled) {
                out_model.parse_warnings.push_back("Unrecognized or malformed @fsm directive at line " +
                                                   std::to_string(line_num) + ": '@fsm:" + body + "'");
            }
            continue;
        }

        if (starts_with(trimmed, "%%")) {
            continue;  // Mermaid comment
        }
        if (starts_with(trimmed, "stateDiagram") || starts_with(trimmed, "stateDiagram-v2")) {
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
                scope_state_start_idx.push_back(out_model.states.size());
                scope_region_splits.emplace_back();
                scope_is_parallel.push_back(false);
            }
            continue;
        }

        // Parse state descriptions: state "Description" as StateName
        if (starts_with(trimmed, "state ")) {
            parse_state_definition(trimmed, out_model, parent_stack);
            continue;
        }

        // Deferred events: `StateName : defer EventName`, `StateName : defer / EventName`, or UML 2.5 `StateName :
        // EventName / defer`
        if (trimmed.find(": defer ") != std::string_view::npos || trimmed.find(":defer ") != std::string_view::npos ||
            trimmed.find(": defer:") != std::string_view::npos || ends_with(trimmed, "/ defer") ||
            ends_with(trimmed, "/defer")) {
            parse_deferred_event(trimmed, out_model, parent_stack);
            continue;
        }

        // Parse transitions: StateA --> StateB : Event [Guard] / Action
        if (trimmed.find("-->") != std::string_view::npos) {
            const size_t prev_transitions = out_model.transitions.size();
            if (!parse_transition_line(trimmed, out_model, out_error, line_num, parent_stack)) {
                return false;
            }
            if (pending_local_transition && out_model.transitions.size() > prev_transitions) {
                out_model.transitions.back().kind = TransitionEdgeKind::Local;
                pending_local_transition = false;
            }
            if (pending_history_transition && out_model.transitions.size() > prev_transitions) {
                auto& tr = out_model.transitions.back();
                tr.target_is_history = true;
                tr.target_is_deep_history = pending_deep_history_transition;
                if (auto* tgt_st = out_model.find_state_mut(tr.target)) {
                    tgt_st->has_history = true;
                    if (pending_deep_history_transition) {
                        tgt_st->has_deep_history = true;
                    }
                }
                pending_history_transition = false;
                pending_deep_history_transition = false;
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

std::string MermaidParser::parse_composite_state_header(std::string_view line, FsmIr& model,
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
            if (!parent.empty()) {
                state->pinned_parent = true;
            }
        }
        return name;
    }
    return "";
}

void MermaidParser::parse_choice_definition(std::string_view line, FsmIr& model) {
    static const std::regex submachine_regex(R"(state\s+([a-zA-Z0-9_]+)\s*<<submachine\s*:\s*([^>]+)>>)");
    const std::string line_str{line};
    std::smatch match;
    if (std::regex_search(line_str, match, submachine_regex)) {
        const std::string name = sanitize_identifier(match[1].str());
        const std::string target_fsm = std::string(trim(match[2].str()));
        auto& st = model.add_or_get_state(name);
        SubmachineRef ref;
        ref.fsm_name = target_fsm;
        st.submachine = std::move(ref);
        return;
    }
    static const std::regex choice_regex(
        R"(state\s+([a-zA-Z0-9_]+)\s*<<(choice|junction|fork|join|entryPoint|exitPoint)>>)");
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

void MermaidParser::parse_state_definition(std::string_view line, FsmIr& model,
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
            if (!parent.empty()) {
                state->pinned_parent = true;
            }

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
                    std::string act = item.substr(p + 7);
                    auto act_sig = DiagramActionParser::parse_action_block(act, "entry_" + name);
                    if (!act_sig.empty()) {
                        model.add_action(act_sig.name);
                        state->entry_actions.push_back(std::move(act_sig));
                    }
                } else if (item.find("exit /") != std::string::npos) {
                    size_t p = item.find("exit /");
                    std::string act = item.substr(p + 6);
                    auto act_sig = DiagramActionParser::parse_action_block(act, "exit_" + name);
                    if (!act_sig.empty()) {
                        model.add_action(act_sig.name);
                        state->exit_actions.push_back(std::move(act_sig));
                    }
                } else if (item.find("do /") != std::string::npos) {
                    size_t p = item.find("do /");
                    size_t end_p = item.find(')', p);
                    std::string act = item.substr(p + 4, (end_p != std::string::npos ? end_p : item.size()) - (p + 4));
                    act = sanitize_identifier(trim(act));
                    if (!act.empty()) {
                        state->do_activity = act;
                    }
                } else if (item.find("defer ") != std::string::npos) {
                    size_t p = item.find("defer ");
                    size_t end_p = item.find(')', p);
                    std::string evt = item.substr(p + 6, (end_p != std::string::npos ? end_p : item.size()) - (p + 6));
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
        if (!parent.empty()) {
            if (auto* state = model.find_state_mut(name)) {
                state->pinned_parent = true;
            }
        }
    }
}

void MermaidParser::parse_deferred_event(std::string_view line, FsmIr& model,
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
        if (!rest.empty() && (rest.front() == '/' || rest.front() == ':')) {
            rest = trim(rest.substr(1));
        }
    } else if (ends_with(rest, "/ defer")) {
        rest = trim(rest.substr(0, rest.size() - 7));
    } else if (ends_with(rest, "/defer")) {
        rest = trim(rest.substr(0, rest.size() - 6));
    } else {
        const auto defer_pos = rest.find("defer ");
        if (defer_pos != std::string_view::npos) {
            rest = trim(rest.substr(defer_pos + 6));
            if (!rest.empty() && (rest.front() == '/' || rest.front() == ':')) {
                rest = trim(rest.substr(1));
            }
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

void MermaidParser::parse_internal_transition(std::string_view line, FsmIr& model,
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

    const std::string event_name = sanitize_identifier(trim(label));
    if (event_name.empty()) {
        return;
    }

    std::optional<ActionSignature> parsed_action;
    if (!raw_action.empty()) {
        std::string fallback = "action_" + state_name + "_" + event_name;
        if (event_name == "entry") {
            fallback = "entry_" + state_name;
        } else if (event_name == "exit") {
            fallback = "exit_" + state_name;
        }
        parsed_action = DiagramActionParser::parse_action_block(raw_action, fallback);
        detail::disambiguate_synthesized_action(model, *parsed_action);
    }

    const std::string current_parent = parent_stack.empty() ? "" : parent_stack.back();
    model.add_state(state_name, current_parent);

    // Native Mermaid lifecycle hooks: entry, exit
    if (event_name == "entry" && parsed_action) {
        model.add_action(parsed_action->name);
        if (auto* st = model.find_state_mut(state_name)) {
            st->entry_actions.push_back(*parsed_action);
        }
        return;
    }
    if (event_name == "exit" && parsed_action) {
        model.add_action(parsed_action->name);
        if (auto* st = model.find_state_mut(state_name)) {
            st->exit_actions.push_back(*parsed_action);
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
    trans.parent_scope = current_parent;

    model.add_transition(std::move(trans));
}

bool MermaidParser::parse_transition_line(std::string_view line, FsmIr& model, std::string& out_error, size_t line_num,
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
    if (is_history && dst.empty() && !current_parent.empty()) {
        dst = current_parent;
    }

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

        // Remaining is EventName (or ChangeTrigger when(...) or TimeTrigger after(...)/every(...)/at(...))
        const std::string evt = std::string(trim(label));
        if (!evt.empty() && evt != "Anonymous" && evt != "AnonymousEvent" && evt != "anonymous") {
            static const std::regex when_ev_regex(R"(^when\s*\((.+)\)$)", std::regex::icase);
            static const std::regex time_call_regex(
                R"(^(after|every|at)\s*\(\s*([0-9]+)\s*(ms|us|s|m|msec|usec|sec|min)?\s*\)$)", std::regex::icase);
            std::smatch wm;
            std::smatch tcm;
            if (std::regex_match(evt, wm, when_ev_regex)) {
                raw_event_change_expr = std::string(trim(wm[1].str()));
            } else if (std::regex_match(evt, tcm, time_call_regex)) {
                std::string kw = tcm[1].str();
                std::transform(kw.begin(), kw.end(), kw.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                std::string u = tcm[3].matched ? tcm[3].str() : "ms";
                std::transform(u.begin(), u.end(), u.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (u == "msec") {
                    u = "ms";
                } else if (u == "usec") {
                    u = "us";
                } else if (u == "sec") {
                    u = "s";
                } else if (u == "min") {
                    u = "m";
                }
                event_name = kw + "_" + tcm[2].str() + u;
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

    if (!model.is_choice_node(src) && model.find_state(src) == nullptr) {
        model.add_state(src, current_parent);
    }
    if (!model.is_choice_node(dst)) {
        if (model.find_state(dst) == nullptr) {
            model.add_state(dst, current_parent);
        }
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
        static const std::regex time_ev_regex(R"(^(after|every|at)_([0-9]+)(ms|us|s|m)$)", std::regex::icase);
        std::smatch tm;
        if (std::regex_match(event_name, tm, time_ev_regex)) {
            std::string kw = tm[1].str();
            std::transform(kw.begin(), kw.end(), kw.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const TimeTriggerKind tkind = (kw == "every")
                                              ? TimeTriggerKind::Every
                                              : ((kw == "at") ? TimeTriggerKind::At : TimeTriggerKind::After);
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

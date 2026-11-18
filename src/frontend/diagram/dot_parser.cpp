#include "fsm/frontend/diagram/dot_parser.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <sstream>

#include "fsm/frontend/diagram/diagram_action_parser.hpp"
#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/frontend/directive/guard_parser.hpp"

namespace fsm::frontend::diagram {

using namespace fsm::ir;
using directive::DirectiveParser;
using directive::GuardExpressionParser;

std::string DotParser::trim_line(std::string_view line_sv) {
    size_t start = 0;
    while (start < line_sv.size() && (std::isspace(static_cast<unsigned char>(line_sv[start])) != 0)) {
        start++;
    }
    if (start == line_sv.size()) {
        return "";
    }
    size_t end = line_sv.size() - 1;
    while (end > start && (std::isspace(static_cast<unsigned char>(line_sv[end])) != 0 || line_sv[end] == ';')) {
        end--;
    }
    return std::string(line_sv.substr(start, end - start + 1));
}

void DotParser::parse_label(const std::string& label, std::string& out_event, std::string& out_guard,
                            std::string& out_action) {
    if (label.empty()) {
        return;
    }

    std::string work = label;
    // Check Action / ActionName
    size_t slash_pos = DiagramActionParser::find_action_slash(work);
    if (slash_pos != std::string::npos) {
        out_action = trim_line(work.substr(slash_pos + 1));
        work = work.substr(0, slash_pos);
    }

    // Check Guard [GuardName]
    size_t lbracket = work.find('[');
    size_t rbracket = work.find(']', lbracket);
    if (lbracket != std::string::npos && rbracket != std::string::npos) {
        out_guard = trim_line(work.substr(lbracket + 1, rbracket - lbracket - 1));
        work = work.substr(0, lbracket);
    }

    // Remainder is Event
    out_event = trim_line(work);
}

bool DotParser::parse(std::string_view content, FsmIr& model, std::string& error_message) {
    std::istringstream stream{std::string(content)};
    std::string line;
    std::string current_parent_state;
    std::vector<std::string> parent_stack;
    std::map<std::string, bool> point_nodes;

    // Regex helpers
    auto unquote_node = [](std::string s) -> std::string {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
            return s.substr(1, s.size() - 2);
        }
        return s;
    };

    const std::regex digraph_regex(R"(^\s*(?:digraph|graph)\s+([A-Za-z0-9_]+))", std::regex::icase);
    const std::regex subgraph_regex(R"raw(^\s*subgraph\s+([A-Za-z0-9_]+|"[^"]*")\s*\{?)raw", std::regex::icase);
    const std::regex node_point_regex(
        R"raw(^\s*([A-Za-z0-9_]+|"[^"]*")\s*\[.*shape\s*=\s*(?:point|circle|none).*\].*)raw", std::regex::icase);
    const std::regex edge_regex(
        R"raw(^\s*([A-Za-z0-9_\[\]\*]+|"[^"]*")\s*(?:->|--)\s*([A-Za-z0-9_\[\]\*]+|"[^"]*")(?:\s*\[(.*)\])?.*)raw");
    const std::regex label_prefix_regex(R"raw(label\s*=\s*")raw", std::regex::icase);
    auto extract_label_attr = [&label_prefix_regex](const std::string& input, std::string& out) -> bool {
        std::smatch m;
        if (!std::regex_search(input, m, label_prefix_regex)) {
            return false;
        }
        std::size_t pos = static_cast<std::size_t>(m.position() + m.length());
        out.clear();
        while (pos < input.size()) {
            char c = input[pos];
            if (c == '\\' && pos + 1 < input.size()) {
                char next = input[pos + 1];
                if (next == '"' || next == '\\') {
                    out.push_back(next);
                    pos += 2;
                    continue;
                }
            }
            if (c == '"') {
                return true;
            }
            out.push_back(c);
            ++pos;
        }
        return true;
    };

    while (std::getline(stream, line)) {
        std::string trimmed = trim_line(line);
        if (trimmed.empty()) {
            continue;
        }

        if (DirectiveParser::is_directive(trimmed)) {
            std::string body = DirectiveParser::extract_directive_body(trimmed);
            if (trim_line(body) == "parallel" && !current_parent_state.empty()) {
                if (auto* parent = model.find_state_mut(current_parent_state)) {
                    parent->kind = StateKind::Parallel;
                }
                continue;
            }
            DirectiveParser::parse_model_directive(body, model);
            continue;
        }

        if (starts_with(trimmed, "//") || starts_with(trimmed, "#") || starts_with(trimmed, "/*")) {
            continue;
        }

        // Check digraph Name {
        std::smatch match;
        if (std::regex_search(trimmed, match, digraph_regex)) {
            model.name = sanitize_identifier(match[1].str());
            continue;
        }

        // Check subgraph cluster_CompositeState {
        if (std::regex_search(trimmed, match, subgraph_regex)) {
            std::string raw_sub = unquote_node(match[1].str());
            if (raw_sub.rfind("cluster_", 0) == 0) {
                raw_sub = raw_sub.substr(8);
            }
            std::string comp_name = sanitize_identifier(raw_sub);
            model.add_state(comp_name, current_parent_state);
            auto* parent_state = model.find_state_mut(comp_name);
            if (parent_state != nullptr) {
                parent_state->is_composite = true;
            }
            parent_stack.push_back(current_parent_state);
            current_parent_state = comp_name;
            continue;
        }

        auto parse_label_actions = [&](const std::string& raw_lbl, const std::string& target_state) {
            std::string unescaped_lbl;
            for (size_t i = 0; i < raw_lbl.size(); ++i) {
                if (raw_lbl[i] == '\\' && i + 1 < raw_lbl.size() && raw_lbl[i + 1] == 'n') {
                    unescaped_lbl += '\n';
                    ++i;
                } else {
                    unescaped_lbl += raw_lbl[i];
                }
            }
            auto extract_slash_clause = [](const std::string& item, std::string_view kw) -> std::string {
                std::string paren_prefix = "(" + std::string(kw) + " /";
                auto p = item.find(paren_prefix);
                if (p != std::string::npos) {
                    size_t start = p + paren_prefix.size();
                    size_t end_p = item.find(')', start);
                    return std::string(
                        trim_line(item.substr(start, (end_p != std::string::npos ? end_p : item.size()) - start)));
                }
                if (item.rfind(kw, 0) == 0) {
                    size_t pos = kw.size();
                    while (pos < item.size() && (item[pos] == ' ' || item[pos] == '\t')) {
                        ++pos;
                    }
                    if (pos < item.size() && item[pos] == '/') {
                        return std::string(trim_line(item.substr(pos + 1)));
                    }
                }
                return "";
            };
            auto add_lifecycle_action = [&](const std::string& raw_act, const char* suffix, bool is_entry) {
                if (raw_act.empty()) {
                    return;
                }
                auto assign_sig = ::fsm::ir::try_parse_assignment_action(raw_act, target_state + suffix);
                ActionSignature sig =
                    assign_sig.has_value() ? std::move(*assign_sig) : ActionSignature{sanitize_identifier(raw_act)};
                if (!sig.name.empty()) {
                    model.add_action(sig.name);
                    if (auto* st = model.find_state_mut(target_state)) {
                        if (is_entry) {
                            st->entry_actions.push_back(std::move(sig));
                        } else {
                            st->exit_actions.push_back(std::move(sig));
                        }
                    }
                }
            };
            std::string item_line;
            std::istringstream lss(unescaped_lbl);
            while (std::getline(lss, item_line)) {
                std::string clean_item = trim_line(item_line);
                if (std::string raw_act = extract_slash_clause(clean_item, "entry"); !raw_act.empty()) {
                    add_lifecycle_action(raw_act, "_entry", true);
                } else if (std::string raw_exit = extract_slash_clause(clean_item, "exit"); !raw_exit.empty()) {
                    add_lifecycle_action(raw_exit, "_exit", false);
                } else if (std::string raw_do = extract_slash_clause(clean_item, "do"); !raw_do.empty()) {
                    std::string act = sanitize_identifier(raw_do);
                    if (!act.empty()) {
                        if (auto* st = model.find_state_mut(target_state))
                            st->do_activity = act;
                    }
                } else if (clean_item.find("(defer ") != std::string::npos || clean_item.rfind("defer ", 0) == 0) {
                    std::string evt;
                    if (clean_item.find("(defer ") != std::string::npos) {
                        size_t p = clean_item.find("(defer ");
                        size_t end_p = clean_item.find(')', p);
                        evt = clean_item.substr(p + 7,
                                                (end_p != std::string::npos ? end_p : clean_item.size()) - (p + 7));
                    } else {
                        evt = clean_item.substr(6);
                    }
                    evt = sanitize_identifier(trim_line(evt));
                    if (!evt.empty()) {
                        model.add_event(evt);
                        if (auto* st = model.find_state_mut(target_state))
                            st->deferred_events.push_back(evt);
                    }
                }
            }
        };

        // Check cluster label inside subgraph: label = "..." or label="...";
        if (!current_parent_state.empty()) {
            const std::regex cluster_label_prefix(R"raw(^\s*label\s*=\s*")raw", std::regex::icase);
            std::string lbl_content;
            if (std::regex_search(trimmed, cluster_label_prefix) && extract_label_attr(trimmed, lbl_content)) {
                parse_label_actions(lbl_content, current_parent_state);
                continue;
            }
        }

        // Check cluster style="...dashed..." inside subgraph to infer parallel state
        static const std::regex cluster_dashed_regex(R"raw(^\s*style\s*=\s*"[^"]*dashed[^"]*")raw", std::regex::icase);
        if (!current_parent_state.empty() && std::regex_search(trimmed, cluster_dashed_regex)) {
            if (auto* parent = model.find_state_mut(current_parent_state)) {
                parent->kind = StateKind::Parallel;
            }
            continue;
        }

        // Closing brace }
        if (trimmed == "}" || trimmed == "};") {
            if (!current_parent_state.empty()) {
                if (auto* parent = model.find_state_mut(current_parent_state)) {
                    if (parent->kind == StateKind::Parallel && parent->orthogonal_regions.empty()) {
                        for (const auto& child : model.states) {
                            if (child.parent_state != current_parent_state) {
                                continue;
                            }
                            OrthogonalRegion reg;
                            reg.id = child.name;
                            reg.name = child.name;
                            reg.initial_state_id = child.initial_sub_state;
                            for (const auto& sub : model.states) {
                                if (sub.parent_state == child.name) {
                                    reg.state_ids.push_back(sub.name);
                                }
                            }
                            if (reg.initial_state_id.empty() && !reg.state_ids.empty()) {
                                reg.initial_state_id = reg.state_ids.front();
                            }
                            parent->orthogonal_regions.push_back(std::move(reg));
                        }
                    }
                }
            }
            if (!parent_stack.empty()) {
                current_parent_state = parent_stack.back();
                parent_stack.pop_back();
            }
            continue;
        }

        // Check point / initial / history node declaration (e.g. init [shape=point]; or __hist_State__ [shape=circle,
        // label="H*"];)
        if (std::regex_search(trimmed, match, node_point_regex)) {
            std::string node_name = sanitize_identifier(unquote_node(match[1].str()));
            if (node_name.rfind("__hist_", 0) == 0 && !current_parent_state.empty()) {
                if (auto* parent = model.find_state_mut(current_parent_state)) {
                    parent->has_history = true;
                    if (trimmed.find("label=\"H*\"") != std::string::npos) {
                        parent->has_deep_history = true;
                    }
                }
            }
            point_nodes[node_name] = true;
            continue;
        }

        // Check node attributes (e.g. StateName [defer="Event1, Event2"];)
        const std::regex node_attr_regex(R"raw(^\s*([A-Za-z0-9_]+|"[^"]*")\s*\[(.*)\])raw", std::regex::icase);
        if (!std::regex_search(trimmed, match, edge_regex) && std::regex_search(trimmed, match, node_attr_regex)) {
            std::string node_name = sanitize_identifier(unquote_node(match[1].str()));
            std::string attrs = match[2].str();

            if (node_name == "node" || node_name == "edge" || node_name == "graph" || node_name == "digraph" ||
                node_name == "strict" || node_name.rfind("__start", 0) == 0 || node_name.rfind("__hist", 0) == 0 ||
                point_nodes.count(node_name) != 0) {
                if (node_name.rfind("__start", 0) == 0 || node_name.rfind("__hist", 0) == 0) {
                    point_nodes[node_name] = true;
                }
                continue;
            }

            if (attrs.find("shape=diamond") != std::string::npos ||
                attrs.find("shape = diamond") != std::string::npos) {
                model.add_choice_node(node_name);
                continue;
            }

            model.add_state(node_name, current_parent_state);

            // Check label for actions / activities / defers
            std::string lbl_content;
            if (extract_label_attr(attrs, lbl_content)) {
                parse_label_actions(lbl_content, node_name);
            }

            const std::regex defer_attr_regex(R"raw(defer\s*=\s*"([^"]*)")raw", std::regex::icase);
            std::smatch defer_match;
            if (std::regex_search(attrs, defer_match, defer_attr_regex)) {
                std::string def_events = defer_match[1].str();
                std::stringstream ss(def_events);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    std::string d_evt = sanitize_identifier(trim_line(item));
                    if (!d_evt.empty()) {
                        model.add_event(d_evt);
                        if (auto* st = model.find_state_mut(node_name)) {
                            st->deferred_events.push_back(d_evt);
                        }
                    }
                }
            }
            continue;
        }

        // Check edge transition S1 -> S2 [label="..."]
        if (std::regex_search(trimmed, match, edge_regex)) {
            std::string src_raw = unquote_node(match[1].str());
            std::string dst_raw = unquote_node(match[2].str());
            std::string attrs = match[3].matched ? match[3].str() : "";

            std::string label_str;
            if (!extract_label_attr(attrs, label_str) && !attrs.empty() && attrs.find('=') == std::string::npos) {
                label_str = attrs;
            }

            // Is initial transition?
            if (point_nodes.count(src_raw) != 0 || src_raw.rfind("__start", 0) == 0 ||
                src_raw.rfind("initial_", 0) == 0 || src_raw.rfind("init_", 0) == 0 ||
                src_raw.rfind("start_", 0) == 0 || src_raw == "[*]" || src_raw == "__start__" || src_raw == "start" ||
                src_raw == "init" || src_raw == "initial") {
                std::string init_target = sanitize_identifier(dst_raw);
                if (current_parent_state.empty()) {
                    model.initial_state = init_target;
                    model.initial_state_id = init_target;
                } else {
                    auto* parent = model.find_state_mut(current_parent_state);
                    if (parent != nullptr) {
                        parent->initial_sub_state = init_target;
                    }
                }
                model.add_state(init_target, current_parent_state);
                continue;
            }

            std::string src = sanitize_identifier(src_raw);
            std::string dst = sanitize_identifier(dst_raw);

            // Parse label: Event (prio=1) [Guard] / Action
            std::string event_name;
            std::string guard_name;
            std::string action_name;
            std::uint32_t priority = 0;

            static const std::regex prio_attr_regex(
                R"regex(priority\s*=\s*"?(\d+)"?|(?:\[|\()(?:prio|priority)=(\d+)(?:\]|\)))regex", std::regex::icase);
            std::smatch prio_match;
            if (std::regex_search(label_str, prio_match, prio_attr_regex)) {
                std::string p_str = prio_match[1].matched ? prio_match[1].str() : prio_match[2].str();
                try {
                    priority = static_cast<std::uint32_t>(std::stoul(p_str));
                } catch (...) {
                }
                label_str = std::regex_replace(label_str, prio_attr_regex, "");
            } else if (std::regex_search(attrs, prio_match, prio_attr_regex)) {
                std::string p_str = prio_match[1].matched ? prio_match[1].str() : prio_match[2].str();
                try {
                    priority = static_cast<std::uint32_t>(std::stoul(p_str));
                } catch (...) {
                }
            }

            parse_label(label_str, event_name, guard_name, action_name);

            TransitionEdge trans;
            trans.source = src;
            trans.target = dst;
            trans.event = sanitize_identifier(event_name);
            trans.priority = priority;
            if (!guard_name.empty()) {
                auto parsed = GuardExpressionParser::parse(guard_name);
                if (!parsed.cpp_type.empty()) {
                    trans.guard = parsed.cpp_type;
                    for (const auto& detail : parsed.atomic_guard_details) {
                        model.add_guard(detail.name, "", detail.expression, detail.expression);
                    }
                    for (const auto& atomic : parsed.atomic_guards) {
                        model.add_guard(atomic);
                    }
                }
            }
            if (!action_name.empty()) {
                std::string prefix = sanitize_identifier(src) + "_" + sanitize_identifier(dst) + "_act";
                auto assign_sig = ::fsm::ir::try_parse_assignment_action(action_name, prefix);
                trans.transition_action =
                    assign_sig.has_value() ? std::move(*assign_sig) : ActionSignature(sanitize_identifier(action_name));
                model.add_action(trans.transition_action->name);
            }
            trans.kind = (src == dst && attrs.find("internal") != std::string::npos) ? TransitionEdgeKind::Internal
                                                                                     : TransitionEdgeKind::External;

            model.add_state(src, current_parent_state);
            model.add_state(dst, current_parent_state);
            if (!trans.event.empty()) {
                model.add_event(trans.event);
            }

            model.add_transition(std::move(trans));
        }
    }

    if (model.states.empty()) {
        error_message = "DOT Parser: No valid states or transitions extracted.";
        return false;
    }

    if (model.initial_state.empty() && !model.states.empty()) {
        model.initial_state = model.states.front().name;
    }

    return true;
}

}  // namespace fsm::frontend::diagram

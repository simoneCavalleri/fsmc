#pragma once

#include <algorithm>
#include <cctype>
#include <map>
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

class DotParser : public IParser {
  public:
    [[nodiscard]] FrontendKind kind() const noexcept override { return FrontendKind::Diagram; }
    [[nodiscard]] std::string_view format_name() const noexcept override { return "dot"; }

    bool parse(std::string_view content, FsmIr& model, std::string& error_message) override {
        std::istringstream stream{std::string(content)};
        std::string line;
        std::string current_parent_state;
        std::vector<std::string> parent_stack;
        std::map<std::string, bool> point_nodes;

        // Regex helpers
        const std::regex digraph_regex(R"(^\s*(?:digraph|graph)\s+([A-Za-z0-9_]+))", std::regex::icase);
        const std::regex subgraph_regex(R"(^\s*subgraph\s+(?:cluster_)?([A-Za-z0-9_]+)\s*\{?)", std::regex::icase);
        const std::regex node_point_regex(R"(^\s*([A-Za-z0-9_]+)\s*\[.*shape\s*=\s*(?:point|circle|none).*\].*)",
                                          std::regex::icase);
        const std::regex edge_regex(
            R"(^\s*([A-Za-z0-9_\[\]\*]+)\s*(?:->|--)\s*([A-Za-z0-9_\[\]\*]+)(?:\s*\[(.*)\])?.*)");
        const std::regex label_attr_regex(R"raw(label\s*=\s*"([^"]*)")raw", std::regex::icase);

        while (std::getline(stream, line)) {
            std::string trimmed = trim_line(line);
            if (trimmed.empty() || starts_with(trimmed, "//") || starts_with(trimmed, "#") ||
                starts_with(trimmed, "/*")) {
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
                std::string comp_name = sanitize_identifier(match[1].str());
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
                std::string item_line;
                std::istringstream lss(unescaped_lbl);
                while (std::getline(lss, item_line)) {
                    std::string clean_item = trim_line(item_line);
                    if (clean_item.find("(entry /") != std::string::npos) {
                        size_t p = clean_item.find("(entry /");
                        size_t end_p = clean_item.find(')', p);
                        std::string act = clean_item.substr(
                            p + 8, (end_p != std::string::npos ? end_p : clean_item.size()) - (p + 8));
                        act = sanitize_identifier(trim_line(act));
                        if (!act.empty()) {
                            model.add_action(act);
                            if (auto* st = model.find_state_mut(target_state))
                                st->entry_actions.push_back(ActionSignature{act});
                        }
                    } else if (clean_item.find("(exit /") != std::string::npos) {
                        size_t p = clean_item.find("(exit /");
                        size_t end_p = clean_item.find(')', p);
                        std::string act = clean_item.substr(
                            p + 7, (end_p != std::string::npos ? end_p : clean_item.size()) - (p + 7));
                        act = sanitize_identifier(trim_line(act));
                        if (!act.empty()) {
                            model.add_action(act);
                            if (auto* st = model.find_state_mut(target_state))
                                st->exit_actions.push_back(ActionSignature{act});
                        }
                    } else if (clean_item.find("(do /") != std::string::npos) {
                        size_t p = clean_item.find("(do /");
                        size_t end_p = clean_item.find(')', p);
                        std::string act = clean_item.substr(
                            p + 5, (end_p != std::string::npos ? end_p : clean_item.size()) - (p + 5));
                        act = sanitize_identifier(trim_line(act));
                        if (!act.empty()) {
                            if (auto* st = model.find_state_mut(target_state))
                                st->do_activity = act;
                        }
                    } else if (clean_item.find("(defer ") != std::string::npos) {
                        size_t p = clean_item.find("(defer ");
                        size_t end_p = clean_item.find(')', p);
                        std::string evt = clean_item.substr(
                            p + 7, (end_p != std::string::npos ? end_p : clean_item.size()) - (p + 7));
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
            const std::regex cluster_label_regex(R"raw(^\s*label\s*=\s*"([^"]*)")raw", std::regex::icase);
            if (!current_parent_state.empty() && std::regex_search(trimmed, match, cluster_label_regex)) {
                std::string lbl_content = match[1].str();
                parse_label_actions(lbl_content, current_parent_state);
                continue;
            }

            // Closing brace }
            if (trimmed == "}" || trimmed == "};") {
                if (!parent_stack.empty()) {
                    current_parent_state = parent_stack.back();
                    parent_stack.pop_back();
                }
                continue;
            }

            // Check point / initial node declaration (e.g. init [shape=point];)
            if (std::regex_search(trimmed, match, node_point_regex)) {
                std::string node_name = sanitize_identifier(match[1].str());
                point_nodes[node_name] = true;
                continue;
            }

            // Check node attributes (e.g. StateName [defer="Event1, Event2"];)
            const std::regex node_attr_regex(R"(^\s*([A-Za-z0-9_]+)\s*\[(.*)\])", std::regex::icase);
            if (!std::regex_search(trimmed, match, edge_regex) && std::regex_search(trimmed, match, node_attr_regex)) {
                std::string node_name = sanitize_identifier(match[1].str());
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
                std::smatch lbl_match;
                if (std::regex_search(attrs, lbl_match, label_attr_regex)) {
                    std::string lbl_content = lbl_match[1].str();
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
                std::string src_raw = match[1].str();
                std::string dst_raw = match[2].str();
                std::string attrs = match[3].matched ? match[3].str() : "";

                std::string label_str;
                std::smatch label_match;
                if (std::regex_search(attrs, label_match, label_attr_regex)) {
                    label_str = label_match[1].str();
                } else if (!attrs.empty() && attrs.find('=') == std::string::npos) {
                    label_str = attrs;
                }

                // Is initial transition?
                if (point_nodes.count(src_raw) != 0 || src_raw.rfind("__start", 0) == 0 || src_raw == "[*]" ||
                    src_raw == "__start__" || src_raw == "start" || src_raw == "init" || src_raw == "initial") {
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
                    R"regex(priority\s*=\s*"?(\d+)"?|(?:\[|\()(?:prio|priority)=(\d+)(?:\]|\)))regex",
                    std::regex::icase);
                std::smatch prio_match;
                if (std::regex_search(attrs, prio_match, prio_attr_regex)) {
                    std::string p_str = prio_match[1].matched ? prio_match[1].str() : prio_match[2].str();
                    try {
                        priority = static_cast<std::uint32_t>(std::stoul(p_str));
                    } catch (...) {
                    }
                } else if (std::regex_search(label_str, prio_match, prio_attr_regex)) {
                    std::string p_str = prio_match[1].matched ? prio_match[1].str() : prio_match[2].str();
                    try {
                        priority = static_cast<std::uint32_t>(std::stoul(p_str));
                    } catch (...) {
                    }
                    label_str = std::regex_replace(label_str, prio_attr_regex, "");
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
                        for (const auto& atomic : parsed.atomic_guards) {
                            model.add_guard(atomic);
                        }
                    }
                }
                if (!action_name.empty()) {
                    trans.action = sanitize_identifier(action_name);
                    model.add_action(*trans.action);
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

  private:
    static std::string trim_line(std::string_view line_sv) {
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

    static void parse_label(const std::string& label, std::string& out_event, std::string& out_guard,
                            std::string& out_action) {
        if (label.empty()) {
            return;
        }

        std::string work = label;
        // Check Action / ActionName
        size_t slash_pos = work.find('/');
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
};

}  // namespace fsm::codegen

#pragma once

#include <cctype>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/frontend/directive_parser.hpp"
#include "fsm/frontend/guard_parser.hpp"
#include "fsm/frontend/parser_interface.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

class Sysml2Parser : public IParser {
  public:
    [[nodiscard]] FrontendKind kind() const noexcept override { return FrontendKind::Formal; }
    [[nodiscard]] std::string_view format_name() const noexcept override { return "sysml2"; }

    enum class SysmlBlockKind : std::uint8_t {
        Package,
        StateDef,
        State,
        ItemDef,
        ActionBlock
    };

    bool parse(std::string_view content, FsmIr& model, std::string& error_message) override {
        std::string content_str(content);
        std::istringstream stream(content_str);
        std::string line;
        size_t line_number = 0;

        std::vector<std::string> state_stack;
        std::vector<SysmlBlockKind> block_stack;
        std::string current_item_def;
        std::string accumulated_stmt;

        auto flush_stmt = [&](size_t current_line, bool is_block_open, SysmlBlockKind& out_kind) -> bool {
            accumulated_stmt = trim(accumulated_stmt);
            out_kind = SysmlBlockKind::ActionBlock;
            if (!accumulated_stmt.empty()) {
                if (!process_statement(accumulated_stmt, model, state_stack, current_item_def, error_message,
                                       current_line, is_block_open, out_kind)) {
                    return false;
                }
                accumulated_stmt.clear();
            }
            return true;
        };

        size_t action_brace_depth = 0;

        while (std::getline(stream, line)) {
            line_number++;
            const std::string trimmed_line = trim(line);
            if (DirectiveParser::is_directive(trimmed_line)) {
                std::string body = DirectiveParser::extract_directive_body(trimmed_line);
                if (body.rfind("var", 0) == 0) {
                    if (auto var = DirectiveParser::parse_variable_directive(body)) {
                        model.add_variable(std::move(*var));
                    }
                } else if (body.rfind("property", 0) == 0) {
                    if (auto prop = DirectiveParser::parse_property_directive(body)) {
                        model.add_property(std::move(*prop));
                    }
                } else if (body.rfind("signal", 0) == 0) {
                    if (auto sig = DirectiveParser::parse_signal_directive(body)) {
                        model.add_signal(std::move(*sig));
                    }
                } else if (!state_stack.empty()) {
                    auto* st = model.find_state_mut(state_stack.back());
                    if (st != nullptr) {
                        if (body.rfind("state", 0) == 0) {
                            DirectiveParser::parse_state_directive(body, *st);
                        } else if (body.rfind("defer", 0) == 0) {
                            DirectiveParser::parse_defer_directive(body, *st);
                        }
                    }
                }
                continue;
            }

            std::string clean_line = strip_comments(line);
            clean_line = trim(clean_line);

            if (clean_line.empty()) {
                continue;
            }

            for (const char character : clean_line) {
                if (action_brace_depth > 0) {
                    accumulated_stmt += character;
                    if (character == '{') {
                        action_brace_depth++;
                    } else if (character == '}') {
                        action_brace_depth--;
                    }
                    continue;
                }

                if (character == '{') {
                    std::string trimmed_acc = trim(accumulated_stmt);
                    if (trimmed_acc.rfind("do", trimmed_acc.size() - 2) != std::string::npos ||
                        trimmed_acc.find("transition") != std::string::npos ||
                        (trimmed_acc.size() >= 2 && trimmed_acc.substr(trimmed_acc.size() - 2) == "do")) {
                        action_brace_depth = 1;
                        accumulated_stmt += "{";
                        continue;
                    }

                    SysmlBlockKind opened_kind = SysmlBlockKind::ActionBlock;
                    if (!flush_stmt(line_number, true, opened_kind)) {
                        return false;
                    }
                    block_stack.push_back(opened_kind);
                } else if (character == ';') {
                    std::string trimmed_acc = trim(accumulated_stmt);
                    if (trimmed_acc == "entry" || trimmed_acc == "initial") {
                        accumulated_stmt += "; ";
                        continue;
                    }
                    SysmlBlockKind unused_kind = SysmlBlockKind::ActionBlock;
                    if (!flush_stmt(line_number, false, unused_kind)) {
                        return false;
                    }
                } else if (character == '}') {
                    SysmlBlockKind unused_kind = SysmlBlockKind::ActionBlock;
                    if (!flush_stmt(line_number, false, unused_kind)) {
                        return false;
                    }
                    if (!block_stack.empty()) {
                        const auto popped_kind = block_stack.back();
                        block_stack.pop_back();
                        if (popped_kind == SysmlBlockKind::State && !state_stack.empty()) {
                            state_stack.pop_back();
                        } else if (popped_kind == SysmlBlockKind::ItemDef) {
                            current_item_def.clear();
                        }
                    }
                } else {
                    accumulated_stmt += character;
                }
            }
            if (!accumulated_stmt.empty() && accumulated_stmt.back() != ' ') {
                accumulated_stmt += ' ';
            }
        }

        SysmlBlockKind end_kind = SysmlBlockKind::ActionBlock;
        if (!flush_stmt(line_number, false, end_kind)) {
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

    static std::string map_sysml_type_to_cpp(std::string_view sysml_type) {
        std::string t = trim(std::string{sysml_type});
        auto colon_pos = t.rfind("::");
        if (colon_pos != std::string::npos) {
            t = t.substr(colon_pos + 2);
        }
        if (t == "Integer" || t == "Natural" || t == "Positive" || t == "int" || t == "uint32") {
            return "uint32_t";
        }
        if (t == "Int32" || t == "int32") {
            return "int32_t";
        }
        if (t == "Real" || t == "Float" || t == "float") {
            return "float";
        }
        if (t == "Double" || t == "double") {
            return "double";
        }
        if (t == "Boolean" || t == "bool") {
            return "bool";
        }
        if (t == "String" || t == "string") {
            return "std::string";
        }
        return t;
    }

    static bool process_statement(const std::string& raw_stmt, FsmIr& model, std::vector<std::string>& state_stack,
                                  std::string& current_item_def, std::string& error_message, size_t line_number,
                                  bool is_block_open, SysmlBlockKind& out_kind) {
        (void)error_message;
        (void)line_number;
        const std::string stmt = normalize_whitespace(raw_stmt);
        out_kind = SysmlBlockKind::ActionBlock;
        if (stmt.empty()) {
            return true;
        }

        // 1. state def <Name> / package <Name>
        static const std::regex state_def_regex(R"(^(?:state\s+def|package)\s+([A-Za-z_][A-Za-z0-9_]*))",
                                                std::regex::optimize);
        std::smatch match;
        if (std::regex_search(stmt, match, state_def_regex)) {
            if (model.name.empty() || model.name == "MyStateMachine" || model.name == "GeneratedFSM") {
                model.name = sanitize_identifier(match[1].str());
            }
            if (is_block_open) {
                out_kind = (stmt.rfind("package", 0) == 0) ? SysmlBlockKind::Package : SysmlBlockKind::StateDef;
            }
            return true;
        }

        // 2. Item Definition (Signal with Payload): item def / event def / attribute def / port def <Name>
        static const std::regex item_def_regex(
            R"(^(?:item\s+def|event\s+def|attribute\s+def|port\s+def)\s+([A-Za-z_][A-Za-z0-9_]*))",
            std::regex::optimize);
        if (std::regex_search(stmt, match, item_def_regex)) {
            const std::string sig_name = sanitize_identifier(match[1].str());
            SignalDefinition sig;
            sig.name = sig_name;
            model.add_signal(std::move(sig));
            if (is_block_open) {
                current_item_def = sig_name;
                out_kind = SysmlBlockKind::ItemDef;
            }
            return true;
        }

        // 3. Attribute / Variable declaration: attribute <name> : <Type> [[unit]] [= <init>] / var <name> : <Type>
        static const std::regex attr_regex(
            R"(^(?:attribute|var)\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z0-9_:]+)(?:\s*(\[[^\]]+\]))?(?:\s*=\s*([^;]+))?)",
            std::regex::optimize);
        if (std::regex_search(stmt, match, attr_regex)) {
            const std::string attr_name = sanitize_identifier(match[1].str());
            const std::string raw_type = match[2].str();
            const std::string cpp_type = map_sysml_type_to_cpp(raw_type);
            const std::string unit_str = match[3].matched ? trim(match[3].str()) : "";
            const std::string init_val = match[4].matched ? trim(match[4].str()) : "";

            if (!current_item_def.empty()) {
                // Member attribute inside a Signal / Item Definition
                for (auto& sig : model.signals) {
                    if (sig.name == current_item_def) {
                        sig.attributes.emplace_back(attr_name, cpp_type, init_val);
                        break;
                    }
                }
            } else {
                // Top-level EFSM state machine variable
                VariableDefinition var;
                var.name = attr_name;
                var.type = cpp_type;
                var.type_kind = infer_type_kind(raw_type);
                std::string clean_unit = unit_str;
                if (clean_unit.size() >= 2 && clean_unit.front() == '[' && clean_unit.back() == ']') {
                    clean_unit = clean_unit.substr(1, clean_unit.size() - 2);
                }
                if (!clean_unit.empty()) {
                    var.physical_unit = clean_unit;
                }
                var.initial_value = init_val;
                model.add_variable(std::move(var));
            }
            return true;
        }

        // 4. Choice node declaration: state <ChoiceName> <<choice>> or state <ChoiceName> :> Choice
        static const std::regex choice_regex(
            R"(^(?:parallel\s+)?state\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:<<choice>>|<<junction>>|:>\s*Choice|:>\s*Junction))",
            std::regex::optimize);
        if (std::regex_search(stmt, match, choice_regex)) {
            const std::string choice_name = sanitize_identifier(match[1].str());
            model.add_choice_node(choice_name);
            return true;
        }

        // 4c. Decide / Decision pseudostate node: decide <ChoiceName>;
        static const std::regex decide_regex(R"(^decide\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
        if (std::regex_search(stmt, match, decide_regex)) {
            const std::string choice_name = sanitize_identifier(match[1].str());
            const std::string parent_name = state_stack.empty() ? "" : state_stack.back();
            model.add_or_get_state(choice_name, parent_name, StateKind::Choice);
            model.add_choice_node(choice_name);
            return true;
        }

        // 4b. Submachine declaration: state <SubName> :> <SubmachineName>
        static const std::regex submachine_regex(
            R"(^(?:parallel\s+)?state\s+([A-Za-z_][A-Za-z0-9_]*)\s*:>\s*([A-Za-z_][A-Za-z0-9_]*))",
            std::regex::optimize);
        if (std::regex_search(stmt, match, submachine_regex)) {
            const std::string state_name = sanitize_identifier(match[1].str());
            const std::string sub_name = sanitize_identifier(match[2].str());
            const std::string parent_name = state_stack.empty() ? "" : state_stack.back();
            auto& st = model.add_state(state_name, parent_name);
            st.submachine = SubmachineRef(sub_name);
            return true;
        }

        // 5. state <Name> (composite or leaf, optional parallel)
        static const std::regex state_decl_regex(R"(^(parallel\s+)?state\s+([A-Za-z_][A-Za-z0-9_]*))",
                                                 std::regex::optimize);
        if (std::regex_search(stmt, match, state_decl_regex)) {
            bool is_parallel = match[1].matched;
            const std::string state_name = sanitize_identifier(match[2].str());
            const std::string parent_name = state_stack.empty() ? "" : state_stack.back();
            auto* existing = model.find_state_mut(state_name);
            if (existing != nullptr) {
                existing->parent_state = parent_name;
                if (is_parallel) {
                    existing->kind = StateKind::Parallel;
                }
            } else {
                auto& st = model.add_state(state_name, parent_name);
                if (is_parallel) {
                    st.kind = StateKind::Parallel;
                }
            }

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
                out_kind = SysmlBlockKind::State;
            }
            return true;
        }

        // 6. entry; then <State>; or initial; then <State>;
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

        if (stmt == "entry" || stmt == "initial") {
            return true;
        }

        // 7a. Entry Point / Exit Point pseudostates: entry point <Name>; / exit point <Name>;
        static const std::regex entry_exit_point_regex(R"(^(entry\s+point|exit\s+point)\s+([A-Za-z_][A-Za-z0-9_]*))",
                                                       std::regex::optimize);
        if (std::regex_search(stmt, match, entry_exit_point_regex)) {
            bool is_entry = match[1].str().find("entry") != std::string::npos;
            const std::string name = sanitize_identifier(match[2].str());
            const std::string parent_name = state_stack.empty() ? "" : state_stack.back();
            model.add_or_get_state(name, parent_name, is_entry ? StateKind::EntryPoint : StateKind::ExitPoint);
            return true;
        }

        // 7b. State Lifecycle Actions: entry action <Act>; / exit action <Act>;
        static const std::regex entry_act_regex(R"(^entry\s+(?:action\s+|do\s+)?(?!point\b)([A-Za-z_][A-Za-z0-9_]*)(?:\s*\(\s*\))?)",
                                                std::regex::optimize);
        if (std::regex_search(stmt, match, entry_act_regex)) {
            const std::string act_name = sanitize_identifier(match[1].str());
            model.add_action(act_name);
            if (!state_stack.empty()) {
                auto* st = model.find_state_mut(state_stack.back());
                if (st != nullptr) {
                    st->entry_actions.emplace_back(act_name);
                }
            }
            return true;
        }

        static const std::regex exit_act_regex(R"(^exit\s+(?:action\s+|do\s+)?(?!point\b)([A-Za-z_][A-Za-z0-9_]*)(?:\s*\(\s*\))?)",
                                               std::regex::optimize);
        if (std::regex_search(stmt, match, exit_act_regex)) {
            const std::string act_name = sanitize_identifier(match[1].str());
            model.add_action(act_name);
            if (!state_stack.empty()) {
                auto* st = model.find_state_mut(state_stack.back());
                if (st != nullptr) {
                    st->exit_actions.emplace_back(act_name);
                }
            }
            return true;
        }

        // 8. State Do Activity: do action <Activity>; or do <Activity>;
        static const std::regex do_act_regex(R"(^do\s+(?:action\s+)?([A-Za-z_][A-Za-z0-9_]*)(?:\s*\(\s*\))?$)", std::regex::optimize);
        if (std::regex_search(stmt, match, do_act_regex)) {
            const std::string act_name = sanitize_identifier(match[1].str());
            if (!state_stack.empty()) {
                auto* st = model.find_state_mut(state_stack.back());
                if (st != nullptr) {
                    st->do_activity = act_name;
                }
            }
            return true;
        }

        // 8c. State Stay Duration / Invariant: stay duration <= 500[ms]; or invariant stay <= 500ms;
        static const std::regex invariant_regex(R"(^(?:stay(?:\s+duration)?\s*<=?|invariant)\s*(.+)$)",
                                                std::regex::optimize);
        if (std::regex_search(stmt, match, invariant_regex)) {
            if (!state_stack.empty()) {
                if (auto* st = model.find_state_mut(state_stack.back())) {
                    st->time_invariant = trim(match[1].str());
                }
            }
            return true;
        }

        // 8b. Deferred Events: defer <EventName>;
        static const std::regex defer_regex(R"(^defer\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
        if (std::regex_search(stmt, match, defer_regex)) {
            const std::string defer_ev = sanitize_identifier(match[1].str());
            if (!state_stack.empty()) {
                auto* st = model.find_state_mut(state_stack.back());
                if (st != nullptr) {
                    st->deferred_events.push_back(defer_ev);
                }
            }
            model.add_event(defer_ev);
            return true;
        }

        // 9. Requirement Satisfaction: satisfy [requirement] <ReqId>;
        static const std::regex satisfy_regex(R"(^satisfy\s+(?:requirement\s+)?([A-Za-z0-9_\-]+))",
                                              std::regex::optimize);
        if (std::regex_search(stmt, match, satisfy_regex)) {
            const std::string req_id = match[1].str();
            if (!state_stack.empty()) {
                auto* st = model.find_state_mut(state_stack.back());
                if (st != nullptr) {
                    st->traceability_reqs.push_back(req_id);
                }
            }
            return true;
        }

        // 10. Temporal Logic Specifications: assert property <Name> : <Formula>
        static const std::regex assert_prop_regex(
            R"(^(?:assert\s+)?property\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(.+)$)", std::regex::optimize);
        if (std::regex_search(stmt, match, assert_prop_regex)) {
            FormalProperty prop;
            prop.name = sanitize_identifier(match[1].str());
            prop.raw_formula = trim(match[2].str());
            prop.ast = LtlPropertyParser::parse(prop.raw_formula);
            prop.id = compute_deterministic_id(prop.name + ":" + prop.raw_formula);
            model.add_property(std::move(prop));
            return true;
        }

        // 11. Transitions: transition [<Name>] [first <Src>] [accept <Evt>] [if <Guard>] [do <Act>] [then <Dst>]
        if (stmt.rfind("transition", 0) == 0 || stmt.find("accept") != std::string::npos ||
            stmt.find("then") != std::string::npos || stmt.find("first") != std::string::npos ||
            stmt.find("after") != std::string::npos) {
            return parse_transition_statement(stmt, model, state_stack);
        }

        return true;
    }

    static bool parse_transition_statement(const std::string& stmt, FsmIr& model,
                                           const std::vector<std::string>& state_stack) {
        std::string source;
        std::string target;
        std::string event;
        std::string guard;
        std::string action;
        std::uint32_t priority = 0;
        std::optional<TimeTrigger> time_trigger;

        static const std::regex prio_regex(R"(\b(?:priority|prio)\s*=?\s*(\d+))", std::regex::optimize);
        std::smatch prio_match;
        if (std::regex_search(stmt, prio_match, prio_regex)) {
            try {
                priority = static_cast<std::uint32_t>(std::stoul(prio_match[1].str()));
            } catch (const std::exception&) {
                priority = 0;
            }
        }

        static const std::regex first_regex(R"(\b(?:first|from)\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
        static const std::regex accept_regex(
            R"(\b(?:accept|when)\s+(?:[A-Za-z_][A-Za-z0-9_]*\s*:\s*)?([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
        static const std::regex after_regex(
            R"(\bafter\s+(\d+(?:\.\d+)?)\s*(?:\[(?:SI::|ISQ::)?([A-Za-z]+)\]|([A-Za-z]+))?)",
            std::regex::optimize);
        static const std::regex if_regex(R"(\bif\s+([A-Za-z0-9_!&\|\(\)\s]+?)(?=\s+(?:do|then|to|;|$)))",
                                         std::regex::optimize);
        static const std::regex do_block_regex(R"(\bdo\s*\{([^}]+)\})", std::regex::optimize);
        static const std::regex do_regex(R"(\bdo\s+(?:action\s+)?([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
        static const std::regex then_regex(R"(\b(?:then|to)\s+([A-Za-z_][A-Za-z0-9_\[\]\*]*))", std::regex::optimize);

        std::smatch match;
        if (std::regex_search(stmt, match, first_regex)) {
            source = sanitize_identifier(match[1].str());
        } else if (!state_stack.empty()) {
            source = state_stack.back();
        }

        if (std::regex_search(stmt, match, accept_regex)) {
            event = sanitize_identifier(match[1].str());
        } else if (std::regex_search(stmt, match, after_regex)) {
            double raw_val = 0.0;
            try {
                raw_val = std::stod(match[1].str());
            } catch (const std::exception&) {
                raw_val = 1.0;
            }
            std::string unit = match[2].matched ? match[2].str() : (match[3].matched ? match[3].str() : "ms");
            uint64_t duration_ms = static_cast<uint64_t>(raw_val);
            if (unit == "s" || unit == "sec" || unit == "seconds") {
                duration_ms = static_cast<uint64_t>(raw_val * 1000.0);
            } else if (unit == "min") {
                duration_ms = static_cast<uint64_t>(raw_val * 60000.0);
            } else if (unit == "h") {
                duration_ms = static_cast<uint64_t>(raw_val * 3600000.0);
            }
            if (duration_ms == 0) duration_ms = 1;
            time_trigger = TimeTrigger(TimeTriggerKind::After, duration_ms, TimeUnit::Milliseconds);
            event = "after_" + std::to_string(duration_ms) + "ms";
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

        if (std::regex_search(stmt, match, do_block_regex)) {
            action = sanitize_identifier(trim(match[1].str()));
        } else if (std::regex_search(stmt, match, do_regex)) {
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
        if (time_trigger.has_value()) {
            trans.trigger = *time_trigger;
        }
        if (!guard.empty()) {
            trans.guard = guard;
        }
        if (!action.empty()) {
            trans.action = action;
        }
        trans.target_is_history = target_is_history;
        trans.target_is_deep_history = target_is_deep_history;
        trans.priority = priority;
        trans.parent_scope = state_stack.empty() ? "" : state_stack.back();

        if (source == target && !event.empty() && stmt.find("then") == std::string::npos &&
            stmt.find("to") == std::string::npos) {
            trans.kind = TransitionEdgeKind::Internal;
        } else {
            trans.kind = TransitionEdgeKind::External;
        }

        if (!model.is_choice_node(source)) {
            model.add_or_get_state(source, "");
        }
        if (!model.is_choice_node(target)) {
            model.add_or_get_state(target, "");
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

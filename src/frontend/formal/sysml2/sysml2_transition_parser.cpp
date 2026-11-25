#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

#include "fsm/frontend/directive/guard_parser.hpp"
#include "fsm/frontend/formal/sysml2/sysml2_parser.hpp"
#include "fsm/ir/guard.hpp"
#include "fsm/ir/identifier_utils.hpp"
#include "sysml2_action_lowering.hpp"

namespace fsm::frontend::formal {

using namespace fsm::ir;
using namespace sysml2_detail;

bool Sysml2Parser::parse_transition_statement(const std::string& stmt, FsmIr& model,
                                              const std::vector<std::string>& state_stack,
                                              const Sysml2SymbolResolver* symbol_resolver) {
    std::string source;
    std::string target;
    std::string event;
    std::string guard;
    std::string action;
    std::uint32_t priority = 0;
    std::optional<TimeTrigger> time_trigger;
    std::optional<ChangeTrigger> change_trigger;

    // 1. Optional transition name matching
    static const std::regex local_prefix_regex(R"(^local\s+transition\b)", std::regex::optimize);
    static const std::regex trans_name_regex(R"(^(?:local\s+)?transition\s+([A-Za-z_][A-Za-z0-9_]*))",
                                             std::regex::optimize);
    std::string trans_name;
    std::smatch match;
    if (std::regex_search(stmt, match, trans_name_regex)) {
        std::string name_candidate = sanitize_identifier(match[1].str());
        if (name_candidate != "from" && name_candidate != "first" && name_candidate != "accept" &&
            name_candidate != "if" && name_candidate != "do" && name_candidate != "then" && name_candidate != "fork" &&
            name_candidate != "join") {
            trans_name = name_candidate;
        }
    }

    // 2. Transition priority extraction (e.g., 'priority = 10' or 'prio 1')
    static const std::regex prio_regex(R"(\b(?:priority|prio)\s*=?\s*(\d+))", std::regex::optimize);
    std::smatch prio_match;
    if (std::regex_search(stmt, prio_match, prio_regex)) {
        try {
            priority = static_cast<std::uint32_t>(std::stoul(prio_match[1].str()));
        } catch (...) {
        }
    }

    // Regular expressions for individual transition components
    static const std::regex first_regex(R"(\b(?:first|from)\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    static const std::regex after_regex(
        R"(\b(?:accept\s+)?after\s*(?:\(\s*(\d+(?:\.\d+)?)\s*(?:\[(?:SI::|ISQ::)?([A-Za-z]+)\]|([A-Za-z]+))?\s*\)|(\d+(?:\.\d+)?)\s*(?:\[(?:SI::|ISQ::)?([A-Za-z]+)\]|([A-Za-z]+))?))",
        std::regex::optimize);
    static const std::regex every_regex(
        R"(\b(?:accept\s+)?every\s*(?:\(\s*(\d+(?:\.\d+)?)\s*(?:\[(?:SI::|ISQ::)?([A-Za-z]+)\]|([A-Za-z]+))?\s*\)|(\d+(?:\.\d+)?)\s*(?:\[(?:SI::|ISQ::)?([A-Za-z]+)\]|([A-Za-z]+))?))",
        std::regex::optimize);
    static const std::regex at_regex(R"(\b(?:accept\s+)?at\b\s*(?:\(\s*([^)]+)\s*\)|([A-Za-z0-9_:]+)))",
                                     std::regex::optimize);
    static const std::regex change_when_regex(R"(\b(?:accept\s+)?when\s*\(([^)]+)\))", std::regex::optimize);
    static const std::regex accept_regex(
        R"(\b(?:accept|when)\s+(?:([A-Za-z_][A-Za-z0-9_]*)\s*:\s*)?([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    static const std::regex if_regex(R"(\bif\s+([^;]+?)(?=\s+(?:do\b|then\b|to\b)|;|$))", std::regex::optimize);
    static const std::regex do_block_regex(R"(\bdo\s*(?:action\s+)?(?:([A-Za-z_][A-Za-z0-9_]*)\s*)?\{([^}]+)\})",
                                           std::regex::optimize);
    static const std::regex do_regex(R"(\bdo\s+(?:action\s+)?([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    static const std::regex then_regex(R"(\b(?:then|to)\s+([A-Za-z_][A-Za-z0-9_.\[\]\*]*))", std::regex::optimize);

    // 3. Source state detection (explicit 'first/from <State>' or inherited from parent scope)
    if (std::regex_search(stmt, match, first_regex)) {
        source = sanitize_identifier(match[1].str());
    } else if (!state_stack.empty()) {
        source = state_stack.back();
    }

    auto parse_duration_ms_match = [](const std::smatch& m) -> uint64_t {
        std::string val_str = m[1].matched ? m[1].str() : m[4].str();
        std::string unit_str = m[2].matched   ? m[2].str()
                               : m[3].matched ? m[3].str()
                               : m[5].matched ? m[5].str()
                               : m[6].matched ? m[6].str()
                                              : "ms";
        double raw_val = 0.0;
        try {
            raw_val = std::stod(val_str);
        } catch (const std::exception&) {
            raw_val = 1.0;
        }
        double duration_ms_f = raw_val;
        if (unit_str == "s" || unit_str == "sec" || unit_str == "seconds") {
            duration_ms_f = raw_val * 1000.0;
        } else if (unit_str == "min") {
            duration_ms_f = raw_val * 60000.0;
        } else if (unit_str == "h") {
            duration_ms_f = raw_val * 3600000.0;
        } else if (unit_str == "us" || unit_str == "usec" || unit_str == "microseconds") {
            duration_ms_f = raw_val / 1000.0;
        } else if (unit_str == "ns" || unit_str == "nsec" || unit_str == "nanoseconds") {
            duration_ms_f = raw_val / 1000000.0;
        }
        uint64_t duration_ms = static_cast<uint64_t>(duration_ms_f + 0.5);
        return duration_ms == 0 ? 1 : duration_ms;
    };

    // 4. Trigger resolution: Time triggers (after, every, at) or Event triggers (accept, when)
    if (std::regex_search(stmt, match, after_regex)) {
        const uint64_t duration_ms = parse_duration_ms_match(match);
        time_trigger = TimeTrigger(TimeTriggerKind::After, duration_ms, TimeUnit::Milliseconds);
        event = "after_" + std::to_string(duration_ms) + "ms";
    } else if (std::regex_search(stmt, match, every_regex)) {
        const uint64_t duration_ms = parse_duration_ms_match(match);
        time_trigger = TimeTrigger(TimeTriggerKind::Every, duration_ms, TimeUnit::Milliseconds);
        event = "every_" + std::to_string(duration_ms) + "ms";
    } else if (std::regex_search(stmt, match, at_regex)) {
        std::string at_target = match[1].matched ? match[1].str() : match[2].str();
        time_trigger = TimeTrigger(TimeTriggerKind::At, 0, TimeUnit::Milliseconds);
        time_trigger->dynamic_expression = trim(at_target);
        event = "at_" + sanitize_identifier(at_target);
    } else if (std::regex_search(stmt, match, change_when_regex)) {
        change_trigger = ChangeTrigger(trim(match[1].str()), true);
        event.clear();
    } else if (std::regex_search(stmt, match, accept_regex)) {
        event = sanitize_identifier(match[2].str());
    }

    // 5. Guard expression resolution (composite boolean expressions or named guard contracts)
    if (std::regex_search(stmt, match, if_regex)) {
        const std::string raw_guard_expr = trim(match[1].str());
        auto parsed = directive::GuardExpressionParser::parse(raw_guard_expr);
        if (!parsed.canonical_expression.empty()) {
            guard = parsed.canonical_expression;
            for (const auto& detail : parsed.atomic_guard_details) {
                model.add_guard(detail.name, "", detail.expression, detail.expression);
            }
            for (const auto& atomic : parsed.atomic_guards) {
                model.add_guard(atomic);
            }
        } else if (!trans_name.empty()) {
            guard = to_pascal_case(trans_name) + "Guard";
            model.add_guard(guard, "", raw_guard_expr);
        } else {
            guard = "TransitionGuard_" + std::to_string(model.transitions.size() + 1);
            model.add_guard(guard, "", raw_guard_expr);
        }
    }

    // 6. Action effects and variable assignments: do { ... }, do send via port, or do action_name
    std::vector<ActionAssignment> assignments;
    std::vector<ActionAstNode> service_calls;
    std::optional<ActionSignature> trans_send_action;
    LocalItemMap local_items;
    std::string explicit_action_name;
    std::string block_content;
    if (extract_do_action_block(stmt, explicit_action_name, block_content)) {
        for (const auto& statement : split_action_block_statements(block_content)) {
            if (statement.empty()) {
                continue;
            }
            if (try_parse_local_item_usage(statement, local_items)) {
                continue;
            }
            SignalEmitOp emit_op;
            std::string send_act_name;
            std::string send_inv;
            if (try_parse_send_statement(statement, local_items, model, emit_op, send_act_name, send_inv)) {
                if (action.empty()) {
                    action = send_act_name;
                }
                service_calls.emplace_back(std::move(emit_op), std::move(send_inv));
            } else if (try_parse_service_call_statement(statement, model, service_calls)) {
                // Lowered directly into typed ActionCallOp
            } else if (!try_parse_assignment_statement(statement, assignments, &model)) {
                // Standalone action method call
                std::string act_call = sanitize_identifier(statement);
                if (!act_call.empty()) {
                    action = act_call;
                    model.add_action(act_call);
                }
            }
        }

        // Materialize synthesized action signatures from variable assignments or typed service calls
        if (!assignments.empty() || !service_calls.empty()) {
            if (!explicit_action_name.empty()) {
                action = explicit_action_name;
            } else if (!assignments.empty() && assignments.size() == 1 && service_calls.empty()) {
                const auto& a = assignments[0];
                if (a.expression == a.target.name + " + 1" || a.expression == a.target.name + " + 1.0") {
                    action = "increment_" + a.target.name;
                } else if (a.expression == a.target.name + " - 1" || a.expression == a.target.name + " - 1.0") {
                    action = "decrement_" + a.target.name;
                } else if (!trans_name.empty()) {
                    action = to_pascal_case(trans_name) + "Action_" + a.target.name;
                } else {
                    action = "assign_" + a.target.name;
                }
            } else if (!trans_name.empty()) {
                action = to_pascal_case(trans_name) + "Action";
            } else {
                action = "update_state_vars";
            }
            if (explicit_action_name.empty()) {
                auto has_action = [&](const std::string& candidate) {
                    for (const auto& existing : model.actions) {
                        if (existing.name == candidate) {
                            return true;
                        }
                    }
                    return false;
                };
                if (has_action(action)) {
                    const std::string base = action;
                    std::size_t suffix = 2;
                    while (has_action(base + "_" + std::to_string(suffix))) {
                        ++suffix;
                    }
                    action = base + "_" + std::to_string(suffix);
                }
            }
            model.add_action(action);
        }
    } else {
        static const std::regex trans_send_slice_re(
            R"(\b(?:do\s+)?(send\s+(?:new\s+)?[A-Za-z_][A-Za-z0-9_]*(?:\s*\([^)]*\))?\s+(?:via|to)\s+[A-Za-z_][A-Za-z0-9_.]*))",
            std::regex::optimize);
        if (std::regex_search(stmt, match, trans_send_slice_re)) {
            SignalEmitOp emit_op;
            std::string send_inv;
            if (try_parse_send_statement(match[1].str(), local_items, model, emit_op, action, send_inv)) {
                ActionSignature sig_action(action, std::move(send_inv));
                sig_action.instructions.emplace_back(std::move(emit_op));
                trans_send_action = std::move(sig_action);
            }
        } else if (std::regex_search(stmt, match, do_regex)) {
            // Direct action invocation
            action = sanitize_identifier(match[1].str());
        }
    }

    // 7. Target state and History pseudostate detection ([H], [H*], .H, .H*)
    bool target_is_history = false;
    bool target_is_deep_history = false;

    if (std::regex_search(stmt, match, then_regex)) {
        const std::string raw_target = match[1].str();
        auto ends_with = [](const std::string& s, const std::string& suffix) {
            return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        if (const auto* hist =
                symbol_resolver != nullptr ? symbol_resolver->find_history_pseudostate(raw_target) : nullptr) {
            target = hist->parent_state;
            target_is_history = true;
            target_is_deep_history = hist->is_deep;
        } else if (raw_target.find("[H*]") != std::string::npos ||
                   raw_target.find("[deep_history]") != std::string::npos || ends_with(raw_target, ".H*") ||
                   ends_with(raw_target, ".deep_history")) {
            target_is_deep_history = true;
            target_is_history = true;
            const auto pos = raw_target.find_first_of("[.");
            target = sanitize_identifier(raw_target.substr(0, pos));
        } else if (raw_target.find("[H]") != std::string::npos || raw_target.find("[history]") != std::string::npos ||
                   ends_with(raw_target, ".H") || ends_with(raw_target, ".history")) {
            target_is_history = true;
            const auto pos = raw_target.find_first_of("[.");
            target = sanitize_identifier(raw_target.substr(0, pos));
        } else {
            target = sanitize_identifier(raw_target);
        }
    }

    static const std::regex fork_regex(R"(\bfork\s*\(\s*([^)]+)\s*\))", std::regex::optimize);
    static const std::regex join_regex(R"(\bjoin\s*\(\s*([^)]+)\s*\))", std::regex::optimize);
    std::vector<std::string> fork_targets;
    std::vector<std::string> join_sources;

    if (std::regex_search(stmt, match, fork_regex)) {
        std::string raw_list = match[1].str();
        std::stringstream ss(raw_list);
        std::string item;
        while (std::getline(ss, item, ',')) {
            std::string cleaned = sanitize_identifier(trim(item));
            if (!cleaned.empty()) {
                fork_targets.push_back(cleaned);
            }
        }
        if (!fork_targets.empty()) {
            target = fork_targets.front();
        }
    }

    if (std::regex_search(stmt, match, join_regex)) {
        std::string raw_list = match[1].str();
        std::stringstream ss(raw_list);
        std::string item;
        while (std::getline(ss, item, ',')) {
            std::string cleaned = sanitize_identifier(trim(item));
            if (!cleaned.empty()) {
                join_sources.push_back(cleaned);
            }
        }
        if (!join_sources.empty()) {
            source = join_sources.front();
        }
    }

    // Normalize implicit self-transitions and source/target defaults
    if (source.empty() && !target.empty()) {
        source = target;
    }
    if (target.empty() && !source.empty()) {
        target = source;  // internal transition
    }

    if (source.empty() || target.empty()) {
        return true;
    }

    // 8. Ensure states, choice nodes, and signals are synchronized in the model first
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
    for (const auto& t_name : fork_targets) {
        if (!model.is_choice_node(t_name)) {
            model.add_or_get_state(t_name, "");
        }
    }
    for (const auto& s_name : join_sources) {
        if (!model.is_choice_node(s_name)) {
            model.add_or_get_state(s_name, "");
        }
    }

    // 9. Construct the canonical TransitionEdge representation
    TransitionEdge trans;
    trans.source = source;
    trans.target = target;
    if (const auto* s = model.find_state_by_name(source)) {
        trans.source_id = s->id;
    }
    if (const auto* t = model.find_state_by_name(target)) {
        trans.target_id = t->id;
    }
    if (!fork_targets.empty()) {
        trans.target_ids = fork_targets;
        for (const auto& t_name : fork_targets) {
            if (const auto* t = model.find_state_by_name(t_name)) {
                trans.multi_target_ids.push_back(t->id);
            }
        }
    }
    if (!join_sources.empty()) {
        trans.source_ids = join_sources;
        for (const auto& s_name : join_sources) {
            if (const auto* s = model.find_state_by_name(s_name)) {
                trans.multi_source_ids.push_back(s->id);
            }
        }
    }
    trans.event = event;
    if (time_trigger.has_value()) {
        trans.trigger = *time_trigger;
    } else if (change_trigger.has_value()) {
        trans.trigger = *change_trigger;
    }
    if (!guard.empty()) {
        trans.set_guard(guard);
    }
    if (trans_send_action.has_value()) {
        trans.transition_action = std::move(trans_send_action);
        model.add_action(action);
    } else if (!action.empty() || !assignments.empty() || !service_calls.empty()) {
        ActionSignature sig;
        sig.name = action;
        sig.assignments = assignments;
        sig.instructions = std::move(service_calls);
        trans.transition_action = std::move(sig);
        model.add_action(action);
    }
    trans.target_is_history = target_is_history;
    trans.target_is_deep_history = target_is_deep_history;
    trans.priority = priority;
    trans.parent_scope = state_stack.empty() ? "" : state_stack.back();
    if (trans.parent_scope.empty() && !trans.source.empty()) {
        if (const auto* src_st = model.find_state(trans.source); src_st != nullptr && !src_st->parent_state.empty()) {
            trans.parent_scope = src_st->parent_state;
        }
    }

    // Distinguish internal, local, and external transitions
    if (std::regex_search(stmt, local_prefix_regex)) {
        trans.kind = TransitionEdgeKind::Local;
    } else if (source == target && !event.empty() && !std::regex_search(stmt, then_regex)) {
        trans.kind = TransitionEdgeKind::Internal;
    } else {
        trans.kind = TransitionEdgeKind::External;
    }

    if (!event.empty()) {
        model.add_event(event);
    }
    if (!action.empty()) {
        model.add_action(action);
    }

    // 10. Register completed transition edge into the model
    model.add_transition(std::move(trans));
    return true;
}

}  // namespace fsm::frontend::formal

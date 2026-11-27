#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/frontend/directive/ltl_parser.hpp"
#include "fsm/frontend/formal/sysml2/sysml2_parser.hpp"
#include "fsm/ir/guard.hpp"
#include "fsm/ir/identifier_utils.hpp"
#include "sysml2_action_lowering.hpp"

namespace fsm::frontend::formal {

using namespace fsm::ir;
using namespace sysml2_detail;

bool Sysml2Parser::process_statement(const std::string& raw_stmt, FsmIr& model, std::vector<std::string>& state_stack,
                                     std::vector<std::string>& package_stack,
                                     std::vector<std::size_t>& package_push_counts, std::string& current_item_def,
                                     std::string& current_enum_def, std::string& current_struct_def,
                                     std::string& current_calc_def, std::string& error_message, size_t line_number,
                                     bool is_block_open, SysmlBlockKind& out_kind,
                                     Sysml2SymbolResolver& symbol_resolver) {
    const std::string stmt = normalize_whitespace(raw_stmt);
    out_kind = SysmlBlockKind::ActionBlock;
    if (stmt.empty()) {
        return true;
    }

    // Native OMG SysML v2.0 calc def body: in <param> : <Type>; / return [<name>] : <Type>;
    if (!current_calc_def.empty()) {
        std::smatch cm;
        ServiceMethodSignature* active_svc = nullptr;
        for (auto& svc : model.services) {
            if (svc.name == current_calc_def) {
                active_svc = &svc;
                break;
            }
        }
        if (active_svc != nullptr) {
            static const std::regex calc_param_regex(
                R"(^in\s+(?:attribute\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z0-9_:]+))", std::regex::optimize);
            static const std::regex calc_return_regex(R"(^return(?:\s+[A-Za-z_][A-Za-z0-9_]*)?\s*:\s*([A-Za-z0-9_:]+))",
                                                      std::regex::optimize);
            if (std::regex_search(stmt, cm, calc_param_regex)) {
                const std::string p_name = sanitize_identifier(cm[1].str());
                const DataType p_type = DataType::from_string(map_sysml_type_to_cpp(cm[2].str(), &symbol_resolver));
                active_svc->parameters.emplace_back(p_name, p_type);
                return true;
            }
            if (std::regex_search(stmt, cm, calc_return_regex)) {
                active_svc->return_type = DataType::from_string(map_sysml_type_to_cpp(cm[1].str(), &symbol_resolver));
                return true;
            }
        }
        return true;
    }

    // Binding connectors: bind <dst> = <src>; or bind <dst> to <src>; or connect <dst> to <src>;
    static const std::regex bind_regex(
        R"(^(?:bind\s+([A-Za-z_][A-Za-z0-9_.]*)\s*(?:=|to)\s*([A-Za-z_][A-Za-z0-9_.]*)|connect\s+([A-Za-z_][A-Za-z0-9_.]*)\s+to\s+([A-Za-z_][A-Za-z0-9_.]*)))",
        std::regex::optimize);
    std::smatch match;
    if (std::regex_search(stmt, match, bind_regex)) {
        std::string dst = match[1].matched ? match[1].str() : match[3].str();
        std::string src = match[2].matched ? match[2].str() : match[4].str();
        model.add_binding(BindingConnector{dst, src});
        if (auto* p_dst = model.find_port_mut(dst)) {
            p_dst->bound_to = src;
        }
        if (auto* p_src = model.find_port_mut(src)) {
            p_src->bound_to = dst;
        }
        return true;
    }

    // Structural statement filtering (allocate, part usage, etc.)
    if (Sysml2BlockScanner::is_structural_statement(stmt)) {
        return true;
    }

    // Import handling (e.g. private import ScalarValues::*, public import MyPkg::*)
    static const std::regex import_stmt_regex(R"(^(?:(?:private|public|protected)\s+)?(import\s+.+)$)",
                                              std::regex::optimize);
    if (std::regex_match(stmt, match, import_stmt_regex)) {
        symbol_resolver.register_import(match[1].str());
        return true;
    }

    // 1. package <Name> (supporting Outer::Inner and nesting)
    static const std::regex package_def_regex(R"(^package\s+([A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*))",
                                              std::regex::optimize);
    if (std::regex_search(stmt, match, package_def_regex)) {
        std::string raw_pkg = match[1].str();
        size_t push_count = 0;
        size_t start = 0;
        while (start < raw_pkg.size()) {
            size_t end = raw_pkg.find("::", start);
            if (end == std::string::npos)
                end = raw_pkg.size();
            std::string part = sanitize_identifier(raw_pkg.substr(start, end - start));
            if (!part.empty()) {
                package_stack.push_back(part);
                push_count++;
            }
            start = end + 2;
        }
        std::string joined;
        for (size_t k = 0; k < package_stack.size(); ++k) {
            if (k > 0)
                joined += "::";
            joined += package_stack[k];
        }
        model.package = joined;

        if (is_block_open) {
            out_kind = SysmlBlockKind::Package;
            package_push_counts.push_back(push_count);
        }
        return true;
    }

    static const std::regex state_def_regex(R"(^state\s+def\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    if (std::regex_search(stmt, match, state_def_regex)) {
        model.name = sanitize_identifier(match[1].str());
        if (!package_stack.empty() && model.package.empty()) {
            std::string joined;
            for (size_t k = 0; k < package_stack.size(); ++k) {
                if (k > 0)
                    joined += "::";
                joined += package_stack[k];
            }
            model.package = joined;
        }
        if (is_block_open) {
            out_kind = SysmlBlockKind::StateDef;
        }
        return true;
    }

    // 2a. Enum Definition: enum def <Name> [ :> <Underlying> ]
    static const std::regex enum_def_regex(R"(^enum\s+def\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*:>\s*([A-Za-z0-9_:]+))?)",
                                           std::regex::optimize);
    if (std::regex_search(stmt, match, enum_def_regex)) {
        const std::string enum_name = sanitize_identifier(match[1].str());
        const std::string underlying =
            match[2].matched ? map_sysml_type_to_cpp(match[2].str(), &symbol_resolver) : "uint8_t";
        EnumDefinition enum_def(enum_name, underlying);
        model.add_enum(std::move(enum_def));
        if (is_block_open) {
            current_enum_def = enum_name;
            out_kind = SysmlBlockKind::EnumDef;
        }
        return true;
    }

    // 2a-2. Enum Literal: [enum] <LitName> [ = <val> ]
    static const std::regex enum_lit_regex(R"(^(?:enum\s+)?([A-Za-z_][A-Za-z0-9_]*)(?:\s*=\s*(-?\d+))?$)",
                                           std::regex::optimize);
    if (!current_enum_def.empty() && std::regex_match(stmt, match, enum_lit_regex)) {
        const std::string lit_name = sanitize_identifier(match[1].str());
        if (lit_name == "doc" || lit_name == "comment" || lit_name == "attribute") {
            return true;
        }
        std::optional<int64_t> val = std::nullopt;
        if (match[2].matched) {
            try {
                val = std::stoll(match[2].str());
            } catch (...) {
            }
        }
        if (auto* en = model.find_enum_mut(current_enum_def)) {
            en->add_literal(EnumLiteral(lit_name, val));
        }
        return true;
    }

    // 2a-3. Struct / Datatype Definition: (struct def|datatype def) <Name>
    static const std::regex struct_def_regex(R"(^(struct\s+def|datatype\s+def)\s+([A-Za-z_][A-Za-z0-9_]*))",
                                             std::regex::optimize);
    if (std::regex_search(stmt, match, struct_def_regex)) {
        bool is_dt = (match[1].str().find("datatype") != std::string::npos);
        const std::string struct_name = sanitize_identifier(match[2].str());
        StructDefinition struct_def(struct_name, is_dt);
        model.add_struct(std::move(struct_def));
        if (is_block_open) {
            current_struct_def = struct_name;
            out_kind = SysmlBlockKind::StructDef;
        }
        return true;
    }

    // 2a-calc. Native OMG SysML v2.0 Calculation Definition -> pure typed ServiceMethodSignature
    static const std::regex calc_def_regex(R"(^calc\s+def\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    if (std::regex_search(stmt, match, calc_def_regex)) {
        const std::string calc_name = sanitize_identifier(match[1].str());
        model.add_service(ServiceMethodSignature(calc_name, {}, DataType::from_string("void"), /*pure=*/true,
                                                 /*noexcept=*/true, ""));
        if (is_block_open) {
            current_calc_def = calc_name;
            out_kind = SysmlBlockKind::CalcDef;
        }
        return true;
    }

    // 2a-calc-usage. Native OMG SysML v2.0 CalculationUsage (`[abstract] calc <GuardName> { return [result] : Boolean
    // ... }`)
    static const std::regex calc_usage_regex(R"(^(?:abstract\s+)?calc\s+(?!def\b)([A-Za-z_][A-Za-z0-9_]*)\b)",
                                             std::regex::optimize);
    if (std::regex_search(stmt, match, calc_usage_regex)) {
        const std::string calc_name = sanitize_identifier(match[1].str());
        if (!calc_name.empty()) {
            model.add_guard(calc_name);
        }
        return true;
    }

    // 2a-ref-port. Reference port inside `state def` (`ref port <name> : [~]<PortDef>;`) — handled by workspace binder
    if (stmt.rfind("ref port ", 0) == 0) {
        return true;
    }

    // 2b. Directional parameter / port declaration:
    //     OMG SysML v2.0: (in|out|inout) [attribute|port] <name> : <Type> [ { assert constraint { <expr> } } ]
    static const std::regex port_regex(
        R"(^(in|out|inout)\s+(?:(?:port|attribute)\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z0-9_:]+))",
        std::regex::optimize);
    if (std::regex_search(stmt, match, port_regex)) {
        std::string dir_str = match[1].str();
        std::string port_name = sanitize_identifier(match[2].str());
        std::string raw_type = match[3].str();
        std::string cpp_type = map_sysml_type_to_cpp(raw_type, &symbol_resolver);
        std::string constraint_str;
        auto assert_pos = stmt.find("assert");
        if (assert_pos != std::string::npos) {
            auto c_start = stmt.find('{', assert_pos);
            auto c_end = stmt.rfind('}');
            if (c_start != std::string::npos && c_end != std::string::npos && c_end > c_start) {
                constraint_str = trim(stmt.substr(c_start + 1, c_end - c_start - 1));
                while (!constraint_str.empty() && constraint_str.front() == '{')
                    constraint_str = trim(constraint_str.substr(1));
                while (!constraint_str.empty() && constraint_str.back() == '}')
                    constraint_str = trim(constraint_str.substr(0, constraint_str.size() - 1));
            }
        }

        PortDirection dir = string_to_port_direction(dir_str);
        std::optional<double> min_val = std::nullopt;
        std::optional<double> max_val = std::nullopt;

        if (!constraint_str.empty()) {
            const std::regex min_re("(?:self|that|" + port_name + R"()\s*>=\s*(-?[0-9]+(?:\.[0-9]+)?))");
            const std::regex max_re("(?:self|that|" + port_name + R"()\s*<=\s*(-?[0-9]+(?:\.[0-9]+)?))");
            std::smatch m_min, m_max;
            if (std::regex_search(constraint_str, m_min, min_re)) {
                try {
                    min_val = std::stod(m_min[1].str());
                } catch (...) {
                }
            }
            if (std::regex_search(constraint_str, m_max, max_re)) {
                try {
                    max_val = std::stod(m_max[1].str());
                } catch (...) {
                }
            }
        }

        PortDefinition port_def(port_name, map_sysml_to_data_type(raw_type, &symbol_resolver), dir, min_val, max_val,
                                constraint_str);
        model.add_port(std::move(port_def));
        return true;
    }

    // 2c. Action definition: action def <Name>; or action def <Name>
    static const std::regex action_def_regex(R"(^action\s+def\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    if (std::regex_search(stmt, match, action_def_regex)) {
        const std::string act_name = sanitize_identifier(match[1].str());
        model.add_action(act_name);
        return true;
    }

    // 2. Item / Signal Definition (Signal with Payload): item def / signal def / event def / attribute def <Name>
    static const std::regex item_def_regex(
        R"(^(?:item\s+def|signal\s+def|event\s+def|attribute\s+def|port\s+def)\s+([A-Za-z_][A-Za-z0-9_]*))",
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

    // 2d. Assert constraint inside an Item / Signal Definition: assert constraint { <expr> }
    if (!current_item_def.empty()) {
        static const std::regex item_constraint_regex(
            R"(^(?:assert\s+)?constraint\s*(?:[A-Za-z_][A-Za-z0-9_]*\s*)?\{(.+)\}\s*;?$)", std::regex::optimize);
        if (std::regex_search(stmt, match, item_constraint_regex)) {
            std::string expr = trim(match[1].str());
            while (!expr.empty() && expr.back() == ';') {
                expr = trim(expr.substr(0, expr.size() - 1));
            }
            expr = std::regex_replace(expr, std::regex(R"(\band\b)"), "&&");
            expr = std::regex_replace(expr, std::regex(R"(\bor\b)"), "||");
            expr = std::regex_replace(expr, std::regex(R"(\bnot\b\s*)"), "!");
            if (!expr.empty()) {
                for (auto& sig : model.signals) {
                    if (sig.name == current_item_def) {
                        sig.validators.push_back(std::move(expr));
                        break;
                    }
                }
            }
            return true;
        }
    }

    // 3. Attribute / Variable declaration: attribute <name> : <Type> [[unit]] [:=|= <init>] / var <name> : <Type>
    static const std::regex attr_regex(
        R"(^(?:attribute|var)\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z0-9_:]+)(?:\s*(\[[^\]]+\]))?(?:\s*:?=\s*([^;]+))?)",
        std::regex::optimize);
    if (std::regex_search(stmt, match, attr_regex)) {
        const std::string attr_name = sanitize_identifier(match[1].str());
        const std::string raw_type = match[2].str();
        const DataType canonical_dt = map_sysml_to_data_type(raw_type, &symbol_resolver);
        const std::string unit_str = match[3].matched ? trim(match[3].str()) : "";
        const std::string init_val = match[4].matched ? trim(match[4].str()) : "";

        if (!current_struct_def.empty()) {
            // Member field inside a Struct / Datatype Definition
            if (auto* st = model.find_struct_mut(current_struct_def)) {
                std::string clean_unit = unit_str;
                if (clean_unit.size() >= 2 && clean_unit.front() == '[' && clean_unit.back() == ']') {
                    clean_unit = clean_unit.substr(1, clean_unit.size() - 2);
                }
                std::optional<std::string> unit_opt =
                    clean_unit.empty() ? std::nullopt : std::make_optional(clean_unit);
                StructField sf(attr_name, canonical_dt, init_val, unit_opt);
                st->add_field(std::move(sf));
            }
        } else if (!current_item_def.empty()) {
            // Member attribute inside a Signal / Item Definition
            for (auto& sig : model.signals) {
                if (sig.name == current_item_def) {
                    SignalAttribute sa(attr_name, canonical_dt, init_val);
                    sig.attributes.push_back(std::move(sa));
                    auto assert_pos = stmt.find("assert");
                    if (assert_pos != std::string::npos) {
                        auto c_start = stmt.find('{', assert_pos);
                        auto c_end = stmt.rfind('}');
                        if (c_start != std::string::npos && c_end != std::string::npos && c_end > c_start) {
                            std::string c_expr = trim(stmt.substr(c_start + 1, c_end - c_start - 1));
                            while (!c_expr.empty() && c_expr.front() == '{') {
                                c_expr = trim(c_expr.substr(1));
                            }
                            while (!c_expr.empty() && (c_expr.back() == '}' || c_expr.back() == ';')) {
                                c_expr = trim(c_expr.substr(0, c_expr.size() - 1));
                            }
                            c_expr = std::regex_replace(c_expr, std::regex(R"(\bself\b)"), attr_name);
                            c_expr = std::regex_replace(c_expr, std::regex(R"(\band\b)"), "&&");
                            c_expr = std::regex_replace(c_expr, std::regex(R"(\bor\b)"), "||");
                            c_expr = std::regex_replace(c_expr, std::regex(R"(\bnot\b\s*)"), "!");
                            if (!c_expr.empty()) {
                                sig.validators.push_back(std::move(c_expr));
                            }
                        }
                    }
                    break;
                }
            }
        } else {
            // Top-level EFSM state machine variable
            VariableDefinition var;
            var.name = attr_name;
            // Widen default unsigned Integer (uint32_t) to signed int32_t when initialized with a negative literal
            // to prevent C++17 brace-initialization narrowing errors (-Wnarrowing).
            if (canonical_dt.is_primitive() && canonical_dt.primitive() == PrimitiveTypeKind::UInt32 &&
                !init_val.empty() && init_val.front() == '-') {
                var.type = DataType::int32();
            } else {
                var.type = canonical_dt;
            }
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

    // 4c. Decide / Decision / Choice pseudostate node: decide <ChoiceName>; or choice <ChoiceName>;
    static const std::regex decide_regex(R"(^(?:decide|choice)\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    if (std::regex_search(stmt, match, decide_regex)) {
        const std::string choice_name = sanitize_identifier(match[1].str());
        const std::string parent_name = state_stack.empty() ? "" : state_stack.back();
        model.add_or_get_state(choice_name, parent_name, StateKind::Choice);
        model.add_choice_node(choice_name);
        return true;
    }

    // 4b. Submachine declaration: state <SubName> :> <SubmachineName>
    static const std::regex submachine_regex(
        R"(^(?:parallel\s+)?state\s+([A-Za-z_][A-Za-z0-9_]*)\s*:>\s*([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    if (std::regex_search(stmt, match, submachine_regex)) {
        const std::string state_name = sanitize_identifier(match[1].str());
        const std::string sub_name = sanitize_identifier(match[2].str());
        const std::string parent_name = state_stack.empty() ? "" : state_stack.back();
        auto& st = model.add_state(state_name, parent_name);
        st.submachine = SubmachineRef(sub_name);
        return true;
    }

    // 4d. History pseudostate declaration: [deep|shallow] history state <HistName>;
    static const std::regex history_state_decl_regex(
        R"(^(?:(deep|shallow)\s+)?history\s+state\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    if (std::regex_search(stmt, match, history_state_decl_regex)) {
        const bool is_deep = match[1].matched && match[1].str() == "deep";
        const std::string hist_name = sanitize_identifier(match[2].str());
        const std::string parent_name = state_stack.empty() ? "" : state_stack.back();
        if (!parent_name.empty()) {
            if (auto* pst = model.find_state_mut(parent_name)) {
                pst->has_history = true;
                if (is_deep) {
                    pst->has_deep_history = true;
                }
            }
            symbol_resolver.register_history_pseudostate(parent_name + "." + hist_name, parent_name, is_deep);
            symbol_resolver.register_history_pseudostate(hist_name, parent_name, is_deep);
        }
        return true;
    }

    // 5. state <Name> (composite or leaf, optional parallel)
    static const std::regex digit_state_decl_regex(R"(^(?:parallel\s+)?state\s+([0-9][A-Za-z0-9_]*))",
                                                   std::regex::optimize);
    if (std::regex_search(stmt, match, digit_state_decl_regex)) {
        error_message =
            "Invalid SysML v2 state identifier '" + match[1].str() + "': identifiers cannot start with a digit";
        return false;
    }
    static const std::regex state_decl_regex(R"(^(parallel\s+)?state\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+(parallel)\b)?)",
                                             std::regex::optimize);
    if (std::regex_search(stmt, match, state_decl_regex)) {
        bool is_parallel = match[1].matched || match[3].matched;
        const std::string state_name = sanitize_identifier(match[2].str());
        const std::string parent_name = state_stack.empty() ? "" : state_stack.back();
        auto* existing = model.find_state_mut(state_name);
        if (existing != nullptr) {
            existing->parent_state = parent_name;
            if (!parent_name.empty()) {
                existing->pinned_parent = true;
            }
            if (is_parallel) {
                existing->kind = StateKind::Parallel;
            }
        } else {
            auto& st = model.add_state(state_name, parent_name);
            if (!parent_name.empty()) {
                st.pinned_parent = true;
            }
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

    // 6. entry; then <State>; or initial; then <State>; or standalone then <State>;
    static const std::regex initial_regex(R"(^(?:(?:entry|initial)(?:\s*;)?\s*)?then\s+([A-Za-z_][A-Za-z0-9_]*)$)",
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

    // 7a-2. Fork and Join pseudostates: fork <Name>; / join <Name>;
    static const std::regex fork_join_regex(R"(^(fork|join)\s+([A-Za-z_][A-Za-z0-9_]*))", std::regex::optimize);
    if (std::regex_search(stmt, match, fork_join_regex)) {
        bool is_fork = (match[1].str() == "fork");
        const std::string name = sanitize_identifier(match[2].str());
        const std::string parent_name = state_stack.empty() ? "" : state_stack.back();
        model.add_or_get_state(name, parent_name, is_fork ? StateKind::Fork : StateKind::Join);
        return true;
    }

    // 7b. State Lifecycle Actions: entry action <Act>; / exit action <Act>;
    auto parse_lifecycle_action_block = [&](const std::string& block_content, const std::string& explicit_name,
                                            const std::string& default_name,
                                            std::vector<ActionSignature>& target_actions) {
        std::vector<ActionAssignment> assignments;
        std::vector<ActionAstNode> service_calls;
        std::vector<ActionAstNode> ordered_instructions;
        bool has_non_assign_inst = false;
        bool has_assign_after_inst = false;
        std::vector<std::string> standalone_calls;
        LocalItemMap local_items;

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
            const std::size_t prev_assign_sz = assignments.size();
            const std::size_t prev_sc_sz = service_calls.size();
            if (try_parse_send_statement(statement, local_items, model, emit_op, send_act_name, send_inv)) {
                has_non_assign_inst = true;
                ActionAstNode node(std::move(emit_op), std::move(send_inv));
                ordered_instructions.push_back(node);
                service_calls.push_back(std::move(node));
            } else if (try_parse_service_call_statement(statement, model, service_calls)) {
                // Lowered directly into typed ActionCallOp
                has_non_assign_inst = true;
                for (std::size_t si = prev_sc_sz; si < service_calls.size(); ++si) {
                    ordered_instructions.push_back(service_calls[si]);
                }
            } else if (try_parse_assignment_statement(statement, assignments, &model)) {
                if (has_non_assign_inst) {
                    has_assign_after_inst = true;
                }
                for (std::size_t ai = prev_assign_sz; ai < assignments.size(); ++ai) {
                    StoreOp st_op;
                    st_op.target = assignments[ai].target;
                    st_op.op = assignments[ai].op;
                    st_op.expression = assignments[ai].expression;
                    st_op.expr_ast = assignments[ai].expr_ast;
                    ordered_instructions.emplace_back(std::move(st_op), statement);
                }
            } else {
                std::string act_call = statement;
                if (act_call.ends_with("()")) {
                    act_call.resize(act_call.size() - 2);
                    act_call = trim(act_call);
                }
                act_call = sanitize_identifier(act_call);
                if (!act_call.empty()) {
                    standalone_calls.push_back(act_call);
                }
            }
        }
        if (has_assign_after_inst && !assignments.empty()) {
            service_calls = std::move(ordered_instructions);
            assignments.clear();
        }

        const bool has_assignments = !assignments.empty() || !service_calls.empty();
        if (has_assignments) {
            std::string act_name = !explicit_name.empty() ? explicit_name : default_name;
            ActionSignature sig(act_name);
            sig.assignments = std::move(assignments);
            sig.instructions = std::move(service_calls);
            model.add_action(act_name);
            target_actions.push_back(std::move(sig));
        }

        for (const auto& call : standalone_calls) {
            model.add_action(call);
            target_actions.emplace_back(call);
        }

        if (!has_assignments && standalone_calls.empty()) {
            std::string act_name = !explicit_name.empty() ? explicit_name : default_name;
            model.add_action(act_name);
            target_actions.emplace_back(act_name);
        }
    };

    static const std::regex entry_block_regex(
        R"(^entry(?:\s*\/)?(?:\s+do)?(?:\s+action)?(?:\s+([A-Za-z_][A-Za-z0-9_]*))?\s*\{([\s\S]*)\})",
        std::regex::optimize);
    if (std::regex_search(stmt, match, entry_block_regex)) {
        std::string explicit_name = match[1].matched ? sanitize_identifier(match[1].str()) : "";
        std::string default_name = !state_stack.empty() ? (state_stack.back() + "_entry") : "entry_action";
        if (!state_stack.empty()) {
            auto* st = model.find_state_mut(state_stack.back());
            if (st != nullptr) {
                parse_lifecycle_action_block(match[2].str(), explicit_name, default_name, st->entry_actions);
            }
        }
        return true;
    }

    static const std::regex exit_block_regex(
        R"(^exit(?:\s*\/)?(?:\s+do)?(?:\s+action)?(?:\s+([A-Za-z_][A-Za-z0-9_]*))?\s*\{([\s\S]*)\})",
        std::regex::optimize);
    if (std::regex_search(stmt, match, exit_block_regex)) {
        std::string explicit_name = match[1].matched ? sanitize_identifier(match[1].str()) : "";
        std::string default_name = !state_stack.empty() ? (state_stack.back() + "_exit") : "exit_action";
        if (!state_stack.empty()) {
            auto* st = model.find_state_mut(state_stack.back());
            if (st != nullptr) {
                parse_lifecycle_action_block(match[2].str(), explicit_name, default_name, st->exit_actions);
            }
        }
        return true;
    }

    static const std::regex do_block_act_regex(
        R"(^do(?:\s*\/)?(?:\s+action)?(?:\s+([A-Za-z_][A-Za-z0-9_]*))?\s*\{([\s\S]*)\})", std::regex::optimize);
    if (std::regex_search(stmt, match, do_block_act_regex)) {
        std::string explicit_name = match[1].matched ? sanitize_identifier(match[1].str()) : "";
        std::string default_name = !state_stack.empty() ? (state_stack.back() + "_do") : "do_activity";
        std::vector<ActionSignature> do_sigs;
        parse_lifecycle_action_block(match[2].str(), explicit_name, default_name, do_sigs);
        std::string act_name =
            !do_sigs.empty() ? do_sigs.front().name : (!explicit_name.empty() ? explicit_name : default_name);
        model.add_action(act_name);
        if (!state_stack.empty()) {
            auto* st = model.find_state_mut(state_stack.back());
            if (st != nullptr) {
                st->do_activity = act_name;
                if (!do_sigs.empty() &&
                    (!do_sigs.front().assignments.empty() || !do_sigs.front().instructions.empty())) {
                    st->do_activity_action = std::move(do_sigs.front());
                }
            }
        }
        return true;
    }

    static const std::regex entry_act_regex(
        R"(^entry\s*(?:/\s*|(?:action|do)\s*(?::\s*)?)?(?!point\b)([A-Za-z_][A-Za-z0-9_]*)(?:\s*\(\s*\))?)",
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

    static const std::regex exit_act_regex(
        R"(^exit\s*(?:/\s*|(?:action|do)\s*(?::\s*)?)?(?!point\b)([A-Za-z_][A-Za-z0-9_]*)(?:\s*\(\s*\))?)",
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

    // 8. State Do Activity: do action [:] <Activity>; or do <Activity>;
    static const std::regex do_act_regex(
        R"(^do\s*(?:/\s*|action\s*(?::\s*)?)?([A-Za-z_][A-Za-z0-9_]*)(?:\s*\(\s*\))?$)", std::regex::optimize);
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

    // 8d. State Send Action: [do] send [new] <Signal>[(<Payload>)] via <Port>;
    static const std::regex send_act_regex(
        R"(^(?:do\s+)?send\s+(?:new\s+)?([A-Za-z_][A-Za-z0-9_]*)(?:\s*\(([^)]*)\))?\s+via\s+([A-Za-z_][A-Za-z0-9_]*))",
        std::regex::optimize);
    if (std::regex_search(stmt, match, send_act_regex)) {
        const std::string sig_name = sanitize_identifier(match[1].str());
        const std::string args_str = match[2].matched ? match[2].str() : "";
        const std::string port_name = sanitize_identifier(match[3].str());

        if (model.find_port(port_name) == nullptr) {
            PortDefinition p(port_name, DataType{PrimitiveTypeKind::UInt32}, PortDirection::Out);
            model.ports.push_back(std::move(p));
        }
        if (model.find_signal(sig_name) == nullptr) {
            model.signals.emplace_back(sig_name);
        }

        std::string act_name = "send_" + sig_name + "_via_" + port_name;
        ActionSignature sig_action(act_name,
                                   port_name + ".send(" + sig_name + (args_str.empty() ? "" : (", " + args_str)) + ")");
        SignalEmitOp emit_op;
        emit_op.signal_name = sig_name;
        emit_op.target_port = port_name;
        if (!args_str.empty())
            emit_op.arguments.push_back(args_str);
        sig_action.instructions.emplace_back(std::move(emit_op));

        model.add_action(act_name);
        if (!state_stack.empty()) {
            if (auto* st = model.find_state_mut(state_stack.back())) {
                st->entry_actions.push_back(std::move(sig_action));
            }
        }
        return true;
    }

    // 8c. State Stay Duration / Invariant: stay duration <= 500[ms]; or [time] invariant stay <= 500ms;
    static const std::regex invariant_regex(R"(^(?:stay(?:\s+duration)?\s*<=?|(?:time\s+)?invariant)\s*(.+)$)",
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

    // 9. Requirement Satisfaction: `[assert] satisfy [requirement [:]] <ReqId>;`
    static const std::regex satisfy_regex(R"(^(?:assert\s+)?satisfy\s+(?:requirement\s*(?::\s*)?)?([A-Za-z0-9_\-]+))",
                                          std::regex::optimize);
    if (std::regex_search(stmt, match, satisfy_regex)) {
        const std::string req_id = match[1].str();
        if (!state_stack.empty()) {
            auto* st = model.find_state_mut(state_stack.back());
            if (st != nullptr) {
                st->traceability_reqs.push_back(req_id);
            }
        } else {
            if (std::find(model.satisfies_reqs.begin(), model.satisfies_reqs.end(), req_id) ==
                model.satisfies_reqs.end()) {
                model.satisfies_reqs.push_back(req_id);
            }
        }
        return true;
    }

    // 9a-1. Native OMG SysML v2.0 MetadataUsage for Deferred Events:
    //       `@Deferred { events = ("EvA", "EvB"); }` or `@Deferred { events = "EvA"; }`
    static const std::regex meta_defer_regex(
        R"(^(?:@|metadata\s+)(?:[A-Za-z_][A-Za-z0-9_]*\s*:\s*)?(?:[A-Za-z_][A-Za-z0-9_]*::)?Deferred\s*\{([\s\S]*)\}\s*;?$)",
        std::regex::optimize);
    if (std::regex_match(stmt, match, meta_defer_regex)) {
        if (!state_stack.empty()) {
            if (auto* st = model.find_state_mut(state_stack.back())) {
                const std::string body = match[1].str();
                static const std::regex quoted_ev_re(R"delim("([^"]+)")delim", std::regex::optimize);
                bool found_quoted = false;
                for (std::sregex_iterator it(body.begin(), body.end(), quoted_ev_re), end; it != end; ++it) {
                    std::string ev = sanitize_identifier((*it)[1].str());
                    if (!ev.empty()) {
                        found_quoted = true;
                        if (std::find(st->deferred_events.begin(), st->deferred_events.end(), ev) ==
                            st->deferred_events.end()) {
                            st->deferred_events.push_back(ev);
                        }
                        model.add_event(ev);
                    }
                }
                if (!found_quoted) {
                    auto eq_pos = body.find('=');
                    if (eq_pos != std::string::npos) {
                        std::string rhs = body.substr(eq_pos + 1);
                        for (char& ch : rhs) {
                            if (ch == '(' || ch == ')' || ch == ';') {
                                ch = ' ';
                            }
                        }
                        std::istringstream iss(rhs);
                        std::string tok;
                        while (std::getline(iss, tok, ',')) {
                            std::string ev = sanitize_identifier(trim(tok));
                            if (!ev.empty()) {
                                if (std::find(st->deferred_events.begin(), st->deferred_events.end(), ev) ==
                                    st->deferred_events.end()) {
                                    st->deferred_events.push_back(ev);
                                }
                                model.add_event(ev);
                            }
                        }
                    }
                }
            }
        }
        return true;
    }

    // 9a-2. Native OMG SysML v2.0 MetadataUsage for Formal Temporal Properties:
    //       `@<PropName> : TemporalProperty [about <Target>] { kind = "Safety"; formula = "G (...)"; ... }`
    static const std::regex meta_prop_regex(
        R"(^(?:@|metadata\s+)(?:([A-Za-z_][A-Za-z0-9_]*)\s*:\s*)?(?:[A-Za-z_][A-Za-z0-9_]*::)?TemporalProperty(?:\s+about\s+[A-Za-z0-9_:,\s]+)?\s*\{([\s\S]*)\}\s*;?$)",
        std::regex::optimize);
    if (std::regex_match(stmt, match, meta_prop_regex)) {
        std::string prop_name = match[1].matched ? sanitize_identifier(match[1].str()) : "";
        const std::string body = match[2].str();
        auto extract_meta_attr = [&](const std::string& attr_name) -> std::string {
            std::regex attr_re(R"((?:^|[;\s])(?:ref\s+)?(?::>>\s*|redefines\s+)?)" + attr_name +
                               R"delim(\s*:?=\s*"((?:\\.|[^"\\])*)")delim");
            std::smatch am;
            if (!std::regex_search(body, am, attr_re)) {
                return "";
            }
            std::string raw = am[1].str();
            std::string unescaped;
            unescaped.reserve(raw.size());
            for (std::size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] == '\\' && i + 1 < raw.size()) {
                    ++i;
                }
                unescaped.push_back(raw[i]);
            }
            return unescaped;
        };
        if (prop_name.empty()) {
            prop_name = sanitize_identifier(extract_meta_attr("name"));
        }
        const std::string kind_str = extract_meta_attr("kind");
        const std::string formula_str = extract_meta_attr("formula");
        const std::string req_str = extract_meta_attr("req");
        std::string desc_str = extract_meta_attr("description");
        if (desc_str.empty()) {
            desc_str = extract_meta_attr("desc");
        }
        if (!prop_name.empty() && !formula_str.empty()) {
            FormalProperty prop;
            prop.name = prop_name;
            prop.kind = kind_str.empty() ? PropertyKind::Safety : property_kind_from_string(kind_str);
            prop.raw_formula = formula_str;
            prop.traceability_req = req_str;
            prop.description = desc_str;
            prop.ast = directive::LtlPropertyParser::parse(prop.raw_formula);
            prop.id = compute_deterministic_id(prop.name + ":" + prop.raw_formula);
            model.add_property(std::move(prop));
        }
        return true;
    }

    // 9a-3. Ignore any other generic SysML v2 `@Metadata` annotations so they do not misparse as transitions
    if (stmt.rfind("@", 0) == 0 || stmt.rfind("metadata ", 0) == 0) {
        return true;
    }

    // 9b-0. Native OMG SysML v2.0 Time::DurationOf state residence constraint:
    //       `assert constraint [<Name>] { DurationOf(<State>) <= 100 [ms] }`
    static const std::regex duration_of_constraint_regex(
        R"(^assert\s+constraint\s*(?:[A-Za-z_][A-Za-z0-9_]*\s*)?\{\s*(?:Time::)?DurationOf\s*\(\s*[A-Za-z_][A-Za-z0-9_]*\s*\)\s*(<=|<)\s*(\d+)\s*(?:\[\s*(?:SI::|ISQ::)?([A-Za-z]+)\s*\]|([A-Za-z]+))?\s*;?\s*\}\s*;?$)",
        std::regex::optimize);
    if (current_item_def.empty() && !state_stack.empty() &&
        std::regex_match(stmt, match, duration_of_constraint_regex)) {
        const std::string op_str = match[1].str();
        const std::string dur_str = match[2].str();
        std::string unit_str = match[3].matched ? match[3].str() : (match[4].matched ? match[4].str() : "ms");
        if (unit_str == "sec" || unit_str == "seconds") {
            unit_str = "s";
        } else if (unit_str == "msec" || unit_str == "milliseconds") {
            unit_str = "ms";
        } else if (unit_str == "usec" || unit_str == "microseconds") {
            unit_str = "us";
        } else if (unit_str == "nsec" || unit_str == "nanoseconds") {
            unit_str = "ns";
        }
        if (auto* st = model.find_state_mut(state_stack.back())) {
            if (op_str == "<=") {
                st->time_invariant = StateTimeInvariant(dur_str + "[" + unit_str + "]");
            } else {
                st->time_invariant = StateTimeInvariant("stay_duration < " + dur_str + "[" + unit_str + "]");
            }
        }
        return true;
    }

    // 9b. Native OMG SysML v2.0 AssertConstraintUsage inside a state or state def:
    //     `assert [not] constraint [<Name>] { <BooleanExpr> }`
    static const std::regex state_constraint_regex(
        R"(^assert\s+(not\s+)?constraint\s*(?:([A-Za-z_][A-Za-z0-9_]*)\s*)?\{([\s\S]+)\}\s*;?$)", std::regex::optimize);
    if (current_item_def.empty() && std::regex_match(stmt, match, state_constraint_regex)) {
        const bool is_negated = match[1].matched;
        std::string prop_name = match[2].matched ? sanitize_identifier(match[2].str()) : "";
        std::string expr = trim(match[3].str());
        while (!expr.empty() && expr.back() == ';') {
            expr = trim(expr.substr(0, expr.size() - 1));
        }
        if (!expr.empty()) {
            if (is_negated) {
                expr = "not (" + expr + ")";
            }
            if (prop_name.empty()) {
                prop_name = (!state_stack.empty() ? (state_stack.back() + "_Constraint_") : "Constraint_") +
                            std::to_string(model.properties.size() + 1);
            }
            std::string formula = !state_stack.empty() ? ("G (not (" + state_stack.back() + " and not (" + expr + ")))")
                                                       : ("G (" + expr + ")");
            FormalProperty prop;
            prop.name = prop_name;
            prop.kind = PropertyKind::Safety;
            prop.raw_formula = formula;
            prop.ast = directive::LtlPropertyParser::parse(prop.raw_formula);
            prop.id = compute_deterministic_id(prop.name + ":" + prop.raw_formula);
            model.add_property(std::move(prop));
        }
        return true;
    }

    // 10. Legacy Temporal Logic Specifications: assert property <Name> : <Formula>
    static const std::regex assert_prop_regex(R"(^(?:assert\s+)?property\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(.+)$)",
                                              std::regex::optimize);
    if (std::regex_search(stmt, match, assert_prop_regex)) {
        FormalProperty prop;
        prop.name = sanitize_identifier(match[1].str());
        prop.raw_formula = trim(match[2].str());
        for (const auto& existing : model.properties) {
            if (existing.name == prop.name) {
                return true;
            }
        }
        prop.ast = directive::LtlPropertyParser::parse(prop.raw_formula);
        prop.id = compute_deterministic_id(prop.name + ":" + prop.raw_formula);
        model.add_property(std::move(prop));
        return true;
    }

    // 11. Transitions: [local] transition [<Name>] [first <Src>] [accept <Evt>] [if <Guard>] [do <Act>] [then <Dst>]
    const bool starts_with_transition = (stmt.rfind("transition", 0) == 0 || stmt.rfind("local transition", 0) == 0);
    if (starts_with_transition || stmt.find("accept") != std::string::npos || stmt.find("then") != std::string::npos ||
        stmt.find("first") != std::string::npos || stmt.find("after") != std::string::npos ||
        stmt.find("fork") != std::string::npos || stmt.find("join") != std::string::npos) {
        if (starts_with_transition && stmt.find("->") != std::string::npos && stmt.find("then") == std::string::npos &&
            stmt.find(" to ") == std::string::npos) {
            error_message =
                "Line " + std::to_string(line_number) +
                ": Invalid SysML v2 transition syntax ('->' is not supported; use 'transition [first <Src>] "
                "[accept <Evt>] then <Dst>')";
            return false;
        }
        static const std::regex digit_endpoint_regex(R"(\b(?:first|from|then|to)\s+([0-9][A-Za-z0-9_]*))",
                                                     std::regex::optimize);
        std::smatch digit_match;
        if (std::regex_search(stmt, digit_match, digit_endpoint_regex)) {
            error_message = "Line " + std::to_string(line_number) + ": Invalid SysML v2 state identifier '" +
                            digit_match[1].str() + "': identifiers cannot start with a digit";
            return false;
        }
        if (!parse_transition_statement(stmt, model, state_stack, &symbol_resolver)) {
            if (!error_message.empty() && line_number > 0 && error_message.find("line ") == std::string::npos &&
                error_message.find("Line ") == std::string::npos) {
                error_message = "Line " + std::to_string(line_number) + ": " + error_message;
            }
            return false;
        }
        return true;
    }

    return true;
}

/**
 * @brief Parses SysML v2 transition statements into TransitionEdge objects in FsmIr.
 *
 * Supports comprehensive SysML v2 state transition syntax variations:
 * - Explicit transition naming: `transition <Name>`
 * - Priority specifications: `priority = <N>`
 * - Source states: `first <Source>` or `from <Source>` (or inferred from state_stack)
 * - Event triggers: `accept <Event>` or `when <Event>`
 * - Timed triggers: `after <Duration>` (with SI units: ms, s, min, h) or `at (<TimeExpr>)`
 * - Guard conditions: `if <GuardExpr>` (composite boolean expressions or custom guards)
 * - Action effects: `do { <Assignments> }`, `do send <Signal> via <Port>`, or `do <Action>`
 * - Target states: `then <Target>` or `to <Target>`
 * - Fork / Join composite transitions: `fork (A, B, C)` or `join (A, B) then S`
 * - History pseudostates: `then State[H]` (shallow) or `then State[H*]` (deep)
 * - Internal transitions: transitions without target or with identical source and target
 *
 * @param stmt Textual transition statement.
 * @param model Output intermediate representation receiving the transition.
 * @param state_stack Current enclosing hierarchical state stack.
 * @return True if parsing succeeded.
 */

}  // namespace fsm::frontend::formal

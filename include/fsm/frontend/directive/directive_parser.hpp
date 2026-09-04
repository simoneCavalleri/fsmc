#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "fsm/frontend/directive/ltl_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::codegen {

class DirectiveParser {
  public:
    // Strips whitespace and extracts directive key/args
    static bool is_directive(std::string_view line) {
        std::string trimmed = trim(line);
        return trimmed.find("@fsm:") != std::string::npos;
    }

    static std::string extract_directive_body(std::string_view line) {
        std::string trimmed = trim(line);
        auto pos = trimmed.find("@fsm:");
        if (pos == std::string::npos)
            return "";
        return trim(trimmed.substr(pos + 5));
    }

    // Parses @fsm:state [name="<name>"] [history=shallow|deep] [satisfies=["REQ-1", "REQ-2"]]
    // [do_activity="async_worker"]
    static bool parse_state_directive(std::string_view body, StateNode& state) {
        std::string str(body);
        if (str.rfind("state", 0) == 0) {
            str = trim(str.substr(5));
        }
        // history=shallow / deep
        auto h_pos = str.find("history=");
        if (h_pos != std::string::npos) {
            auto val = extract_quoted_or_word(str, h_pos + 8);
            if (val == "deep") {
                state.kind = StateKind::DeepHistory;
            } else if (val == "shallow") {
                state.kind = StateKind::ShallowHistory;
            }
        }
        // kind=entryPoint / exitPoint / choice / junction
        auto k_pos = str.find("kind=");
        if (k_pos != std::string::npos) {
            auto val = extract_quoted_or_word(str, k_pos + 5);
            state.kind = state_kind_from_string(val);
        }
        // invariant="..." or time_invariant="..."
        auto inv_pos = str.find("invariant=");
        if (inv_pos == std::string::npos) {
            inv_pos = str.find("time_invariant=");
            if (inv_pos != std::string::npos) {
                inv_pos += 15;
            }
        } else {
            inv_pos += 10;
        }
        if (inv_pos != std::string::npos) {
            state.time_invariant = extract_quoted_or_word(str, inv_pos);
        }
        // do_activity="..."
        auto act_pos = str.find("do_activity=");
        if (act_pos != std::string::npos) {
            state.do_activity = extract_quoted_or_word(str, act_pos + 12);
        }
        // satisfies=["REQ-1", "REQ-2"]
        auto req_pos = str.find("satisfies=");
        if (req_pos != std::string::npos) {
            auto list = extract_array_items(str, req_pos + 10);
            for (auto& item : list) {
                if (std::find(state.traceability_reqs.begin(), state.traceability_reqs.end(), item) ==
                    state.traceability_reqs.end()) {
                    state.traceability_reqs.push_back(std::move(item));
                }
            }
        }
        return true;
    }

    static bool parse_state_directive(std::string_view body, FsmIr& model,
                                      const std::vector<std::string>& parent_stack) {
        std::string str(body);
        if (str.rfind("state", 0) == 0) {
            str = trim(str.substr(5));
        }
        std::string st_name;
        auto n_pos = str.find("name=");
        if (n_pos != std::string::npos) {
            st_name = extract_quoted_or_word(str, n_pos + 5);
        }
        StateNode* st = nullptr;
        if (!st_name.empty()) {
            st = model.find_state_mut(st_name);
            if (st == nullptr) {
                st = &model.add_state(st_name, parent_stack.empty() ? "" : parent_stack.back());
            }
        } else if (!parent_stack.empty()) {
            st = model.find_state_mut(parent_stack.back());
        }
        if (st != nullptr) {
            return parse_state_directive(body, *st);
        }
        return false;
    }

    // Parses @fsm:defer [Ev1, Ev2]
    static bool parse_defer_directive(std::string_view body, StateNode& state) {
        std::string str(body);
        if (str.rfind("defer", 0) == 0) {
            str = trim(str.substr(5));
        }
        auto list = extract_array_items(str, 0);
        for (auto& ev : list) {
            if (!ev.empty() && std::find(state.deferred_events.begin(), state.deferred_events.end(), ev) ==
                                   state.deferred_events.end()) {
                state.deferred_events.push_back(std::move(ev));
            }
        }
        return true;
    }

    // Parses @fsm:signal EvPacketRecv{uint32_t len, const uint8_t* ptr} validator="len > 0 && ptr != nullptr"
    static std::optional<SignalDefinition> parse_signal_directive(std::string_view body) {
        std::string str = trim(body);
        if (str.empty())
            return std::nullopt;

        if (str.rfind("signal", 0) == 0) {
            str = trim(str.substr(6));
        }

        SignalDefinition sig;
        auto brace_open = str.find('{');
        auto brace_close = str.find('}');

        if (brace_open != std::string::npos && brace_close != std::string::npos && brace_close > brace_open) {
            sig.name = trim(str.substr(0, brace_open));
            std::string attr_list = str.substr(brace_open + 1, brace_close - brace_open - 1);
            std::istringstream iss(attr_list);
            std::string token;
            while (std::getline(iss, token, ',')) {
                token = trim(token);
                if (token.empty())
                    continue;
                auto last_space = token.find_last_of(" \t*&");
                if (last_space != std::string::npos) {
                    std::string type = trim(token.substr(0, last_space + 1));
                    std::string name = trim(token.substr(last_space + 1));
                    sig.attributes.emplace_back(name, type);
                } else {
                    sig.attributes.emplace_back(token, "int");
                }
            }
        } else {
            // Signal without attributes, e.g. @fsm:signal EvSensorReady
            auto first_space = str.find_first_of(" \t");
            if (first_space != std::string::npos) {
                sig.name = trim(str.substr(0, first_space));
            } else {
                sig.name = str;
            }
        }

        // Parse validator="..."
        auto v_pos = str.find("validator=");
        if (v_pos != std::string::npos) {
            std::string val = extract_quoted_or_word(str, v_pos + 10);
            if (!val.empty()) {
                sig.validators.push_back(std::move(val));
            }
        }

        return sig;
    }

    // Parses @fsm:property name=SafeLand kind=Safety formula="G (LowBattery -> F SafeLand)" [req="REQ-01"] [desc="..."]
    static std::optional<FormalProperty> parse_property_directive(std::string_view body) {
        std::string str = trim(body);
        if (str.empty())
            return std::nullopt;

        if (str.rfind("property", 0) == 0) {
            str = trim(str.substr(8));
        }

        FormalProperty prop;
        // name="..."
        auto n_pos = str.find("name=");
        if (n_pos != std::string::npos) {
            prop.name = extract_quoted_or_word(str, n_pos + 5);
        } else {
            auto first_space = str.find_first_of(" \t=");
            if (first_space != std::string::npos) {
                prop.name = trim(str.substr(0, first_space));
            } else {
                prop.name = str;
            }
        }

        // kind=Safety / Liveness / Invariant / Reachability / DeadlockFreedom
        auto k_pos = str.find("kind=");
        if (k_pos != std::string::npos) {
            std::string k_str = extract_quoted_or_word(str, k_pos + 5);
            prop.kind = property_kind_from_string(k_str);
        }

        // formula="..." or ltl="..."
        auto f_pos = str.find("formula=");
        if (f_pos == std::string::npos) {
            auto l_pos = str.find("ltl=");
            if (l_pos != std::string::npos) {
                f_pos = l_pos + 4;
            }
        } else {
            f_pos += 8;
        }

        if (f_pos != std::string::npos) {
            prop.raw_formula = extract_quoted_or_word(str, f_pos);
            prop.ast = LtlPropertyParser::parse(prop.raw_formula);
        } else {
            // Check for direct assignment: PropName = "..."
            auto eq_pos = str.find('=');
            if (eq_pos != std::string::npos) {
                std::string after_eq = trim(str.substr(eq_pos + 1));
                if (after_eq.rfind("formula=", 0) == std::string::npos &&
                    after_eq.rfind("ltl=", 0) == std::string::npos && after_eq.rfind("kind=", 0) == std::string::npos) {
                    prop.raw_formula = extract_quoted_or_word(str, eq_pos + 1);
                    if (!prop.raw_formula.empty()) {
                        if (prop.raw_formula.back() == ';') {
                            prop.raw_formula.pop_back();
                            prop.raw_formula = trim(prop.raw_formula);
                        }
                        prop.ast = LtlPropertyParser::parse(prop.raw_formula);
                    }
                }
            }
        }

        // req="..."
        auto req_pos = str.find("req=");
        if (req_pos != std::string::npos) {
            prop.traceability_req = extract_quoted_or_word(str, req_pos + 4);
        }

        // desc="..."
        auto d_pos = str.find("desc=");
        if (d_pos != std::string::npos) {
            prop.description = extract_quoted_or_word(str, d_pos + 5);
        }

        if (prop.id.empty()) {
            prop.id = compute_deterministic_id(prop.name + ":" + prop.raw_formula);
        }

        return prop;
    }

    // Parses @fsm:port name=sensor_val type=float dir=in [min=0] [max=100] [constraint="..."] [unit="..."] [desc="..."]
    static std::optional<PortDefinition> parse_port_directive(std::string_view body) {
        std::string str = trim(body);
        if (str.empty())
            return std::nullopt;

        if (str.rfind("port", 0) == 0) {
            str = trim(str.substr(4));
        }

        PortDefinition port;
        auto n_pos = str.find("name=");
        if (n_pos != std::string::npos) {
            port.name = extract_quoted_or_word(str, n_pos + 5);
        } else {
            auto first_space = str.find_first_of(" \t");
            if (first_space != std::string::npos) {
                port.name = trim(str.substr(0, first_space));
            } else {
                port.name = str;
            }
        }

        auto t_pos = str.find("type=");
        if (t_pos != std::string::npos) {
            port.type = extract_quoted_or_word(str, t_pos + 5);
            port.type_kind = infer_type_kind(port.type);
        }

        auto d_pos = str.find("dir=");
        if (d_pos == std::string::npos) {
            d_pos = str.find("direction=");
            if (d_pos != std::string::npos) {
                d_pos += 10;
            }
        } else {
            d_pos += 4;
        }
        if (d_pos != std::string::npos) {
            std::string dir_str = extract_quoted_or_word(str, d_pos);
            port.direction = string_to_port_direction(dir_str);
        }

        auto u_pos = str.find("unit=");
        if (u_pos != std::string::npos) {
            port.physical_unit = extract_quoted_or_word(str, u_pos + 5);
        }

        auto c_pos = str.find("constraint=");
        if (c_pos != std::string::npos) {
            port.constraint = extract_quoted_or_word(str, c_pos + 11);
        }

        auto min_pos = str.find("min=");
        if (min_pos != std::string::npos) {
            std::string m_str = extract_quoted_or_word(str, min_pos + 4);
            if (!m_str.empty()) {
                try {
                    port.min_value = std::stod(m_str);
                } catch (...) {
                }
            }
        }

        auto max_pos = str.find("max=");
        if (max_pos != std::string::npos) {
            std::string m_str = extract_quoted_or_word(str, max_pos + 4);
            if (!m_str.empty()) {
                try {
                    port.max_value = std::stod(m_str);
                } catch (...) {
                }
            }
        }

        auto desc_pos = str.find("desc=");
        if (desc_pos != std::string::npos) {
            port.description = extract_quoted_or_word(str, desc_pos + 5);
        }

        return port;
    }

    // Parses @fsm:var name=retry_count type=uint32_t init=0 [unit="[mm/s]"] [min=0] [max=5] [desc="..."]
    static std::optional<VariableDefinition> parse_variable_directive(std::string_view body) {
        std::string str = trim(body);
        if (str.empty())
            return std::nullopt;

        if (str.rfind("variable", 0) == 0) {
            str = trim(str.substr(8));
        } else if (str.rfind("var", 0) == 0) {
            str = trim(str.substr(3));
        }

        VariableDefinition var;
        auto n_pos = str.find("name=");
        if (n_pos != std::string::npos) {
            var.name = extract_quoted_or_word(str, n_pos + 5);
        } else {
            auto first_space = str.find_first_of(" \t");
            if (first_space != std::string::npos) {
                var.name = trim(str.substr(0, first_space));
            } else {
                var.name = str;
            }
        }

        auto t_pos = str.find("type=");
        if (t_pos != std::string::npos) {
            var.type = extract_quoted_or_word(str, t_pos + 5);
            var.type_kind = infer_type_kind(var.type);
        }

        auto k_pos = str.find("kind=");
        if (k_pos != std::string::npos) {
            std::string k_str = extract_quoted_or_word(str, k_pos + 5);
            if (k_str == "Boolean")
                var.type_kind = VariableTypeKind::Boolean;
            else if (k_str == "Integer")
                var.type_kind = VariableTypeKind::Integer;
            else if (k_str == "UnsignedInteger")
                var.type_kind = VariableTypeKind::UnsignedInteger;
            else if (k_str == "Float")
                var.type_kind = VariableTypeKind::Float;
            else if (k_str == "Enum")
                var.type_kind = VariableTypeKind::Enum;
            else if (k_str == "CustomStruct")
                var.type_kind = VariableTypeKind::CustomStruct;
        }

        auto u_pos = str.find("unit=");
        if (u_pos != std::string::npos) {
            var.physical_unit = extract_quoted_or_word(str, u_pos + 5);
        }

        auto i_pos = str.find("init=");
        if (i_pos != std::string::npos) {
            var.initial_value = extract_quoted_or_word(str, i_pos + 5);
        }

        auto min_pos = str.find("min=");
        if (min_pos != std::string::npos) {
            std::string m_str = extract_quoted_or_word(str, min_pos + 4);
            if (!m_str.empty()) {
                var.min_value = std::stoll(m_str);
            }
        }

        auto max_pos = str.find("max=");
        if (max_pos != std::string::npos) {
            std::string m_str = extract_quoted_or_word(str, max_pos + 4);
            if (!m_str.empty()) {
                var.max_value = std::stoll(m_str);
            }
        }

        auto d_pos = str.find("desc=");
        if (d_pos != std::string::npos) {
            var.description = extract_quoted_or_word(str, d_pos + 5);
        }

        return var;
    }

    // Parses @fsm:enum name=FlightPhase [type=uint8_t] literals=[Preflight=0, Taxi=1, Cruise=2] [desc="..."]
    static std::optional<EnumDefinition> parse_enum_directive(std::string_view body) {
        std::string str = trim(body);
        if (str.empty())
            return std::nullopt;

        if (str.rfind("enum", 0) == 0) {
            str = trim(str.substr(4));
        }

        EnumDefinition en;
        auto n_pos = str.find("name=");
        if (n_pos != std::string::npos) {
            en.name = extract_quoted_or_word(str, n_pos + 5);
        } else {
            auto first_space = str.find_first_of(" \t");
            if (first_space != std::string::npos) {
                en.name = trim(str.substr(0, first_space));
            } else {
                en.name = str;
            }
        }

        if (en.name.empty()) {
            return std::nullopt;
        }

        auto t_pos = str.find("type=");
        if (t_pos != std::string::npos) {
            en.underlying_type = extract_quoted_or_word(str, t_pos + 5);
        }
        if (en.underlying_type.empty()) {
            en.underlying_type = "uint8_t";
        }

        auto d_pos = str.find("desc=");
        if (d_pos != std::string::npos) {
            en.description = extract_quoted_or_word(str, d_pos + 5);
        }

        auto lit_pos = str.find("literals=");
        if (lit_pos != std::string::npos) {
            auto items = extract_array_items(str, lit_pos + 9);
            for (const auto& item : items) {
                auto eq_pos = item.find('=');
                if (eq_pos != std::string::npos) {
                    std::string lname = trim_ws(item.substr(0, eq_pos));
                    std::string lval = trim_ws(item.substr(eq_pos + 1));
                    if (!lname.empty()) {
                        try {
                            en.add_literal(lname, std::stoll(lval));
                        } catch (...) {
                            en.add_literal(lname);
                        }
                    }
                } else {
                    std::string lname = trim_ws(item);
                    if (!lname.empty()) {
                        en.add_literal(lname);
                    }
                }
            }
        }

        return en;
    }

    // Parses @fsm:struct name=Waypoint [is_datatype=true] fields=[latitude_deg:Real=0.0, longitude_deg:Real=0.0] [desc="..."]
    static std::optional<StructDefinition> parse_struct_directive(std::string_view body) {
        std::string str = trim(body);
        if (str.empty())
            return std::nullopt;

        if (str.rfind("struct", 0) == 0) {
            str = trim(str.substr(6));
        }

        StructDefinition st;
        auto n_pos = str.find("name=");
        if (n_pos != std::string::npos) {
            st.name = extract_quoted_or_word(str, n_pos + 5);
        } else {
            auto first_space = str.find_first_of(" \t");
            if (first_space != std::string::npos) {
                st.name = trim(str.substr(0, first_space));
            } else {
                st.name = str;
            }
        }

        if (st.name.empty()) {
            return std::nullopt;
        }

        auto dt_pos = str.find("is_datatype=");
        if (dt_pos != std::string::npos) {
            std::string val = extract_quoted_or_word(str, dt_pos + 12);
            st.is_datatype = (val == "true" || val == "1");
        }

        auto d_pos = str.find("desc=");
        if (d_pos != std::string::npos) {
            st.description = extract_quoted_or_word(str, d_pos + 5);
        }

        auto f_pos = str.find("fields=");
        if (f_pos != std::string::npos) {
            auto items = extract_array_items(str, f_pos + 7);
            for (const auto& item : items) {
                std::string fname;
                std::string ftype = "string";
                std::string fdefault;

                auto colon_pos = item.find(':');
                if (colon_pos != std::string::npos) {
                    fname = trim_ws(item.substr(0, colon_pos));
                    std::string rest = item.substr(colon_pos + 1);
                    auto eq_pos = rest.find('=');
                    if (eq_pos != std::string::npos) {
                        ftype = trim_ws(rest.substr(0, eq_pos));
                        fdefault = trim_ws(rest.substr(eq_pos + 1));
                    } else {
                        ftype = trim_ws(rest);
                    }
                } else {
                    auto eq_pos = item.find('=');
                    if (eq_pos != std::string::npos) {
                        fname = trim_ws(item.substr(0, eq_pos));
                        fdefault = trim_ws(item.substr(eq_pos + 1));
                    } else {
                        fname = trim_ws(item);
                    }
                }

                if (!fname.empty()) {
                    StructField field(fname, ftype, fdefault);
                    st.add_field(std::move(field));
                }
            }
        }

        return st;
    }

    // Formats an EnumDefinition into @fsm:enum directive argument string
    static std::string format_enum_directive(const EnumDefinition& en) {
        std::string out = "name=" + en.name + " type=" + (en.underlying_type.empty() ? "uint8_t" : en.underlying_type) + " literals=[";
        for (size_t i = 0; i < en.literals.size(); ++i) {
            if (i > 0) out += ", ";
            out += en.literals[i].name;
            if (en.literals[i].value.has_value()) {
                out += "=" + std::to_string(*en.literals[i].value);
            }
        }
        out += "]";
        if (!en.description.empty()) {
            out += " desc=\"" + en.description + "\"";
        }
        return out;
    }

    // Formats a StructDefinition into @fsm:struct directive argument string
    static std::string format_struct_directive(const StructDefinition& st) {
        std::string out = "name=" + st.name;
        if (st.is_datatype) {
            out += " is_datatype=true";
        }
        out += " fields=[";
        for (size_t i = 0; i < st.fields.size(); ++i) {
            if (i > 0) out += ", ";
            out += st.fields[i].name + ":" + st.fields[i].type;
            if (!st.fields[i].default_value.empty()) {
                out += "=" + st.fields[i].default_value;
            }
        }
        out += "]";
        if (!st.description.empty()) {
            out += " desc=\"" + st.description + "\"";
        }
        return out;
    }

    // Parses any model-level directive (var, port, property, signal, enum, struct)
    static bool parse_model_directive(std::string_view body, FsmIr& model) {
        if (body.rfind("var", 0) == 0 || body.rfind("variable", 0) == 0) {
            if (auto var = parse_variable_directive(body)) {
                model.add_variable(std::move(*var));
                return true;
            }
        } else if (body.rfind("port", 0) == 0) {
            if (auto port = parse_port_directive(body)) {
                model.ports.push_back(std::move(*port));
                return true;
            }
        } else if (body.rfind("property", 0) == 0) {
            if (auto prop = parse_property_directive(body)) {
                model.add_property(std::move(*prop));
                return true;
            }
        } else if (body.rfind("signal", 0) == 0) {
            if (auto sig = parse_signal_directive(body)) {
                model.add_signal(std::move(*sig));
                return true;
            }
        } else if (body.rfind("enum", 0) == 0) {
            if (auto en = parse_enum_directive(body)) {
                model.add_enum(std::move(*en));
                return true;
            }
        } else if (body.rfind("struct", 0) == 0) {
            if (auto st = parse_struct_directive(body)) {
                model.add_struct(std::move(*st));
                return true;
            }
        }
        return false;
    }

    // Parses @fsm:trans [id="<hash>"] [guard_ast="..."] [action_sig="..."]
    static bool parse_trans_directive(std::string_view body, TransitionEdge& trans) {
        std::string str(body);
        if (str.rfind("trans", 0) == 0) {
            str = trim(str.substr(5));
        }
        auto id_pos = str.find("id=");
        if (id_pos != std::string::npos) {
            trans.id = extract_quoted_or_word(str, id_pos + 3);
        }
        auto g_pos = str.find("guard_ast=");
        if (g_pos != std::string::npos) {
            std::string guard_expr = extract_quoted_or_word(str, g_pos + 10);
            trans.guard_ast = GuardAstNode(guard_expr);
        }
        auto a_pos = str.find("action_sig=");
        if (a_pos != std::string::npos) {
            std::string act = extract_quoted_or_word(str, a_pos + 11);
            trans.action_sig = ActionSignature(act, act);
        }
        auto p_pos = str.find("priority=");
        if (p_pos == std::string::npos) {
            p_pos = str.find("prio=");
            if (p_pos != std::string::npos) {
                p_pos += 5;
            }
        } else {
            p_pos += 9;
        }
        if (p_pos != std::string::npos) {
            std::string p_str = extract_quoted_or_word(str, p_pos);
            if (!p_str.empty()) {
                trans.priority = static_cast<std::uint32_t>(std::stoul(p_str));
            }
        }
        return true;
    }

  private:
    static std::string trim(std::string_view s) {
        auto start = s.find_first_not_of(" \t\r\n'#%/");
        if (start == std::string::npos)
            return "";
        auto end = s.find_last_not_of(" \t\r\n;");
        return std::string(s.substr(start, end - start + 1));
    }

    static std::string trim_ws(std::string_view s) {
        auto start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
            return "";
        auto end = s.find_last_not_of(" \t\r\n");
        return std::string(s.substr(start, end - start + 1));
    }

    static std::string extract_quoted_or_word(const std::string& str, std::size_t start) {
        if (start >= str.size())
            return "";
        while (start < str.size() && (str[start] == ' ' || str[start] == '\t')) {
            ++start;
        }
        if (start >= str.size())
            return "";
        if (str[start] == '"') {
            auto end = str.find('"', start + 1);
            if (end != std::string::npos) {
                return str.substr(start + 1, end - start - 1);
            }
            return str.substr(start + 1);
        }
        auto end = str.find_first_of(" \t\r\n];,", start);
        if (end != std::string::npos) {
            return str.substr(start, end - start);
        }
        return str.substr(start);
    }

    static std::vector<std::string> extract_array_items(const std::string& str, std::size_t start) {
        std::vector<std::string> items;
        auto open_bracket = str.find('[', start);
        auto close_bracket = str.find(']', open_bracket);
        if (open_bracket == std::string::npos || close_bracket == std::string::npos) {
            return items;
        }
        std::string inner = str.substr(open_bracket + 1, close_bracket - open_bracket - 1);
        std::istringstream iss(inner);
        std::string token;
        while (std::getline(iss, token, ',')) {
            std::string t = trim(token);
            if (t.front() == '"' && t.back() == '"' && t.size() >= 2) {
                t = t.substr(1, t.size() - 2);
            }
            if (!t.empty()) {
                items.push_back(std::move(t));
            }
        }
        return items;
    }
};

}  // namespace fsm::codegen

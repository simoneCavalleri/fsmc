#include <algorithm>
#include <regex>
#include <sstream>

#include "directive_utils.hpp"
#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/frontend/directive/ltl_parser.hpp"
#include "fsm/ir/guard.hpp"
#include "fsm/ir/identifier_utils.hpp"

namespace fsm::frontend::directive {

using namespace fsm::ir;
using namespace detail;

std::optional<SignalDefinition> DirectiveParser::parse_signal_directive(std::string_view body) {
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
        auto first_space = str.find_first_of(" \t");
        if (first_space != std::string::npos) {
            sig.name = trim(str.substr(0, first_space));
        } else {
            sig.name = str;
        }
    }

    auto v_pos = str.find("validator=");
    if (v_pos != std::string::npos) {
        std::string val = extract_quoted_or_word(str, v_pos + 10);
        if (!val.empty()) {
            sig.validators.push_back(std::move(val));
        }
    }

    return sig;
}

std::optional<FormalProperty> DirectiveParser::parse_property_directive(std::string_view body) {
    std::string str = trim(body);
    if (str.empty())
        return std::nullopt;

    if (str.rfind("property", 0) == 0) {
        str = trim(str.substr(8));
    }

    FormalProperty prop;
    std::size_t after_name_pos = std::string::npos;
    auto n_pos = str.find("name=");
    if (n_pos != std::string::npos) {
        prop.name = extract_quoted_or_word(str, n_pos + 5);
    } else {
        auto first_delim = str.find_first_of(" \t=:");
        if (first_delim != std::string::npos) {
            prop.name = trim(str.substr(0, first_delim));
            after_name_pos = first_delim;
        } else {
            prop.name = str;
        }
    }

    auto k_pos = str.find("kind=");
    if (k_pos != std::string::npos) {
        std::string k_str = extract_quoted_or_word(str, k_pos + 5);
        prop.kind = property_kind_from_string(k_str);
    }

    auto f_pos = str.find("formula=");
    if (f_pos == std::string::npos) {
        auto l_pos = str.find("ltl=");
        if (l_pos != std::string::npos) {
            f_pos = l_pos + 4;
        }
    } else {
        f_pos += 8;
    }

    auto extract_inline_formula = [](std::string_view raw) -> std::string {
        std::string tail = trim(raw);
        if (tail.empty()) {
            return {};
        }
        if (tail.front() == '"' || tail.front() == '\'') {
            return extract_quoted_or_word(tail, 0);
        }
        // Strip trailing key=value metadata clauses (req=, desc=, kind=) if present
        std::size_t cut = tail.size();
        for (const char* kw : {" req=", " desc=", " kind="}) {
            if (auto p = tail.find(kw); p != std::string::npos && p < cut) {
                cut = p;
            }
        }
        std::string formula = trim(tail.substr(0, cut));
        if (!formula.empty() && formula.back() == ';') {
            formula.pop_back();
            formula = trim(formula);
        }
        return formula;
    };

    if (f_pos != std::string::npos) {
        prop.raw_formula = extract_quoted_or_word(str, f_pos);
        prop.ast = LtlPropertyParser::parse(prop.raw_formula);
    } else if (after_name_pos != std::string::npos) {
        std::size_t sep = after_name_pos;
        while (sep < str.size() && (str[sep] == ' ' || str[sep] == '\t')) {
            ++sep;
        }
        if (sep < str.size() && (str[sep] == ':' || str[sep] == '=')) {
            std::string after_sep = trim(str.substr(sep + 1));
            if (after_sep.rfind("formula=", 0) == std::string::npos &&
                after_sep.rfind("ltl=", 0) == std::string::npos && after_sep.rfind("kind=", 0) == std::string::npos) {
                prop.raw_formula = extract_inline_formula(after_sep);
                if (!prop.raw_formula.empty()) {
                    prop.ast = LtlPropertyParser::parse(prop.raw_formula);
                }
            }
        }
    }

    auto req_pos = str.find("req=");
    if (req_pos != std::string::npos) {
        prop.traceability_req = extract_quoted_or_word(str, req_pos + 4);
    }

    auto d_pos = str.find("desc=");
    if (d_pos != std::string::npos) {
        prop.description = extract_quoted_or_word(str, d_pos + 5);
    }

    if (prop.id.empty()) {
        prop.id = compute_deterministic_id(prop.name + ":" + prop.raw_formula);
    }

    return prop;
}

std::optional<PortDefinition> DirectiveParser::parse_port_directive(std::string_view body) {
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

std::optional<VariableDefinition> DirectiveParser::parse_variable_directive(std::string_view body) {
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
    }

    auto k_pos = str.find("kind=");
    if (k_pos != std::string::npos) {
        std::string k_str = extract_quoted_or_word(str, k_pos + 5);
        if (k_str == "Boolean")
            var.type = DataType::boolean();
        else if (k_str == "Integer")
            var.type = DataType::int32();
        else if (k_str == "UnsignedInteger")
            var.type = DataType::uint32();
        else if (k_str == "Float")
            var.type = DataType::float32();
    }

    auto u_pos = str.find("unit=");
    if (u_pos != std::string::npos) {
        var.physical_unit = extract_quoted_or_word(str, u_pos + 5);
    }

    auto i_pos = str.find("init=");
    if (i_pos != std::string::npos) {
        var.initial_value = extract_quoted_or_word(str, i_pos + 5);
        if (var.type.primitive() == ir::PrimitiveTypeKind::String) {
            bool already_quoted =
                var.initial_value.size() >= 2 && var.initial_value.front() == '"' && var.initial_value.back() == '"';
            if (!already_quoted) {
                var.initial_value = "\"" + var.initial_value + "\"";
            }
        }
    } else if (!var.type.is_primitive() || var.type.primitive() == ir::PrimitiveTypeKind::String) {
        var.initial_value.clear();
    }

    auto min_pos = str.find("min=");
    if (min_pos != std::string::npos) {
        std::string m_str = extract_quoted_or_word(str, min_pos + 4);
        if (!m_str.empty()) {
            try {
                var.min_value = std::stoll(m_str);
            } catch (...) {
            }
        }
    }

    auto max_pos = str.find("max=");
    if (max_pos != std::string::npos) {
        std::string m_str = extract_quoted_or_word(str, max_pos + 4);
        if (!m_str.empty()) {
            try {
                var.max_value = std::stoll(m_str);
            } catch (...) {
            }
        }
    }

    auto d_pos = str.find("desc=");
    if (d_pos != std::string::npos) {
        var.description = extract_quoted_or_word(str, d_pos + 5);
    }

    return var;
}

bool DirectiveParser::parse_model_directive(std::string_view body, FsmIr& model) {
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
    } else if (body.rfind("external_type", 0) == 0) {
        if (auto td = parse_external_type_directive(body)) {
            model.add_type(std::move(*td));
            return true;
        }
    } else if (body.rfind("service", 0) == 0) {
        if (auto svc = parse_service_directive(body)) {
            model.add_service(std::move(*svc));
            return true;
        }
    }
    return false;
}

}  // namespace fsm::frontend::directive

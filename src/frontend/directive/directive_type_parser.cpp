#include <algorithm>
#include <regex>
#include <sstream>

#include "directive_utils.hpp"
#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/ir/identifier_utils.hpp"

namespace fsm::frontend::directive {

using namespace fsm::ir;
using namespace detail;

std::optional<EnumDefinition> DirectiveParser::parse_enum_directive(std::string_view body) {
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

std::optional<StructDefinition> DirectiveParser::parse_struct_directive(std::string_view body) {
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

std::optional<TypeDefinition> DirectiveParser::parse_external_type_directive(std::string_view body) {
    std::string str = trim(body);
    if (str.empty()) {
        return std::nullopt;
    }
    if (str.rfind("external_type", 0) == 0) {
        str = trim(str.substr(13));
    }

    TypeDefinition td;
    td.kind = TypeKind::Struct;

    auto n_pos = str.find("name=");
    if (n_pos != std::string::npos) {
        td.name = extract_quoted_or_word(str, n_pos + 5);
    } else {
        auto first_space = str.find_first_of(" \t");
        if (first_space != std::string::npos) {
            td.name = trim(str.substr(0, first_space));
        } else {
            td.name = str;
        }
    }

    if (td.name.empty()) {
        return std::nullopt;
    }

    auto k_pos = str.find("kind=");
    if (k_pos != std::string::npos) {
        const std::string k_val = extract_quoted_or_word(str, k_pos + 5);
        if (k_val == "enum" || k_val == "Enum") {
            td.kind = TypeKind::Enum;
        }
    }

    std::string mod;
    std::string sym;
    auto m_pos = str.find("module=");
    if (m_pos != std::string::npos) {
        mod = extract_quoted_or_word(str, m_pos + 7);
    }
    auto s_pos = str.find("symbol=");
    if (s_pos != std::string::npos) {
        sym = extract_quoted_or_word(str, s_pos + 7);
    }
    if (mod.empty() || sym.empty()) {
        return std::nullopt;
    }
    td.external_ref = ExternalTypeRef{std::move(mod), std::move(sym)};

    auto d_pos = str.find("desc=");
    if (d_pos != std::string::npos) {
        td.description = extract_quoted_or_word(str, d_pos + 5);
    }

    return td;
}

std::optional<ServiceMethodSignature> DirectiveParser::parse_service_directive(std::string_view body) {
    std::string str = trim(body);
    if (str.empty()) {
        return std::nullopt;
    }
    if (str.rfind("service", 0) == 0) {
        str = trim(str.substr(7));
    }

    ServiceMethodSignature svc;
    svc.return_type = DataType::from_string("void");
    svc.is_noexcept = true;

    const auto paren_open = str.find('(');
    const auto paren_close = (paren_open != std::string::npos) ? str.find(')', paren_open) : std::string::npos;

    // Form 1: signature form: compute_pwm(err: int32_t, kp: float) -> int32_t [pure=true] [noexcept=true]
    if (paren_open != std::string::npos && paren_close != std::string::npos && paren_close > paren_open) {
        std::string head = trim_ws(str.substr(0, paren_open));
        if (head.rfind("name=", 0) == 0) {
            head = extract_quoted_or_word(head, 5);
        }
        svc.name = std::move(head);

        const std::string param_str = str.substr(paren_open + 1, paren_close - paren_open - 1);
        std::istringstream piss(param_str);
        std::string item;
        while (std::getline(piss, item, ',')) {
            item = trim_ws(item);
            if (item.empty()) {
                continue;
            }
            const auto colon = item.find(':');
            if (colon != std::string::npos) {
                std::string pname = trim_ws(item.substr(0, colon));
                std::string ptype = trim_ws(item.substr(colon + 1));
                if (!pname.empty() && !ptype.empty()) {
                    svc.parameters.emplace_back(std::move(pname), DataType::from_string(ptype));
                }
            } else {
                const auto sp = item.find_last_of(" \t");
                if (sp != std::string::npos) {
                    std::string ptype = trim_ws(item.substr(0, sp));
                    std::string pname = trim_ws(item.substr(sp + 1));
                    if (!pname.empty() && !ptype.empty()) {
                        svc.parameters.emplace_back(std::move(pname), DataType::from_string(ptype));
                    }
                }
            }
        }

        const std::string tail = str.substr(paren_close + 1);
        const auto arrow = tail.find("->");
        if (arrow != std::string::npos) {
            const std::string ret_str = extract_quoted_or_word(tail, arrow + 2);
            if (!ret_str.empty() && ret_str != "void") {
                svc.return_type = DataType::from_string(ret_str);
            }
        }
    } else {
        // Form 2: key-value form: name=compute_pwm params=[err:int32_t] return=int32_t
        const auto n_pos = str.find("name=");
        if (n_pos != std::string::npos) {
            svc.name = extract_quoted_or_word(str, n_pos + 5);
        } else {
            const auto first_space = str.find_first_of(" \t");
            svc.name = (first_space != std::string::npos) ? trim(str.substr(0, first_space)) : str;
        }
        const auto p_pos = str.find("params=");
        if (p_pos != std::string::npos) {
            const auto items = extract_array_items(str, p_pos + 7);
            for (const auto& item : items) {
                const auto colon = item.find(':');
                if (colon != std::string::npos) {
                    std::string pname = trim_ws(item.substr(0, colon));
                    std::string ptype = trim_ws(item.substr(colon + 1));
                    if (!pname.empty() && !ptype.empty()) {
                        svc.parameters.emplace_back(std::move(pname), DataType::from_string(ptype));
                    }
                }
            }
        }
    }

    if (svc.name.empty()) {
        return std::nullopt;
    }

    const auto r_pos = str.find("return=");
    if (r_pos != std::string::npos) {
        const std::string ret_str = extract_quoted_or_word(str, r_pos + 7);
        if (!ret_str.empty() && ret_str != "void") {
            svc.return_type = DataType::from_string(ret_str);
        }
    }
    const auto pure_pos = str.find("pure=");
    if (pure_pos != std::string::npos) {
        const std::string val = extract_quoted_or_word(str, pure_pos + 5);
        svc.is_pure = (val == "true" || val == "1");
    }
    const auto noexc_pos = str.find("noexcept=");
    if (noexc_pos != std::string::npos) {
        const std::string val = extract_quoted_or_word(str, noexc_pos + 9);
        svc.is_noexcept = (val == "true" || val == "1");
    }
    const auto d_pos = str.find("desc=");
    if (d_pos != std::string::npos) {
        svc.description = extract_quoted_or_word(str, d_pos + 5);
    }

    return svc;
}

std::string DirectiveParser::format_enum_directive(const EnumDefinition& en) {
    std::string out =
        "name=" + en.name + " type=" + (en.underlying_type.empty() ? "uint8_t" : en.underlying_type) + " literals=[";
    for (size_t i = 0; i < en.literals.size(); ++i) {
        if (i > 0)
            out += ", ";
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

std::string DirectiveParser::format_enum_directive(const TypeDefinition& en) {
    std::string out =
        "name=" + en.name + " type=" + (en.underlying_type.empty() ? "uint8_t" : en.underlying_type) + " literals=[";
    for (size_t i = 0; i < en.literals.size(); ++i) {
        if (i > 0)
            out += ", ";
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

std::string DirectiveParser::format_struct_directive(const StructDefinition& st) {
    std::string out = "name=" + st.name;
    if (st.is_datatype) {
        out += " is_datatype=true";
    }
    out += " fields=[";
    for (size_t i = 0; i < st.fields.size(); ++i) {
        if (i > 0)
            out += ", ";
        out += st.fields[i].name + ":" + st.fields[i].type.to_canonical_string();
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

std::string DirectiveParser::format_struct_directive(const TypeDefinition& st) {
    std::string out = "name=" + st.name;
    if (st.is_datatype) {
        out += " is_datatype=true";
    }
    out += " fields=[";
    for (size_t i = 0; i < st.fields.size(); ++i) {
        if (i > 0)
            out += ", ";
        out += st.fields[i].name + ":" + st.fields[i].type.to_canonical_string();
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

}  // namespace fsm::frontend::directive

#include "fsm/frontend/formal/stateflow/stateflow_parser.hpp"

#include <cctype>
#include <cstdint>
#include <optional>
#include <regex>
#include <sstream>
#include <utility>

#if defined(FSMC_HAS_ZLIB) && FSMC_HAS_ZLIB
#include <zlib.h>
#endif

#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/frontend/directive/guard_parser.hpp"
#include "slx_archive_reader.hpp"
#include "stateflow_label_parser.hpp"

namespace fsm::frontend::formal {

using namespace fsm::ir;
using namespace stateflow_detail;

namespace {
static std::string decode_sf_newlines(std::string s) {
    // Handle numeric newline entities &#10; / &#x0A; if not already decoded by SimpleXmlParser
    size_t pos = 0;
    while ((pos = s.find("&#10;", pos)) != std::string::npos) {
        s.replace(pos, 5, "\n");
        ++pos;
    }
    pos = 0;
    while ((pos = s.find("&#xA;", pos)) != std::string::npos) {
        s.replace(pos, 5, "\n");
        ++pos;
    }
    return s;
}

static std::string get_sf_prop(const std::shared_ptr<XmlNode>& node, const std::string& prop_name) {
    if (!node) {
        return {};
    }
    std::string attr = node->get_attr(prop_name);
    if (!attr.empty()) {
        return decode_sf_newlines(std::move(attr));
    }
    for (const auto& child : node->children) {
        if (child->tag == "P" && child->get_attr("Name") == prop_name) {
            return decode_sf_newlines(child->text_content);
        }
    }
    return {};
}

static std::string extract_state_name_from_label(std::string_view label) {
    std::istringstream iss{std::string(label)};
    std::string first_line;
    while (std::getline(iss, first_line)) {
        // Trim whitespace
        size_t start = first_line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            continue;
        }
        size_t end = first_line.find_last_not_of(" \t\r\n");
        std::string trimmed = first_line.substr(start, end - start + 1);
        if (trimmed.rfind("entry:", 0) == 0 || trimmed.rfind("en:", 0) == 0 || trimmed.rfind("during:", 0) == 0 ||
            trimmed.rfind("du:", 0) == 0 || trimmed.rfind("exit:", 0) == 0 || trimmed.rfind("ex:", 0) == 0) {
            return {};
        }
        // State name may be followed by '/' on the same line (e.g. "Idle / entry: x = 0")
        size_t slash = trimmed.find('/');
        if (slash != std::string::npos) {
            trimmed = trimmed.substr(0, slash);
            size_t e2 = trimmed.find_last_not_of(" \t");
            trimmed = (e2 == std::string::npos) ? "" : trimmed.substr(0, e2 + 1);
        }
        return trimmed;
    }
    return {};
}

static std::string map_sf_datatype(std::string_view sf_type) {
    if (sf_type == "double") {
        return "double";
    }
    if (sf_type == "single" || sf_type == "float") {
        return "float";
    }
    if (sf_type == "boolean" || sf_type == "bool") {
        return "bool";
    }
    if (sf_type == "int8") {
        return "int8_t";
    }
    if (sf_type == "uint8") {
        return "uint8_t";
    }
    if (sf_type == "int16") {
        return "int16_t";
    }
    if (sf_type == "uint16") {
        return "uint16_t";
    }
    if (sf_type == "int32" || sf_type == "int") {
        return "int32_t";
    }
    if (sf_type == "uint32") {
        return "uint32_t";
    }
    return sf_type.empty() ? "int32_t" : std::string(sf_type);
}

}  // namespace

bool StateflowParser::parse(std::string_view content, FsmIr& out_ir, diagnostic::DiagnosticEngine& diag) {
    std::string err;
    if (!parse_impl(content, out_ir, err)) {
        report_parse_error(diag, "E_PARSE_STATEFLOW", err);
        return false;
    }
    return true;
}

bool StateflowParser::parse_impl(std::string_view content, FsmIr& model, std::string& error_message) {
    std::string xml_storage;
    std::string_view xml_view = content;

    // Direct .slx ZIP archive detection (PK\x03\x04)
    if (content.size() >= 4 && content[0] == 'P' && content[1] == 'K' && content[2] == '\x03' && content[3] == '\x04') {
        if (!extract_stateflow_from_slx(content, xml_storage, error_message)) {
            return false;
        }
        xml_view = xml_storage;
    }

    std::string xml_err;
    auto root = SimpleXmlParser::parse(xml_view, xml_err);
    if (!root) {
        error_message = "Stateflow Parser: Failed to parse XML structure: " + xml_err;
        return false;
    }

    // Find Stateflow root or Chart element
    std::shared_ptr<XmlNode> chart_node;
    if (root->tag == "Stateflow" || root->tag == "chart" || root->tag == "machine") {
        chart_node = find_element_recursive(root, "chart");
        if (!chart_node) {
            chart_node = root;
        }
    } else {
        chart_node = find_element_recursive(root, "chart");
    }

    if (!chart_node) {
        error_message = "Stateflow Parser: Root <Stateflow> or <chart> element not found.";
        return false;
    }

    std::string chart_name = get_sf_prop(chart_node, "name");
    if (!chart_name.empty()) {
        model.name = sanitize_identifier(chart_name);
    } else {
        model.name = "StateflowChart";
    }

    std::string chart_initial = get_sf_prop(chart_node, "initial");
    if (!chart_initial.empty()) {
        model.initial_state = sanitize_identifier(chart_initial);
    }

    // Parse @fsm directives from XML comments
    {
        std::string raw_str{xml_view};
        static const std::regex comment_re(R"(<!--\s*@fsm:([^\r\n]+?)\s*-->)");
        auto begin = std::sregex_iterator(raw_str.begin(), raw_str.end(), comment_re);
        auto end = std::sregex_iterator();
        for (auto i = begin; i != end; ++i) {
            std::smatch match = *i;
            std::string body = match[1].str();
            directive::DirectiveParser::parse_model_directive(body, model);
        }
    }

    // Pre-pass: index all SSID -> state/junction names so transitions using <src><P Name="SSID"> resolve cleanly
    ssid_to_name_.clear();
    collect_ssids(chart_node);

    // Parse Stateflow elements
    parse_chart_elements(chart_node, model, "");

    // In Stateflow, transitions entering a composite state that owns a HISTORY junction target its history
    auto is_descendant_of = [&](const std::string& state_name, const std::string& ancestor_name) {
        std::string cur = state_name;
        for (std::size_t depth = 0; !cur.empty() && depth < model.states.size(); ++depth) {
            if (cur == ancestor_name) {
                return true;
            }
            const auto* st = model.find_state(cur);
            cur = (st != nullptr) ? st->parent_state : "";
        }
        return false;
    };
    for (auto& t : model.transitions) {
        if (t.target_is_history || t.target.empty()) {
            continue;
        }
        const auto* tgt = model.find_state(t.target);
        if (tgt != nullptr && tgt->has_history && !is_descendant_of(t.source, tgt->name)) {
            t.target_is_history = true;
            t.target_is_deep_history = tgt->has_deep_history;
        }
    }

    if (model.states.empty()) {
        error_message = "Stateflow Parser: No states found in Stateflow chart.";
        return false;
    }

    if (model.initial_state.empty() && !model.states.empty()) {
        model.initial_state = model.states.front().name;
    }

    return true;
}

std::shared_ptr<XmlNode> StateflowParser::find_element_recursive(const std::shared_ptr<XmlNode>& node,
                                                                 std::string_view tag_name) {
    if (!node)
        return nullptr;
    if (node->tag == tag_name)
        return node;
    for (const auto& child : node->children) {
        if (auto found = find_element_recursive(child, tag_name)) {
            return found;
        }
    }
    return nullptr;
}

void StateflowParser::collect_ssids(const std::shared_ptr<XmlNode>& node) {
    if (!node) {
        return;
    }
    for (const auto& child : node->children) {
        if (child->tag == "Children") {
            collect_ssids(child);
            continue;
        }
        if (child->tag == "state" || child->tag == "State") {
            std::string ssid = child->get_attr("SSID");
            if (ssid.empty()) {
                ssid = child->get_attr("id");
            }
            std::string st_name = get_sf_prop(child, "name");
            if (st_name.empty()) {
                st_name = extract_state_name_from_label(get_sf_prop(child, "labelString"));
            }
            if (st_name.empty() && !ssid.empty()) {
                st_name = "State_" + ssid;
            }
            if (!ssid.empty() && !st_name.empty()) {
                ssid_to_name_[ssid] = sanitize_identifier(st_name);
            }
            collect_ssids(child);
        } else if (child->tag == "junction" || child->tag == "Junction") {
            std::string jtype = get_sf_prop(child, "type");
            if (jtype == "HISTORY" || jtype == "history" || jtype == "HISTORY_JUNCTION" || jtype == "HISTORY_DEEP" ||
                jtype == "deep_history") {
                continue;
            }
            std::string jid = child->get_attr("SSID");
            if (jid.empty()) {
                jid = child->get_attr("id");
            }
            std::string jname = get_sf_prop(child, "name");
            if (jname.empty()) {
                std::string jdesc = get_sf_prop(child, "description");
                auto cpos = jdesc.find("@fsm:choice=");
                if (cpos != std::string::npos) {
                    jname = jdesc.substr(cpos + 12);
                    auto semi = jname.find(';');
                    if (semi != std::string::npos) {
                        jname = jname.substr(0, semi);
                    }
                }
            }
            if (jname.empty() && !jid.empty()) {
                jname = "Junction_" + jid;
            }
            if (!jid.empty() && !jname.empty()) {
                ssid_to_name_[jid] = sanitize_identifier(jname);
            }
        }
    }
}

void StateflowParser::parse_chart_elements(const std::shared_ptr<XmlNode>& node, FsmIr& model,
                                           const std::string& parent_state) {
    for (const auto& child : node->children) {
        if (child->tag == "Children") {
            parse_chart_elements(child, model, parent_state);
            continue;
        }
        if (child->tag == "state" || child->tag == "State") {
            std::string state_label = get_sf_prop(child, "labelString");
            if (state_label.empty()) {
                state_label = get_sf_prop(child, "label");
            }
            std::string st_name = get_sf_prop(child, "name");
            std::string ssid = child->get_attr("SSID");
            if (st_name.empty() && !state_label.empty()) {
                st_name = extract_state_name_from_label(state_label);
            }
            if (st_name.empty()) {
                st_name = child->get_attr("id");
            }
            if (st_name.empty() && !ssid.empty()) {
                st_name = "State_" + ssid;
            }
            if (st_name.empty()) {
                st_name = "State_" + std::to_string(model.states.size() + 1);
            }

            st_name = sanitize_identifier(st_name);
            model.add_state(st_name, parent_state);

            // Check for Stateflow decomposition (parallel/AND: SET_STATE / PARALLEL_AND / AND)
            std::string decomp = get_sf_prop(child, "decomposition");
            if (decomp == "PARALLEL_AND" || decomp == "AND" || decomp == "SET_STATE") {
                if (auto* s = model.find_state_mut(st_name)) {
                    s->kind = StateKind::Parallel;
                }
            }

            auto* s = model.find_state_mut(st_name);
            std::string initial_sub = get_sf_prop(child, "initial");
            if (s != nullptr && !initial_sub.empty()) {
                s->initial_sub_state = sanitize_identifier(initial_sub);
            }

            // Parse entry, during, exit attributes
            std::string entry_act = child->get_attr("entry");
            if (entry_act.empty())
                entry_act = child->get_attr("en");
            if (entry_act.empty())
                entry_act = child->get_attr("on_entry");

            std::string during_act = child->get_attr("during");
            if (during_act.empty())
                during_act = child->get_attr("du");
            if (during_act.empty())
                during_act = child->get_attr("do_activity");

            std::string exit_act = child->get_attr("exit");
            if (exit_act.empty())
                exit_act = child->get_attr("ex");
            if (exit_act.empty())
                exit_act = child->get_attr("on_exit");

            std::string defer_attr = get_sf_prop(child, "defer");
            std::string req_attr = get_sf_prop(child, "satisfies");
            std::string inv_attr = get_sf_prop(child, "invariant");
            const std::string st_desc = get_sf_prop(child, "description");
            if (!st_desc.empty()) {
                std::istringstream dstream(st_desc);
                std::string dseg;
                while (std::getline(dstream, dseg, ';')) {
                    std::string seg = std::string(trim(dseg));
                    if (seg.rfind("@fsm:defer=", 0) == 0 && defer_attr.empty()) {
                        defer_attr = seg.substr(11);
                    } else if (seg.rfind("@fsm:satisfies=", 0) == 0 && req_attr.empty()) {
                        req_attr = seg.substr(15);
                    } else if (seg.rfind("@fsm:invariant=", 0) == 0 && inv_attr.empty()) {
                        inv_attr = seg.substr(15);
                    }
                }
            }
            if (!defer_attr.empty() && s != nullptr) {
                std::istringstream dss(defer_attr);
                std::string dev;
                while (std::getline(dss, dev, ',')) {
                    std::string d_evt = sanitize_identifier(trim(dev));
                    if (!d_evt.empty()) {
                        model.add_event(d_evt);
                        s->deferred_events.push_back(d_evt);
                    }
                }
            }
            if (!req_attr.empty() && s != nullptr) {
                std::istringstream rss(req_attr);
                std::string req_item;
                while (std::getline(rss, req_item, ',')) {
                    std::string r = std::string(trim(req_item));
                    if (!r.empty() && std::find(s->traceability_reqs.begin(), s->traceability_reqs.end(), r) ==
                                          s->traceability_reqs.end()) {
                        s->traceability_reqs.push_back(r);
                    }
                }
            }
            if (!inv_attr.empty() && s != nullptr) {
                s->time_invariant = inv_attr;
            }

            // Parse state labelString (e.g. StateName\nentry: ...\nduring: ...\nexit: ...)
            if (!state_label.empty()) {
                std::istringstream stream(state_label);
                std::string line;
                enum class Section { None, Entry, During, Exit };
                Section cur_sec = Section::None;
                while (std::getline(stream, line)) {
                    std::string t_line = std::string(trim(line));
                    if (t_line.empty())
                        continue;
                    if (t_line.rfind("entry:", 0) == 0 || t_line.rfind("en:", 0) == 0) {
                        cur_sec = Section::Entry;
                        size_t colon = t_line.find(':');
                        t_line = std::string(trim(t_line.substr(colon + 1)));
                    } else if (t_line.rfind("during:", 0) == 0 || t_line.rfind("du:", 0) == 0) {
                        cur_sec = Section::During;
                        size_t colon = t_line.find(':');
                        t_line = std::string(trim(t_line.substr(colon + 1)));
                    } else if (t_line.rfind("exit:", 0) == 0 || t_line.rfind("ex:", 0) == 0) {
                        cur_sec = Section::Exit;
                        size_t colon = t_line.find(':');
                        t_line = std::string(trim(t_line.substr(colon + 1)));
                    }
                    if (t_line.empty())
                        continue;
                    if (cur_sec == Section::Entry) {
                        if (!entry_act.empty())
                            entry_act += "; ";
                        entry_act += t_line;
                    } else if (cur_sec == Section::During) {
                        if (!during_act.empty())
                            during_act += "; ";
                        during_act += t_line;
                    } else if (cur_sec == Section::Exit) {
                        if (!exit_act.empty())
                            exit_act += "; ";
                        exit_act += t_line;
                    }
                }
            }

            if (s != nullptr) {
                auto append_lifecycle_actions = [&](const std::string& raw_list, const std::string& suffix,
                                                    std::vector<ActionSignature>& target) {
                    if (raw_list.empty()) {
                        return;
                    }
                    std::istringstream iss(raw_list);
                    std::string part;
                    while (std::getline(iss, part, ';')) {
                        std::string a = std::string(trim(part));
                        if (!a.empty()) {
                            ActionSignature sig = build_stateflow_action(a, st_name + suffix);
                            model.add_action(sig.name);
                            target.push_back(std::move(sig));
                        }
                    }
                };
                append_lifecycle_actions(entry_act, "_entry", s->entry_actions);
                if (!during_act.empty()) {
                    s->do_activity = sanitize_identifier(during_act);
                }
                append_lifecycle_actions(exit_act, "_exit", s->exit_actions);
            }

            // Recursively parse child states and transitions
            parse_chart_elements(child, model, st_name);

            // Populate orthogonal_regions for PARALLEL_AND states after children are parsed
            if (auto* par_st = model.find_state_mut(st_name);
                par_st != nullptr && par_st->kind == StateKind::Parallel) {
                par_st->is_composite = true;
                par_st->initial_sub_state.clear();
                auto is_pseudo = [](const StateNode& sn) {
                    return sn.kind == StateKind::Junction || sn.kind == StateKind::Choice ||
                           sn.kind == StateKind::Fork || sn.kind == StateKind::Join ||
                           sn.kind == StateKind::EntryPoint || sn.kind == StateKind::ExitPoint;
                };
                for (const auto& reg_node : model.states) {
                    if (reg_node.parent_state != st_name || is_pseudo(reg_node)) {
                        continue;
                    }
                    OrthogonalRegion reg;
                    reg.id = reg_node.name;
                    reg.name = reg_node.name;
                    reg.initial_state_id = reg_node.initial_sub_state;
                    for (const auto& leaf : model.states) {
                        if (leaf.parent_state == reg_node.name && !is_pseudo(leaf)) {
                            reg.state_ids.push_back(leaf.name);
                            if (reg.initial_state_id.empty()) {
                                reg.initial_state_id = leaf.name;
                            }
                        }
                    }
                    par_st->orthogonal_regions.push_back(std::move(reg));
                }
            }
        } else if (child->tag == "junction" || child->tag == "Junction") {
            std::string jtype = get_sf_prop(child, "type");
            std::string jid = child->get_attr("SSID");
            if (jid.empty()) {
                jid = child->get_attr("id");
            }
            if (jtype == "HISTORY" || jtype == "history" || jtype == "HISTORY_JUNCTION") {
                if (auto* s = model.find_state_mut(parent_state)) {
                    s->has_history = true;
                    const std::string jdesc = get_sf_prop(child, "description");
                    s->has_deep_history = (jdesc.find("@fsm:history=deep") != std::string::npos);
                }
            } else if (jtype == "HISTORY_DEEP" || jtype == "deep_history") {
                if (auto* s = model.find_state_mut(parent_state)) {
                    s->has_history = true;
                    s->has_deep_history = true;
                }
            } else {
                std::string jname = get_sf_prop(child, "name");
                const std::string jdesc = get_sf_prop(child, "description");
                if (jname.empty()) {
                    auto cpos = jdesc.find("@fsm:choice=");
                    if (cpos != std::string::npos) {
                        jname = jdesc.substr(cpos + 12);
                        auto semi = jname.find(';');
                        if (semi != std::string::npos) {
                            jname = jname.substr(0, semi);
                        }
                        jname = sanitize_identifier(jname);
                        model.add_choice_node(jname);
                        continue;
                    }
                }
                if (jname.empty()) {
                    jname = "Junction_" + (jid.empty() ? std::to_string(model.states.size() + 1) : jid);
                }
                jname = sanitize_identifier(jname);
                model.add_state(jname, parent_state, StateKind::Junction);
            }
        } else if (child->tag == "data" || child->tag == "Data") {
            std::string d_name = get_sf_prop(child, "name");
            if (!d_name.empty()) {
                d_name = sanitize_identifier(d_name);
                std::string scope = get_sf_prop(child, "scope");
                std::string d_type = map_sf_datatype(get_sf_prop(child, "dataType"));
                if (scope == "INPUT_DATA" || scope == "OUTPUT_DATA" || scope == "DATA_STORE_MEMORY_DATA" ||
                    scope == "INOUT_DATA") {
                    if (model.find_port(d_name) == nullptr) {
                        PortDefinition p;
                        p.name = d_name;
                        p.type = d_type;
                        p.direction =
                            (scope == "OUTPUT_DATA")
                                ? PortDirection::Out
                                : ((scope == "DATA_STORE_MEMORY_DATA" || scope == "INOUT_DATA") ? PortDirection::InOut
                                                                                                : PortDirection::In);
                        model.add_port(std::move(p));
                    }
                } else if (scope == "LOCAL_DATA" || scope.empty()) {
                    if (model.find_variable(d_name) == nullptr) {
                        model.add_variable(VariableDefinition(d_name, d_type, ""));
                    }
                }
            }
        } else if (child->tag == "event" || child->tag == "Event") {
            std::string ev_name = get_sf_prop(child, "name");
            if (!ev_name.empty()) {
                ev_name = sanitize_identifier(ev_name);
                if (!ev_name.empty()) {
                    model.add_event(ev_name);
                }
            }
        } else if (child->tag == "transition" || child->tag == "Transition") {
            parse_stateflow_transition(child, model, parent_state);
        }
    }
}

void StateflowParser::parse_stateflow_transition(const std::shared_ptr<XmlNode>& trans_node, FsmIr& model,
                                                 const std::string& scope) {
    auto resolve_endpoint = [&](const std::string& tag_name) -> std::string {
        std::string direct = trans_node->get_attr(tag_name);
        if (!direct.empty()) {
            return direct;
        }
        if (auto ep = trans_node->find_first_child(tag_name)) {
            std::string ssid = get_sf_prop(ep, "SSID");
            if (!ssid.empty()) {
                auto it = ssid_to_name_.find(ssid);
                return (it != ssid_to_name_.end()) ? it->second : ("State_" + ssid);
            }
        }
        return {};
    };
    std::string src = resolve_endpoint("src");
    std::string dst = resolve_endpoint("dst");
    if (src.empty() && !dst.empty()) {
        if (scope.empty() && model.initial_state.empty()) {
            model.initial_state = sanitize_identifier(dst);
        } else if (!scope.empty()) {
            if (auto* parent = model.find_state_mut(scope)) {
                if (parent->initial_sub_state.empty()) {
                    parent->initial_sub_state = sanitize_identifier(dst);
                }
            }
        }
        return;
    }

    std::string label = get_sf_prop(trans_node, "labelString");
    if (label.empty()) {
        label = get_sf_prop(trans_node, "label");
    }

    auto strip_matlab_comments = [](std::string_view input) -> std::string {
        std::string out;
        out.reserve(input.size());
        char quote = '\0';
        for (size_t i = 0; i < input.size(); ++i) {
            char c = input[i];
            if (quote != '\0') {
                out.push_back(c);
                if (c == '\\' && i + 1 < input.size()) {
                    out.push_back(input[++i]);
                } else if (c == quote) {
                    quote = '\0';
                }
                continue;
            }
            if (c == '"' || c == '\'') {
                quote = c;
                out.push_back(c);
                continue;
            }
            if (c == '%') {
                while (i < input.size() && input[i] != '\n') {
                    ++i;
                }
                if (i < input.size() && input[i] == '\n') {
                    out.push_back('\n');
                }
                continue;
            }
            out.push_back(c);
        }
        return std::string{trim(out)};
    };

    auto comps = parse_stateflow_label(label);
    comps.guard = strip_matlab_comments(comps.guard);
    comps.condition_action = strip_matlab_comments(comps.condition_action);
    comps.transition_action = strip_matlab_comments(comps.transition_action);

    std::string src_name = sanitize_identifier(src.empty() ? scope : src);
    std::string dst_name = sanitize_identifier(dst.empty() ? src_name : dst);

    if (src_name.empty() || dst_name.empty()) {
        return;
    }

    TransitionEdge trans;
    trans.source = src_name;
    trans.target = dst_name;
    std::string trans_type = get_sf_prop(trans_node, "type");
    if (trans_type.empty()) {
        trans_type = get_sf_prop(trans_node, "kind");
    }
    const std::string trans_desc = get_sf_prop(trans_node, "description");
    if (!trans_desc.empty()) {
        std::istringstream dstream(trans_desc);
        std::string dseg;
        while (std::getline(dstream, dseg, ';')) {
            std::string seg = std::string(trim(dseg));
            if (seg.rfind("@fsm:kind=", 0) == 0 && trans_type.empty()) {
                trans_type = seg.substr(10);
            } else if (seg.rfind("@fsm:priority=", 0) == 0) {
                try {
                    trans.priority = static_cast<std::uint32_t>(std::stoul(seg.substr(14)));
                } catch (...) {
                }
            }
        }
    }
    if (trans.priority == 0) {
        std::string exec_order = get_sf_prop(trans_node, "executionOrder");
        if (!exec_order.empty()) {
            try {
                trans.priority = static_cast<std::uint32_t>(std::stoul(exec_order));
            } catch (...) {
            }
        }
    }
    if (trans_type == "internal" || (dst.empty() && !src.empty())) {
        trans.kind = TransitionEdgeKind::Internal;
    } else if (trans_type == "local") {
        trans.kind = TransitionEdgeKind::Local;
    }
    static const std::regex when_ev_regex(R"(^\s*when\s*\((.+)\)\s*$)", std::regex::icase);
    std::smatch wm;
    if (!comps.event.empty() && std::regex_match(comps.event, wm, when_ev_regex)) {
        trans.trigger = ChangeTrigger(std::string(trim(wm[1].str())), true);
    } else {
        trans.event = sanitize_identifier(comps.event);
    }
    if (comps.time_trigger.has_value()) {
        trans.trigger = *comps.time_trigger;
    }
    if (!comps.guard.empty()) {
        std::string norm_guard;
        norm_guard.reserve(comps.guard.size());
        for (size_t gi = 0; gi < comps.guard.size(); ++gi) {
            if (comps.guard[gi] == '~') {
                if (gi + 1 < comps.guard.size() && comps.guard[gi + 1] == '=') {
                    norm_guard += "!=";
                    ++gi;
                } else {
                    norm_guard.push_back('!');
                }
            } else {
                norm_guard.push_back(comps.guard[gi]);
            }
        }
        auto parsed = directive::GuardExpressionParser::parse(norm_guard);
        if (!parsed.canonical_expression.empty()) {
            trans.guard = parsed.canonical_expression;
            for (const auto& detail : parsed.atomic_guard_details) {
                model.add_guard(detail.name, "", detail.expression, detail.expression);
            }
            for (const auto& a : parsed.atomic_guards) {
                model.add_guard(a);
            }
        } else {
            // Generate a stable synthetic guard name instead of sanitizing the raw
            // boolean expression (which would corrupt operator characters like >, &&, ! etc.).
            std::string guard_name = "guard_" + sanitize_identifier(src_name) + "_to_" + sanitize_identifier(dst_name) +
                                     "_" + std::to_string(model.transitions.size() + 1);
            model.add_guard(guard_name, "", std::optional<std::string>{norm_guard},
                            std::optional<std::string>{norm_guard});
            trans.guard = guard_name;
        }
    }
    auto disambiguate_action = [&](ActionSignature& sig) {
        if (sig.assignments.empty()) {
            return;
        }
        const std::string base = sig.name;
        std::size_t suffix = 2;
        auto has_action = [&](const std::string& n) {
            return std::any_of(model.actions.begin(), model.actions.end(), [&](const auto& a) { return a.name == n; });
        };
        while (has_action(sig.name)) {
            sig.name = base + "_" + std::to_string(suffix++);
            sig.invocation = sig.name;
        }
    };
    if (!comps.condition_action.empty()) {
        std::string prefix = sanitize_identifier(src_name) + "_" + sanitize_identifier(dst_name) + "_cond";
        ActionSignature sig = build_stateflow_action(comps.condition_action, prefix);
        disambiguate_action(sig);
        trans.condition_action = std::move(sig);
        model.add_action(trans.condition_action->name);
    }
    if (!comps.transition_action.empty()) {
        std::string prefix = sanitize_identifier(src_name) + "_" + sanitize_identifier(dst_name) + "_act";
        ActionSignature sig = build_stateflow_action(comps.transition_action, prefix);
        disambiguate_action(sig);
        trans.transition_action = std::move(sig);
        model.add_action(trans.transition_action->name);
    }

    const auto* src_st = model.find_state(trans.source);
    if (src_st != nullptr && !src_st->parent_state.empty()) {
        trans.parent_scope = src_st->parent_state;
    }

    model.add_transition(std::move(trans));
}

}  // namespace fsm::frontend::formal

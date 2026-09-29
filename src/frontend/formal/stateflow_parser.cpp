#include "fsm/frontend/formal/stateflow_parser.hpp"

#include <cctype>
#include <cstdint>
#include <optional>
#include <regex>
#include <utility>

#if defined(FSMC_HAS_ZLIB) && FSMC_HAS_ZLIB
#include <zlib.h>
#endif

#include "fsm/frontend/directive/directive_parser.hpp"
#include "fsm/frontend/directive/guard_parser.hpp"

namespace fsm::frontend::formal {

using namespace fsm::ir;
using directive::DirectiveParser;
using directive::GuardExpressionParser;

namespace {

uint16_t read_u16_le(const unsigned char* p) noexcept {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t read_u32_le(const unsigned char* p) noexcept {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

bool decompress_entry(uint16_t method, const unsigned char* data_ptr, uint32_t comp_size, uint32_t uncomp_size,
                      const std::string& filename, std::string& out, std::string& err) {
    if (method == 0) {
        // Stored (uncompressed)
        out.assign(reinterpret_cast<const char*>(data_ptr), uncomp_size);
        return true;
    }
    if (method == 8) {
#if defined(FSMC_HAS_ZLIB) && FSMC_HAS_ZLIB
        out.resize(uncomp_size);
        z_stream strm{};
        strm.next_in = const_cast<Bytef*>(data_ptr);
        strm.avail_in = comp_size;
        strm.next_out = reinterpret_cast<Bytef*>(out.data());
        strm.avail_out = uncomp_size;

        if (inflateInit2(&strm, -15) != Z_OK) {
            err = "Stateflow Parser: Failed to initialize zlib inflate for '" + filename + "'";
            return false;
        }
        int ret = inflate(&strm, Z_FINISH);
        inflateEnd(&strm);
        if (ret != Z_STREAM_END && ret != Z_OK) {
            err = "Stateflow Parser: Decompression failed for entry '" + filename + "'";
            return false;
        }
        out.resize(strm.total_out);
        return true;
#else
        (void)comp_size;
        (void)uncomp_size;
        (void)data_ptr;
        err = "Stateflow Parser: .slx entry '" + filename +
              "' is compressed with Deflate, but FSMC was compiled without ZLIB support.";
        return false;
#endif
    }
    err = "Stateflow Parser: Unsupported compression method (" + std::to_string(method) + ") in .slx";
    return false;
}

bool extract_stateflow_from_slx(std::string_view zip_content, std::string& extracted_xml, std::string& err) {
    if (zip_content.size() < 22) {
        err = "Stateflow Parser: File size too small for .slx archive.";
        return false;
    }

    const auto* bytes = reinterpret_cast<const unsigned char*>(zip_content.data());
    const std::size_t total_size = zip_content.size();

    // 1. Try Central Directory method via End of Central Directory (EOCD)
    std::size_t search_start = (total_size > 65557) ? total_size - 65557 : 0;
    std::optional<std::size_t> eocd_pos;
    for (std::size_t i = total_size - 22; i >= search_start; --i) {
        if (read_u32_le(bytes + i) == 0x06054b50) {
            eocd_pos = i;
            break;
        }
        if (i == 0)
            break;
    }

    std::string fallback_blockdiagram_xml;

    if (eocd_pos.has_value()) {
        std::size_t ep = *eocd_pos;
        uint16_t num_entries = read_u16_le(bytes + ep + 10);
        uint32_t cd_offset = read_u32_le(bytes + ep + 16);

        std::size_t cd_pos = cd_offset;
        for (uint16_t e = 0; e < num_entries && cd_pos + 46 <= total_size; ++e) {
            if (read_u32_le(bytes + cd_pos) != 0x02014b50) {
                break;
            }
            uint16_t method = read_u16_le(bytes + cd_pos + 10);
            uint32_t comp_size = read_u32_le(bytes + cd_pos + 20);
            uint32_t uncomp_size = read_u32_le(bytes + cd_pos + 24);
            uint16_t fn_len = read_u16_le(bytes + cd_pos + 28);
            uint16_t extra_len = read_u16_le(bytes + cd_pos + 30);
            uint16_t comment_len = read_u16_le(bytes + cd_pos + 32);
            uint32_t local_hdr_offset = read_u32_le(bytes + cd_pos + 42);

            if (cd_pos + 46 + fn_len > total_size) {
                break;
            }
            std::string filename(reinterpret_cast<const char*>(bytes + cd_pos + 46), fn_len);

            bool is_stateflow = (filename == "simulink/stateflow.xml" || filename == "stateflow.xml" ||
                                 filename.find("stateflow.xml") != std::string::npos);
            bool is_blockdiagram =
                (!is_stateflow && (filename == "simulink/blockdiagram.xml" || filename == "blockdiagram.xml" ||
                                   filename.find("blockdiagram.xml") != std::string::npos));

            if ((is_stateflow || is_blockdiagram) && local_hdr_offset + 30 <= total_size) {
                uint16_t loc_fn_len = read_u16_le(bytes + local_hdr_offset + 26);
                uint16_t loc_extra_len = read_u16_le(bytes + local_hdr_offset + 28);
                std::size_t data_offset = local_hdr_offset + 30 + loc_fn_len + loc_extra_len;

                if (data_offset + comp_size <= total_size) {
                    std::string decomp;
                    if (decompress_entry(method, bytes + data_offset, comp_size, uncomp_size, filename, decomp, err)) {
                        if (is_stateflow) {
                            extracted_xml = std::move(decomp);
                            return true;
                        }
                        if (is_blockdiagram && fallback_blockdiagram_xml.empty()) {
                            fallback_blockdiagram_xml = std::move(decomp);
                        }
                    } else if (is_stateflow) {
                        return false;
                    }
                }
            }

            cd_pos += 46 + fn_len + extra_len + comment_len;
        }

        if (!fallback_blockdiagram_xml.empty()) {
            extracted_xml = std::move(fallback_blockdiagram_xml);
            return true;
        }
    }

    // 2. Fallback to scanning Local File Headers from beginning
    std::size_t offset = 0;
    while (offset + 30 <= total_size) {
        if (read_u32_le(bytes + offset) != 0x04034b50) {
            break;
        }
        uint16_t method = read_u16_le(bytes + offset + 8);
        uint32_t comp_size = read_u32_le(bytes + offset + 18);
        uint32_t uncomp_size = read_u32_le(bytes + offset + 22);
        uint16_t fn_len = read_u16_le(bytes + offset + 26);
        uint16_t extra_len = read_u16_le(bytes + offset + 28);

        std::size_t header_len = 30 + fn_len + extra_len;
        if (offset + header_len + comp_size > total_size) {
            break;
        }

        std::string filename(reinterpret_cast<const char*>(bytes + offset + 30), fn_len);
        bool is_stateflow = (filename == "simulink/stateflow.xml" || filename == "stateflow.xml" ||
                             filename.find("stateflow.xml") != std::string::npos);
        bool is_blockdiagram =
            (!is_stateflow && (filename == "simulink/blockdiagram.xml" || filename == "blockdiagram.xml" ||
                               filename.find("blockdiagram.xml") != std::string::npos));

        if (is_stateflow || is_blockdiagram) {
            std::string decomp;
            if (decompress_entry(method, bytes + offset + header_len, comp_size, uncomp_size, filename, decomp, err)) {
                if (is_stateflow) {
                    extracted_xml = std::move(decomp);
                    return true;
                }
                if (is_blockdiagram && fallback_blockdiagram_xml.empty()) {
                    fallback_blockdiagram_xml = std::move(decomp);
                }
            }
        }

        offset += header_len + comp_size;
    }

    if (!fallback_blockdiagram_xml.empty()) {
        extracted_xml = std::move(fallback_blockdiagram_xml);
        return true;
    }

    err = "Stateflow Parser: No 'simulink/stateflow.xml' or 'simulink/blockdiagram.xml' found inside .slx archive.";
    return false;
}

}  // namespace

bool StateflowParser::parse(std::string_view content, FsmIr& model, std::string& error_message) {
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

    std::string chart_name = chart_node->get_attr("name");
    if (!chart_name.empty()) {
        model.name = sanitize_identifier(chart_name);
    } else {
        model.name = "StateflowChart";
    }

    std::string chart_initial = chart_node->get_attr("initial");
    if (!chart_initial.empty()) {
        model.initial_state = sanitize_identifier(chart_initial);
    }

    // Parse @fsm directives from XML comments
    {
        std::string raw_str{content};
        std::regex comment_re(R"(<!--\s*@fsm:([^\r\n-]+?)\s*-->)");
        auto begin = std::sregex_iterator(raw_str.begin(), raw_str.end(), comment_re);
        auto end = std::sregex_iterator();
        for (auto i = begin; i != end; ++i) {
            std::smatch match = *i;
            std::string body = match[1].str();
            directive::DirectiveParser::parse_model_directive(body, model);
        }
    }

    // Parse Stateflow elements
    parse_chart_elements(chart_node, model, "");

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

void StateflowParser::parse_chart_elements(const std::shared_ptr<XmlNode>& node, FsmIr& model,
                                           const std::string& parent_state) {
    for (const auto& child : node->children) {
        if (child->tag == "state" || child->tag == "State") {
            std::string st_name = child->get_attr("name");
            std::string ssid = child->get_attr("SSID");
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

            // Check for Stateflow decomposition (parallel/AND vs exclusive/OR)
            std::string decomp = child->get_attr("decomposition");
            if (decomp == "PARALLEL_AND" || decomp == "AND") {
                if (auto* s = model.find_state_mut(st_name)) {
                    s->kind = StateKind::Parallel;
                }
            }

            std::string during_act = child->get_attr("during");
            if (during_act.empty()) {
                during_act = child->get_attr("do_activity");
            }
            if (!during_act.empty()) {
                if (auto* s = model.find_state_mut(st_name)) {
                    s->do_activity = sanitize_identifier(during_act);
                }
            }

            // Recursively parse child states and transitions
            parse_chart_elements(child, model, st_name);
        } else if (child->tag == "junction" || child->tag == "Junction") {
            std::string jtype = child->get_attr("type");
            std::string jid = child->get_attr("SSID");
            if (jid.empty()) {
                jid = child->get_attr("id");
            }
            if (jtype == "HISTORY" || jtype == "history") {
                if (auto* s = model.find_state_mut(parent_state)) {
                    s->has_history = true;
                    s->has_deep_history = false;
                }
            } else if (jtype == "HISTORY_DEEP" || jtype == "deep_history") {
                if (auto* s = model.find_state_mut(parent_state)) {
                    s->has_history = true;
                    s->has_deep_history = true;
                }
            } else {
                std::string jname =
                    sanitize_identifier("Junction_" + (jid.empty() ? std::to_string(model.states.size() + 1) : jid));
                model.add_state(jname, parent_state, StateKind::Junction);
            }
        } else if (child->tag == "transition" || child->tag == "Transition") {
            parse_stateflow_transition(child, model, parent_state);
        }
    }
}

StateflowParser::StateflowLabelComponents StateflowParser::parse_stateflow_label(std::string_view raw_label) {
    StateflowLabelComponents res;
    std::string label = std::string(trim(raw_label));
    if (label.empty()) {
        return res;
    }

    // Check for temporal logic after(N, sec / msec)
    static const std::regex after_re(R"(after\s*\(\s*(\d+(?:\.\d+)?)\s*,\s*(sec|msec|seconds|milliseconds|s|ms)\s*\))",
                                     std::regex::optimize);
    std::smatch match;
    if (std::regex_search(label, match, after_re)) {
        double val = std::stod(match[1].str());
        std::string unit = match[2].str();
        uint64_t dur_ms = static_cast<uint64_t>(val);
        if (unit == "sec" || unit == "s" || unit == "seconds") {
            dur_ms = static_cast<uint64_t>(val * 1000.0);
        }
        res.time_trigger = TimeTrigger(TimeTriggerKind::After, dur_ms, TimeUnit::Milliseconds);
        res.event = "after_" + std::to_string(dur_ms) + "ms";
    }

    size_t idx = 0;
    const size_t len = label.size();

    // 1. Event part: up to '[' or '{' or '/' (if not already matched temporal)
    if (res.event.empty()) {
        size_t ev_end = 0;
        while (ev_end < len && label[ev_end] != '[' && label[ev_end] != '{' && label[ev_end] != '/') {
            ev_end++;
        }
        res.event = std::string(trim(label.substr(0, ev_end)));
        idx = ev_end;
    } else {
        size_t first_delim = label.find_first_of("[{/");
        if (first_delim != std::string::npos) {
            idx = first_delim;
        } else {
            idx = len;
        }
    }

    // 2. Scan brackets and braces with nesting support
    while (idx < len) {
        char c = label[idx];
        if (c == '[') {
            idx++;
            size_t depth = 1;
            size_t g_start = idx;
            while (idx < len && depth > 0) {
                if (label[idx] == '[') {
                    depth++;
                } else if (label[idx] == ']') {
                    depth--;
                }
                if (depth > 0) {
                    idx++;
                }
            }
            res.guard = std::string(trim(label.substr(g_start, idx - g_start)));
            if (idx < len && label[idx] == ']') {
                idx++;
            }
        } else if (c == '{') {
            idx++;
            size_t depth = 1;
            size_t ca_start = idx;
            while (idx < len && depth > 0) {
                if (label[idx] == '{') {
                    depth++;
                } else if (label[idx] == '}') {
                    depth--;
                }
                if (depth > 0) {
                    idx++;
                }
            }
            res.condition_action = std::string(trim(label.substr(ca_start, idx - ca_start)));
            if (idx < len && label[idx] == '}') {
                idx++;
            }
        } else if (c == '/') {
            idx++;
            while (idx < len && (std::isspace(static_cast<unsigned char>(label[idx])) != 0)) {
                idx++;
            }
            if (idx < len && label[idx] == '{') {
                idx++;
                size_t depth = 1;
                size_t ta_start = idx;
                while (idx < len && depth > 0) {
                    if (label[idx] == '{') {
                        depth++;
                    } else if (label[idx] == '}') {
                        depth--;
                    }
                    if (depth > 0) {
                        idx++;
                    }
                }
                res.transition_action = std::string(trim(label.substr(ta_start, idx - ta_start)));
                if (idx < len && label[idx] == '}') {
                    idx++;
                }
            } else {
                res.transition_action = std::string(trim(label.substr(idx)));
                idx = len;
            }
        } else {
            idx++;
        }
    }

    return res;
}

void StateflowParser::parse_stateflow_transition(const std::shared_ptr<XmlNode>& trans_node, FsmIr& model,
                                                 const std::string& scope) {
    std::string src = trans_node->get_attr("src");
    std::string dst = trans_node->get_attr("dst");
    if (src.empty() && !dst.empty()) {
        if (scope.empty() && model.initial_state.empty()) {
            model.initial_state = sanitize_identifier(dst);
        }
        return;
    }

    std::string label = trans_node->get_attr("labelString");
    if (label.empty()) {
        label = trans_node->get_attr("label");
    }

    auto comps = parse_stateflow_label(label);

    std::string src_name = sanitize_identifier(src.empty() ? scope : src);
    std::string dst_name = sanitize_identifier(dst.empty() ? src_name : dst);

    if (src_name.empty() || dst_name.empty()) {
        return;
    }

    TransitionEdge trans;
    trans.source = src_name;
    trans.target = dst_name;
    trans.event = sanitize_identifier(comps.event);
    if (comps.time_trigger.has_value()) {
        trans.trigger = *comps.time_trigger;
    }
    if (!comps.guard.empty()) {
        auto parsed = directive::GuardExpressionParser::parse(comps.guard);
        if (!parsed.cpp_type.empty()) {
            trans.guard = parsed.cpp_type;
            for (const auto& a : parsed.atomic_guards) {
                model.add_guard(a);
            }
        } else {
            trans.guard = sanitize_identifier(comps.guard);
            model.add_guard(trans.guard.value());
        }
    }
    if (!comps.condition_action.empty()) {
        trans.condition_action = ActionSignature(sanitize_identifier(comps.condition_action));
        model.add_action(trans.condition_action->name);
    }
    if (!comps.transition_action.empty()) {
        trans.transition_action = ActionSignature(sanitize_identifier(comps.transition_action));
        model.add_action(trans.transition_action->name);
    }

    model.add_transition(std::move(trans));
}

}  // namespace fsm::frontend::formal

#include "fsm/frontend/common/xml_parser.hpp"

#include <cctype>

namespace fsm::frontend {

std::string XmlNode::get_attr(const std::string& name, const std::string& default_val) const {
    // Exact match
    auto attr_it = attributes.find(name);
    if (attr_it != attributes.end()) {
        return attr_it->second;
    }
    // Case-insensitive / prefix-agnostic match (e.g., "xmi:id" or "id" or "xmi:type" or "type")
    for (const auto& [attr_key, attr_val] : attributes) {
        if (attr_key == name || ends_with(attr_key, ":" + name)) {
            return attr_val;
        }
    }
    return default_val;
}

std::vector<std::shared_ptr<XmlNode>> XmlNode::find_children(const std::string& tag_name) const {
    std::vector<std::shared_ptr<XmlNode>> result;
    for (const auto& child : children) {
        if (child->tag == tag_name || ends_with(child->tag, ":" + tag_name)) {
            result.push_back(child);
        }
    }
    return result;
}

std::shared_ptr<XmlNode> XmlNode::find_first_child(const std::string& tag_name) const {
    for (const auto& child : children) {
        if (child->tag == tag_name || ends_with(child->tag, ":" + tag_name)) {
            return child;
        }
    }
    return nullptr;
}

std::shared_ptr<XmlNode> XmlNode::find_child_recursive(std::string_view tag_name) const {
    for (const auto& child : children) {
        if (child->tag == tag_name || ends_with(child->tag, ":" + std::string(tag_name))) {
            return child;
        }
        if (auto found = child->find_child_recursive(tag_name)) {
            return found;
        }
    }
    return nullptr;
}

std::string SimpleXmlParser::unescape_xml(std::string str) {
    auto replace_all = [](std::string& s, const std::string& from, const std::string& to) {
        size_t p = 0;
        while ((p = s.find(from, p)) != std::string::npos) {
            s.replace(p, from.length(), to);
            p += to.length();
        }
    };
    replace_all(str, "&amp;", "&");
    replace_all(str, "&lt;", "<");
    replace_all(str, "&gt;", ">");
    replace_all(str, "&quot;", "\"");
    replace_all(str, "&apos;", "'");
    return str;
}

std::string SimpleXmlParser::trim_sv(std::string_view input_sv) {
    size_t start = 0;
    while (start < input_sv.size() && (std::isspace(static_cast<unsigned char>(input_sv[start])) != 0)) {
        start++;
    }
    if (start == input_sv.size()) {
        return "";
    }
    size_t end = input_sv.size() - 1;
    while (end > start && (std::isspace(static_cast<unsigned char>(input_sv[end])) != 0)) {
        end--;
    }
    return std::string(input_sv.substr(start, end - start + 1));
}

void SimpleXmlParser::parse_tag_body(std::string_view body, std::string& out_tag,
                                     std::map<std::string, std::string>& out_attrs) {
    size_t pos = 0;
    const size_t len = body.size();

    // Skip leading space
    while (pos < len && (std::isspace(static_cast<unsigned char>(body[pos])) != 0)) {
        pos++;
    }

    // Tag name
    size_t name_start = pos;
    while (pos < len && (std::isspace(static_cast<unsigned char>(body[pos])) == 0) && body[pos] != '/') {
        pos++;
    }
    out_tag = std::string(body.substr(name_start, pos - name_start));

    // Attributes
    while (pos < len) {
        while (pos < len && (std::isspace(static_cast<unsigned char>(body[pos])) != 0)) {
            pos++;
        }
        if (pos >= len) {
            break;
        }

        size_t attr_name_start = pos;
        while (pos < len && body[pos] != '=' && (std::isspace(static_cast<unsigned char>(body[pos])) == 0)) {
            pos++;
        }
        std::string attr_name(body.substr(attr_name_start, pos - attr_name_start));

        while (pos < len && (std::isspace(static_cast<unsigned char>(body[pos])) != 0)) {
            pos++;
        }
        if (pos < len && body[pos] == '=') {
            pos++;
            while (pos < len && (std::isspace(static_cast<unsigned char>(body[pos])) != 0)) {
                pos++;
            }
            if (pos < len && (body[pos] == '"' || body[pos] == '\'')) {
                char quote = body[pos++];
                size_t val_start = pos;
                while (pos < len && body[pos] != quote) {
                    pos++;
                }
                std::string attr_val(body.substr(val_start, pos - val_start));
                if (pos < len && body[pos] == quote) {
                    pos++;
                }
                out_attrs[attr_name] = unescape_xml(attr_val);
            }
        } else if (!attr_name.empty()) {
            out_attrs[attr_name] = "true";
        }
    }
}

std::shared_ptr<XmlNode> SimpleXmlParser::parse(std::string_view xml_text, std::string& err) {
    size_t idx = 0;
    const size_t len = xml_text.size();
    auto root = std::make_shared<XmlNode>();
    root->tag = "__ROOT__";

    std::vector<std::shared_ptr<XmlNode>> node_stack;
    node_stack.push_back(root);

    while (idx < len) {
        // Find '<'
        size_t tag_start = xml_text.find('<', idx);
        if (tag_start == std::string_view::npos) {
            // Remaining text
            if (idx < len && !node_stack.empty()) {
                std::string_view text = xml_text.substr(idx);
                std::string clean_text = trim_sv(text);
                if (!clean_text.empty()) {
                    node_stack.back()->text_content += unescape_xml(clean_text);
                }
            }
            break;
        }

        // Text before tag
        if (tag_start > idx && !node_stack.empty()) {
            std::string_view text = xml_text.substr(idx, tag_start - idx);
            std::string clean_text = trim_sv(text);
            if (!clean_text.empty()) {
                node_stack.back()->text_content += unescape_xml(clean_text);
            }
        }

        // Check CDATA: <![CDATA[ ... ]]>
        if (starts_with(xml_text.substr(tag_start), "<![CDATA[")) {
            size_t cdata_end = xml_text.find("]]>", tag_start + 9);
            if (cdata_end == std::string_view::npos) {
                err = "Malformed XML: unclosed CDATA section";
                return nullptr;
            }
            std::string_view cdata_content = xml_text.substr(tag_start + 9, cdata_end - (tag_start + 9));
            if (!node_stack.empty()) {
                node_stack.back()->text_content += std::string(cdata_content);
            }
            idx = cdata_end + 3;
            continue;
        }

        // Check comment <!-- ... -->
        if (starts_with(xml_text.substr(tag_start), "<!--")) {
            size_t comment_end = xml_text.find("-->", tag_start + 4);
            if (comment_end == std::string_view::npos) {
                idx = len;
                break;
            }
            idx = comment_end + 3;
            continue;
        }

        // Check processing instruction <? ... ?> or <! ... > (e.g., <!DOCTYPE ...>)
        if (starts_with(xml_text.substr(tag_start), "<?") || starts_with(xml_text.substr(tag_start), "<!")) {
            size_t pi_end = xml_text.find('>', tag_start + 2);
            if (pi_end == std::string_view::npos) {
                idx = len;
                break;
            }
            idx = pi_end + 1;
            continue;
        }

        // Closing tag </tag>
        if (starts_with(xml_text.substr(tag_start), "</")) {
            size_t close_end = xml_text.find('>', tag_start + 2);
            if (close_end == std::string_view::npos) {
                err = "Malformed XML: unclosed closing tag";
                return nullptr;
            }
            if (node_stack.size() > 1) {
                node_stack.pop_back();
            }
            idx = close_end + 1;
            continue;
        }

        // Opening or self-closing tag <tag attr="val"...>
        size_t scan_pos = tag_start + 1;
        char in_quote = '\0';
        while (scan_pos < len) {
            char c = xml_text[scan_pos];
            if (in_quote != '\0') {
                if (c == in_quote) {
                    in_quote = '\0';
                }
            } else if (c == '"' || c == '\'') {
                in_quote = c;
            } else if (c == '>') {
                break;
            }
            scan_pos++;
        }
        if (scan_pos >= len || xml_text[scan_pos] != '>') {
            err = "Malformed XML: unclosed opening tag";
            return nullptr;
        }
        size_t tag_end = scan_pos;

        std::string_view tag_body = xml_text.substr(tag_start + 1, tag_end - tag_start - 1);
        bool is_self_closing = false;
        if (ends_with(tag_body, "/")) {
            is_self_closing = true;
            tag_body = tag_body.substr(0, tag_body.size() - 1);
        }

        auto new_node = std::make_shared<XmlNode>();
        parse_tag_body(tag_body, new_node->tag, new_node->attributes);

        if (!node_stack.empty()) {
            node_stack.back()->children.push_back(new_node);
        }

        if (!is_self_closing) {
            node_stack.push_back(new_node);
        }

        idx = tag_end + 1;
    }

    if (node_stack.size() > 1) {
        err = "Malformed XML: unclosed tag <" + node_stack.back()->tag + ">";
        return nullptr;
    }

    if (root->children.empty()) {
        err = "Empty XML document";
        return nullptr;
    }

    return root->children.size() == 1 ? root->children.front() : root;
}

}  // namespace fsm::frontend

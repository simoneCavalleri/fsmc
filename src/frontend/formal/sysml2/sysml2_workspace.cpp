#include "sysml2_workspace.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_set>

#include "fsm/ir/identifier_utils.hpp"

namespace fsm::frontend::formal::sysml2_detail {

using namespace fsm::ir;
namespace fs = std::filesystem;

namespace {

bool is_builtin_sysml_package(std::string_view pkg) {
    return pkg == "ScalarValues" || pkg == "SI" || pkg == "ISQ" || pkg == "Base" || pkg == "KerML" ||
           pkg == "Quantities" || pkg == "ScalarFunctions" || pkg == "Time";
}

std::string strip_line_comments(std::string_view src) {
    std::string out;
    out.reserve(src.size());
    bool in_block_comment = false;
    for (std::size_t i = 0; i < src.size(); ++i) {
        if (in_block_comment) {
            if (src[i] == '*' && i + 1 < src.size() && src[i + 1] == '/') {
                in_block_comment = false;
                ++i;
            }
            continue;
        }
        if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '*') {
            in_block_comment = true;
            ++i;
            continue;
        }
        if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            while (i < src.size() && src[i] != '\n') {
                ++i;
            }
            if (i < src.size()) {
                out.push_back('\n');
            }
            continue;
        }
        out.push_back(src[i]);
    }
    return out;
}

/// Strips `state def <Name> { ... }` blocks from imported library files so only shared types/ports/items/calcs remain.
std::string strip_state_defs(std::string_view src) {
    std::string out;
    out.reserve(src.size());
    static const std::regex sd_re(R"(\bstate\s+def\s+[A-Za-z_][A-Za-z0-9_]*\s*\{)");
    std::string text(src);
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::smatch m;
        auto sub_begin = text.cbegin() + static_cast<std::ptrdiff_t>(pos);
        if (!std::regex_search(sub_begin, text.cend(), m, sd_re)) {
            out.append(text, pos, text.size() - pos);
            break;
        }
        const std::size_t match_start = pos + static_cast<std::size_t>(m.position());
        out.append(text, pos, match_start - pos);
        std::size_t brace_pos = match_start + static_cast<std::size_t>(m.length()) - 1U;
        int depth = 1;
        std::size_t scan = brace_pos + 1U;
        while (scan < text.size() && depth > 0) {
            if (text[scan] == '{') {
                ++depth;
            } else if (text[scan] == '}') {
                --depth;
            }
            ++scan;
        }
        pos = scan;
    }
    return out;
}

std::string map_raw_type_to_cpp(std::string_view raw, const Sysml2SymbolResolver& resolver) {
    if (raw == "Boolean" || raw == "bool") {
        return "bool";
    }
    if (raw == "Real" || raw == "Float64" || raw == "double") {
        return "double";
    }
    if (raw == "Float32" || raw == "float") {
        return "float";
    }
    if (raw == "Integer" || raw == "Natural" || raw == "Positive" || raw == "UInt32" || raw == "uint32_t") {
        return "uint32_t";
    }
    if (raw == "Int32" || raw == "int32_t" || raw == "int") {
        return "int32_t";
    }
    if (raw == "Int64" || raw == "int64_t") {
        return "int64_t";
    }
    if (raw == "UInt64" || raw == "uint64_t") {
        return "uint64_t";
    }
    if (raw == "Int16" || raw == "int16_t") {
        return "int16_t";
    }
    if (raw == "UInt16" || raw == "uint16_t") {
        return "uint16_t";
    }
    if (raw == "Int8" || raw == "int8_t") {
        return "int8_t";
    }
    if (raw == "UInt8" || raw == "uint8_t") {
        return "uint8_t";
    }
    if (raw == "String" || raw == "string") {
        return "std::string";
    }
    const std::string resolved = resolver.resolve_type(raw);
    return resolved.empty() ? std::string(raw) : resolved;
}

bool find_package_file(const std::string& pkg_name, const std::vector<fs::path>& search_dirs, fs::path& out_path) {
    const std::regex pkg_re(R"(\bpackage\s+)" + pkg_name + R"(\b)");
    for (const auto& dir : search_dirs) {
        std::error_code ec;
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
            continue;
        }
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec) || entry.path().extension() != ".sysml") {
                continue;
            }
            std::ifstream ifs(entry.path());
            if (!ifs.is_open()) {
                continue;
            }
            std::string body((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            if (std::regex_search(body, pkg_re)) {
                out_path = fs::weakly_canonical(entry.path(), ec);
                if (ec) {
                    out_path = entry.path();
                }
                return true;
            }
        }
    }
    return false;
}

void collect_imports_recursive(std::string_view content, const std::vector<fs::path>& search_dirs,
                               std::unordered_set<std::string>& visited_files,
                               std::unordered_set<std::string>& loaded_packages,
                               std::ostringstream& imported_preamble) {
    static const std::regex import_re(
        R"(\b(?:public\s+|private\s+)?import\s+([A-Za-z_][A-Za-z0-9_]*)::(?:\*|[A-Za-z_][A-Za-z0-9_]*)\s*;)");
    std::string cleaned = strip_line_comments(content);
    for (std::sregex_iterator it(cleaned.begin(), cleaned.end(), import_re), end; it != end; ++it) {
        const std::string pkg = (*it)[1].str();
        if (is_builtin_sysml_package(pkg) || !loaded_packages.insert(pkg).second) {
            continue;
        }
        fs::path pkg_file;
        if (!find_package_file(pkg, search_dirs, pkg_file)) {
            continue;
        }
        const std::string canon = pkg_file.string();
        if (!visited_files.insert(canon).second) {
            continue;
        }
        std::ifstream ifs(pkg_file);
        if (!ifs.is_open()) {
            continue;
        }
        std::string file_text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        collect_imports_recursive(file_text, search_dirs, visited_files, loaded_packages, imported_preamble);
        imported_preamble << strip_state_defs(file_text) << "\n";
    }
}

}  // namespace

std::vector<std::string> scan_state_def_names(std::string_view content) {
    std::vector<std::string> names;
    std::unordered_set<std::string> seen;
    std::string cleaned = strip_line_comments(content);
    static const std::regex sd_re(R"(\bstate\s+def\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?::|\{))");
    for (std::sregex_iterator it(cleaned.begin(), cleaned.end(), sd_re), end; it != end; ++it) {
        const std::string n = sanitize_identifier((*it)[1].str());
        if (!n.empty() && seen.insert(n).second) {
            names.push_back(n);
        }
    }
    return names;
}

bool resolve_workspace_imports(std::string_view content, const Sysml2ParseOptions& opts, std::string& out_combined,
                               std::string& out_error) {
    (void)out_error;
    std::vector<fs::path> search_dirs;
    std::unordered_set<std::string> visited_files;

    if (!opts.source_file.empty()) {
        std::error_code ec;
        fs::path sf = fs::weakly_canonical(fs::path(opts.source_file), ec);
        if (ec) {
            sf = fs::path(opts.source_file);
        }
        visited_files.insert(sf.string());
        if (sf.has_parent_path()) {
            search_dirs.push_back(sf.parent_path());
        }
    }
    for (const auto& d : opts.import_dirs) {
        if (!d.empty()) {
            search_dirs.emplace_back(d);
        }
    }

    if (search_dirs.empty()) {
        out_combined.assign(content);
        return true;
    }

    std::unordered_set<std::string> loaded_packages;
    std::ostringstream preamble;
    collect_imports_recursive(content, search_dirs, visited_files, loaded_packages, preamble);

    std::string pre = preamble.str();
    if (pre.empty()) {
        out_combined.assign(content);
    } else {
        out_combined = pre + "\n" + std::string(content);
    }
    return true;
}

WorkspaceMetadata extract_port_defs_and_bindings(std::string_view combined_content) {
    WorkspaceMetadata meta;
    const std::string cleaned = strip_line_comments(combined_content);

    auto find_matching_brace = [&](std::size_t open_pos) -> std::size_t {
        int depth = 1;
        std::size_t i = open_pos + 1U;
        while (i < cleaned.size() && depth > 0) {
            if (cleaned[i] == '{') {
                ++depth;
            } else if (cleaned[i] == '}') {
                --depth;
            }
            ++i;
        }
        return (depth == 0) ? (i - 1U) : cleaned.size();
    };

    // 1. Parse all `port def <Name> { ... }`
    static const std::regex port_def_re(R"(\bport\s+def\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{)");
    static const std::regex item_feat_re(
        R"(\b(in|out|inout)\s+item\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z0-9_:]+)\s*;)");
    static const std::regex data_feat_re(
        R"(\b(in|out|inout)\s+(?:attribute\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z0-9_:]+)\s*;)");

    for (std::sregex_iterator it(cleaned.begin(), cleaned.end(), port_def_re), end; it != end; ++it) {
        const std::string pd_name = sanitize_identifier((*it)[1].str());
        const std::size_t open_pos = static_cast<std::size_t>(it->position() + it->length()) - 1U;
        const std::size_t close_pos = find_matching_brace(open_pos);
        const std::string body = cleaned.substr(open_pos + 1U, close_pos - open_pos - 1U);

        Sysml2PortDefSpec spec;
        spec.name = pd_name;
        for (std::sregex_iterator iit(body.begin(), body.end(), item_feat_re), iend; iit != iend; ++iit) {
            PortDefItemFeature feat;
            const std::string dir = (*iit)[1].str();
            feat.direction = (dir == "out")     ? PortDirection::Out
                             : (dir == "inout") ? PortDirection::InOut
                                                : PortDirection::In;
            feat.name = sanitize_identifier((*iit)[2].str());
            feat.signal_type = sanitize_identifier((*iit)[3].str());
            spec.item_features.push_back(std::move(feat));
        }
        for (std::sregex_iterator dit(body.begin(), body.end(), data_feat_re), dend; dit != dend; ++dit) {
            const std::string fname = sanitize_identifier((*dit)[2].str());
            if (fname == "item") {
                continue;
            }
            PortDefDataFeature feat;
            const std::string dir = (*dit)[1].str();
            feat.direction = (dir == "out")     ? PortDirection::Out
                             : (dir == "inout") ? PortDirection::InOut
                                                : PortDirection::In;
            feat.name = fname;
            feat.raw_type = (*dit)[3].str();
            spec.data_features.push_back(std::move(feat));
        }
        meta.port_defs[pd_name] = std::move(spec);
    }

    // 2. Scan `port <inst> : (~)?<PortDef>;` inside `part def` with `exhibit state <inst> : <StateDef>;`
    static const std::regex part_def_re(R"(\bpart\s+def\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{)");
    static const std::regex port_inst_re(
        R"(\bport\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(~)?\s*([A-Za-z_][A-Za-z0-9_]*)\s*;)");
    static const std::regex exhibit_re(
        R"(\bexhibit\s+state\s+[A-Za-z_][A-Za-z0-9_]*\s*:\s*([A-Za-z_][A-Za-z0-9_]*)\s*;)");

    for (std::sregex_iterator it(cleaned.begin(), cleaned.end(), part_def_re), end; it != end; ++it) {
        const std::size_t open_pos = static_cast<std::size_t>(it->position() + it->length()) - 1U;
        const std::size_t close_pos = find_matching_brace(open_pos);
        const std::string body = cleaned.substr(open_pos + 1U, close_pos - open_pos - 1U);

        std::vector<PortInstantiation> pinsts;
        for (std::sregex_iterator pit(body.begin(), body.end(), port_inst_re), pend; pit != pend; ++pit) {
            pinsts.push_back(PortInstantiation{sanitize_identifier((*pit)[1].str()),
                                               sanitize_identifier((*pit)[3].str()), (*pit)[2].matched});
        }
        if (pinsts.empty()) {
            continue;
        }
        for (std::sregex_iterator eit(body.begin(), body.end(), exhibit_re), eend; eit != eend; ++eit) {
            const std::string sd_name = sanitize_identifier((*eit)[1].str());
            auto& dst = meta.state_def_ports[sd_name];
            dst.insert(dst.end(), pinsts.begin(), pinsts.end());
        }
    }

    // 3. Also scan direct `port <inst> : (~)?<PortDef>;` inside `state def <Name> { ... }`
    static const std::regex state_def_re(R"(\bstate\s+def\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{)");
    for (std::sregex_iterator it(cleaned.begin(), cleaned.end(), state_def_re), end; it != end; ++it) {
        const std::string sd_name = sanitize_identifier((*it)[1].str());
        const std::size_t open_pos = static_cast<std::size_t>(it->position() + it->length()) - 1U;
        const std::size_t close_pos = find_matching_brace(open_pos);
        const std::string body = cleaned.substr(open_pos + 1U, close_pos - open_pos - 1U);

        for (std::sregex_iterator pit(body.begin(), body.end(), port_inst_re), pend; pit != pend; ++pit) {
            meta.state_def_ports[sd_name].push_back(PortInstantiation{
                sanitize_identifier((*pit)[1].str()), sanitize_identifier((*pit)[3].str()), (*pit)[2].matched});
        }
    }

    return meta;
}

void apply_port_bindings_to_model(const WorkspaceMetadata& meta, const std::string& state_def_name, FsmIr& model,
                                  const Sysml2SymbolResolver& resolver) {
    auto it = meta.state_def_ports.find(state_def_name);
    if (it == meta.state_def_ports.end()) {
        return;
    }
    for (const auto& inst : it->second) {
        auto pd_it = meta.port_defs.find(inst.port_def_name);
        if (pd_it == meta.port_defs.end()) {
            continue;
        }
        const auto& spec = pd_it->second;
        for (const auto& df : spec.data_features) {
            PortDirection dir = df.direction;
            if (inst.conjugated) {
                if (dir == PortDirection::In) {
                    dir = PortDirection::Out;
                } else if (dir == PortDirection::Out) {
                    dir = PortDirection::In;
                }
            }
            if (model.find_port(df.name) == nullptr) {
                PortDefinition p;
                p.name = df.name;
                p.type = DataType::from_string(map_raw_type_to_cpp(df.raw_type, resolver));
                p.direction = dir;
                p.description = "port:" + inst.instance_name + ":" + (inst.conjugated ? "~" : "") + spec.name;
                model.add_port(std::move(p));
            }
        }
        for (const auto& itf : spec.item_features) {
            if (!itf.signal_type.empty() && model.find_signal(itf.signal_type) == nullptr) {
                model.add_signal(SignalDefinition(itf.signal_type));
            }
        }
    }
}

}  // namespace fsm::frontend::formal::sysml2_detail

#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace fsm::tools {

namespace fs = std::filesystem;

inline bool ends_with(std::string_view str, std::string_view suffix) noexcept {
    return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline std::string read_file_content(const std::string& path, std::string& error_msg) {
    if (!fs::exists(path)) {
        error_msg = "File does not exist: " + path;
        return "";
    }
    std::ifstream stream(path);
    if (!stream.is_open()) {
        error_msg = "Could not open file for reading: " + path;
        return "";
    }
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

inline bool write_file_content(const std::string& path, std::string_view content, std::string& error_msg) {
    std::ofstream stream(path);
    if (!stream.is_open()) {
        error_msg = "Could not open file for writing: " + path;
        return false;
    }
    stream << content;
    return true;
}

inline std::string infer_fsm_name_from_file(const std::string& path) {
    fs::path p(path);
    std::string stem = p.stem().string();
    if (stem.empty()) {
        return "MyStateMachine";
    }
    // Capitalize first letter and sanitize
    std::string result;
    bool cap_next = true;
    for (char c : stem) {
        if (c == '_' || c == '-' || c == '.') {
            cap_next = true;
        } else if (cap_next) {
            result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            cap_next = false;
        } else {
            result += c;
        }
    }
    return result.empty() ? "MyStateMachine" : result;
}

}  // namespace fsm::tools

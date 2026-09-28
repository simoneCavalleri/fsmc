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

/**
 * @brief Validates a path string to ensure it contains no illegal characters or null bytes.
 */
inline bool is_valid_path_string(std::string_view path) noexcept {
    if (path.empty()) {
        return false;
    }
    for (char c : path) {
        if (c == '\0') {
            return false;
        }
    }
    return true;
}

/**
 * @brief Resolves a path for safe reading by ensuring the file exists, is a regular file,
 *        and normalizing/canonicalizing to eliminate directory traversal sequences.
 */
inline bool resolve_safe_read_path(const std::string& path, fs::path& safe_path, std::string& error_msg) {
    if (!is_valid_path_string(path)) {
        error_msg = "Invalid file path: path is empty or contains illegal characters";
        return false;
    }

    std::error_code ec;
    fs::path p(path);
    if (!fs::exists(p, ec)) {
        error_msg = "File does not exist: " + path;
        return false;
    }
    if (!fs::is_regular_file(p, ec)) {
        error_msg = "Path is not a regular file: " + path;
        return false;
    }

    // Resolve canonical representation to eliminate directory traversal (..)
    fs::path canonical_p = fs::canonical(p, ec);
    if (!ec && fs::is_regular_file(canonical_p, ec)) {
        safe_path = canonical_p;
    } else {
        safe_path = p.lexically_normal();
    }
    return true;
}

/**
 * @brief Resolves a path for safe writing by ensuring parent directories are valid/created
 *        and normalizing to eliminate directory traversal sequences.
 */
inline bool resolve_safe_write_path(const std::string& path, fs::path& safe_path, std::string& error_msg) {
    if (!is_valid_path_string(path)) {
        error_msg = "Invalid destination path: path is empty or contains illegal characters";
        return false;
    }

    std::error_code ec;
    fs::path p(path);
    if (fs::exists(p, ec) && fs::is_directory(p, ec)) {
        error_msg = "Destination path is an existing directory: " + path;
        return false;
    }

    fs::path parent = p.parent_path();
    if (!parent.empty()) {
        if (!fs::exists(parent, ec)) {
            fs::create_directories(parent, ec);
            if (ec) {
                error_msg = "Could not create destination directory '" + parent.string() + "': " + ec.message();
                return false;
            }
        }
        fs::path canonical_parent = fs::canonical(parent, ec);
        if (!ec) {
            safe_path = canonical_parent / p.filename();
            return true;
        }
    }

    safe_path = p.lexically_normal();
    return true;
}

/**
 * @brief Reads the complete text content of a file after validating the path against traversal.
 */
inline std::string read_file_content(const std::string& path, std::string& error_msg) {
    fs::path safe_path;
    if (!resolve_safe_read_path(path, safe_path, error_msg)) {
        return "";
    }

    std::ifstream stream(safe_path);
    if (!stream.is_open()) {
        error_msg = "Could not open file for reading: " + safe_path.string();
        return "";
    }
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

/**
 * @brief Writes text content to a destination file after validating and sanitizing the destination path.
 */
inline bool write_file_content(const std::string& path, std::string_view content, std::string& error_msg) {
    fs::path safe_path;
    if (!resolve_safe_write_path(path, safe_path, error_msg)) {
        return false;
    }

    std::ofstream stream(safe_path);
    if (!stream.is_open()) {
        error_msg = "Could not open file for writing: " + safe_path.string();
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

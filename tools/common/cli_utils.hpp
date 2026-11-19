#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "tools/common/file_utils.hpp"

namespace fsm::tools {

/**
 * @struct CommonCliOptions
 * @brief Shared CLI configuration fields common to both `fsmc` and `fsm-opt`.
 */
struct CommonCliOptions {
    std::string input_path;
    std::string output_path;
    std::string format_override = "auto";
    std::string submachine_dir;
    std::string diagnostic_format = "text";
    std::string pipe_through_cmd;
    std::vector<std::string> pass_plugins;
    bool warnings_as_errors = false;
    bool show_help = false;
    bool show_version = false;
    bool is_valid = true;
    std::string error_message;
};

/**
 * @brief Uniformly matches a CLI flag that accepts a value via `-f <val>`, `--flag <val>`, or `--flag=<val>`.
 *
 * @param arg Current argument string view (`argv[i]`)
 * @param i Current argument index (incremented if a separate token is consumed)
 * @param argc Total argument count
 * @param argv Argument string vector
 * @param short_flag Optional short flag (e.g. "-i"), or empty if none
 * @param long_flag Long flag (e.g. "--input")
 * @param out_val Output string receiving the matched option value
 * @param out_err Output error message if a required value argument is missing
 * @return True if `arg` matched the flag (check `out_err.empty()` for validity), false otherwise
 */
inline bool match_opt_value(std::string_view arg, int& i, int argc, char* argv[], std::string_view short_flag,
                            std::string_view long_flag, std::string& out_val, std::string& out_err) {
    if ((!short_flag.empty() && arg == short_flag) || (!long_flag.empty() && arg == long_flag)) {
        if (i + 1 >= argc) {
            out_err = "Missing argument for option: " + std::string(arg);
            return true;
        }
        out_val = argv[++i];
        return true;
    }
    if (!long_flag.empty() && arg.size() > long_flag.size() && arg.starts_with(long_flag) &&
        arg[long_flag.size()] == '=') {
        out_val = std::string(arg.substr(long_flag.size() + 1));
        return true;
    }
    if (!short_flag.empty() && arg.size() > short_flag.size() && arg.starts_with(short_flag) &&
        arg[short_flag.size()] == '=') {
        out_val = std::string(arg.substr(short_flag.size() + 1));
        return true;
    }
    return false;
}

/**
 * @brief Parses compiler CLI flags shared between `fsmc` and `fsm-opt`.
 *
 * Handles `-i/--input`, `-o/--output`, `--format`, `--submachine-dir`,
 * `--diagnostic-format`, `--pipe-through`, `--load-pass-plugin`, and `-Werror`.
 *
 * @return True if `arg` was recognized and handled (check `error_message.empty()` for validity), false otherwise.
 */
inline bool parse_common_compiler_arg(std::string_view arg, int& i, int argc, char* argv[], std::string& input_path,
                                      std::string& output_path, std::string& format_override,
                                      std::string& submachine_dir, std::string& diagnostic_format,
                                      std::string& pipe_through_cmd, std::vector<std::string>& pass_plugins,
                                      bool& warnings_as_errors, std::string& error_message) {
    if (match_opt_value(arg, i, argc, argv, "-i", "--input", input_path, error_message)) {
        return true;
    }
    if (match_opt_value(arg, i, argc, argv, "-o", "--output", output_path, error_message)) {
        return true;
    }
    if (match_opt_value(arg, i, argc, argv, "", "--format", format_override, error_message)) {
        return true;
    }
    if (match_opt_value(arg, i, argc, argv, "", "--submachine-dir", submachine_dir, error_message)) {
        return true;
    }
    if (match_opt_value(arg, i, argc, argv, "", "--diagnostic-format", diagnostic_format, error_message)) {
        if (error_message.empty() && !is_valid_diagnostic_format(diagnostic_format)) {
            error_message =
                "Invalid diagnostic format '" + diagnostic_format + "'. Supported formats: text, json, github.";
        }
        return true;
    }
    if (match_opt_value(arg, i, argc, argv, "", "--pipe-through", pipe_through_cmd, error_message)) {
        return true;
    }
    std::string plugin_val;
    if (match_opt_value(arg, i, argc, argv, "", "--load-pass-plugin", plugin_val, error_message)) {
        if (error_message.empty()) {
            pass_plugins.push_back(std::move(plugin_val));
        }
        return true;
    }
    if (arg == "-Werror") {
        warnings_as_errors = true;
        return true;
    }
    return false;
}

inline bool parse_common_compiler_arg(std::string_view arg, int& i, int argc, char* argv[], CommonCliOptions& opts) {
    return parse_common_compiler_arg(arg, i, argc, argv, opts.input_path, opts.output_path, opts.format_override,
                                     opts.submachine_dir, opts.diagnostic_format, opts.pipe_through_cmd,
                                     opts.pass_plugins, opts.warnings_as_errors, opts.error_message);
}

}  // namespace fsm::tools

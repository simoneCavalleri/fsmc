/**
 * @file runtime_exporter.hpp
 * @brief Exporter utility bundling standalone header-only C++ runtime files.
 */

#pragma once

#include <string>

#include "fsm/backend/cpp/cpp_options.hpp"

namespace fsm::backend::cpp {

/**
 * @class RuntimeExporter
 * @brief Bundles and exports the embedded runtime headers for standalone distribution.
 */
class RuntimeExporter {
  public:
    /**
     * @brief Writes out the unified single-header C++17+ runtime to the specified path.
     * @param output_file_path Target file path or directory for the standalone runtime.
     * @param out_error Diagnostic error message if export fails.
     * @return True on success, false otherwise.
     */
    static bool export_runtime(const std::string& output_file_path, std::string& out_error);
};

}  // namespace fsm::backend::cpp

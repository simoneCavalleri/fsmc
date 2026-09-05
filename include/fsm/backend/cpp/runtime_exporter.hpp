#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "fsm/backend/cpp/cpp_generator.hpp"

namespace fs = std::filesystem;

namespace fsm::backend::cpp {

class RuntimeExporter {
  public:
    static bool export_runtime(const std::string& output_file_path, CppStandard standard, std::string& out_error) {
        try {
            const fs::path out_path(output_file_path);
            const fs::path parent_dir = out_path.parent_path();

            // Attempt to create parent directory chain. Use std::error_code to avoid
            // silent failures on Windows where some paths don't throw but silently no-op.
            if (!parent_dir.empty()) {
                std::error_code ec;
                fs::create_directories(parent_dir, ec);
                if (ec) {
                    out_error = "Could not create output directory '" + parent_dir.string() + "': " + ec.message();
                    return false;
                }
            }

            std::ofstream out(out_path);
            if (!out.is_open()) {
                out_error = "Could not create file: " + out_path.string();
                return false;
            }

            FsmIr empty_model;
            empty_model.name = "";
            empty_model.package = "";

            GeneratorOptions opts;
            opts.target_namespace = "";
            opts.cpp_standard = standard;

            opts.standalone = true;
            opts.thread_safe = true;
            opts.include_stubs = false;

            const std::string runtime_code = CppGenerator::generate_header(empty_model, opts);
            out << runtime_code;

            std::cout << "[SUCCESS] FSM Runtime (" << (standard == CppStandard::Cpp20 ? "Native C++20" : "C++17")
                      << ") exported successfully to: " << out_path.string() << "\n";
            return true;
        } catch (const std::exception& ex) {
            out_error = ex.what();
            return false;
        }
    }
};

}  // namespace fsm::backend::cpp

namespace fsm::backend {
using cpp::RuntimeExporter;
}  // namespace fsm::backend

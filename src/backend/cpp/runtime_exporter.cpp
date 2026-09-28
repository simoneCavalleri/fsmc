#include "fsm/backend/cpp/runtime_exporter.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

#include "fsm/backend/cpp/cpp_generator.hpp"

namespace fs = std::filesystem;

namespace fsm::backend::cpp {

bool RuntimeExporter::export_runtime(const std::string& output_file_path, CppStandard standard,
                                     std::string& out_error) {
    try {
        if (output_file_path.empty() || output_file_path.find('\0') != std::string::npos) {
            out_error = "Invalid destination file path: path is empty or contains illegal characters";
            return false;
        }

        const fs::path out_path(output_file_path);
        const fs::path parent_dir = out_path.parent_path();

        // Attempt to create parent directory chain. Use std::error_code to avoid
        // silent failures on Windows where some paths don't throw but silently no-op.
        std::error_code ec;
        if (!parent_dir.empty()) {
            if (!fs::exists(parent_dir, ec)) {
                fs::create_directories(parent_dir, ec);
                if (ec) {
                    out_error = "Could not create output directory '" + parent_dir.string() + "': " + ec.message();
                    return false;
                }
            }
        }

        fs::path safe_out_path;
        if (!parent_dir.empty()) {
            fs::path canonical_parent = fs::canonical(parent_dir, ec);
            safe_out_path = (!ec) ? (canonical_parent / out_path.filename()) : out_path.lexically_normal();
        } else {
            safe_out_path = out_path.lexically_normal();
        }

        std::ofstream out(safe_out_path);
        if (!out.is_open()) {
            out_error = "Could not create file: " + safe_out_path.string();
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

}  // namespace fsm::backend::cpp

#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "cpp_generator.hpp"

namespace fs = std::filesystem;

namespace fsm::codegen {

class RuntimeExporter {
  public:
    static bool export_runtime(const std::string& target_dir, CppStandard standard, std::string& out_error) {
        try {
            fs::create_directories(target_dir);

            const std::string header_path = (fs::path(target_dir) / "fsm.hpp").string();
            std::ofstream out(header_path);
            if (!out.is_open()) {
                out_error = "Could not create file: " + header_path;
                return false;
            }

            FsmModel empty_model;
            empty_model.name = "";
            empty_model.ns = "";

            GeneratorOptions opts;
            opts.cpp_standard = standard;
            opts.standalone = true;
            opts.thread_safe = true;
            opts.include_stubs = false;

            const std::string runtime_code = CppGenerator::generate_header(empty_model, opts);
            out << runtime_code;

            std::cout << "[SUCCESS] FSM Runtime (" << (standard == CppStandard::Cpp20 ? "Native C++20" : "C++17")
                      << ") exported successfully to: " << header_path << "\n";
            return true;
        } catch (const std::exception& ex) {
            out_error = ex.what();
            return false;
        }
    }
};

}  // namespace fsm::codegen

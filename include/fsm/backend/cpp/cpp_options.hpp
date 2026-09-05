#pragma once

#include <cstdint>
#include <string>

namespace fsm::backend::cpp {

enum class CppStandard : std::uint8_t { Cpp17, Cpp20 };

struct GeneratorOptions {
    CppStandard cpp_standard = CppStandard::Cpp17;
    bool standalone = true;     // Generate completely self-contained file (no external includes needed)
    bool include_stubs = true;  // Generate default functor definitions for guards and actions
    bool thread_safe = true;    // Generate thread_safe_fsm wrapper
    std::string target_namespace = "";  // Explicit target namespace (overrides model.package if non-empty)
};

}  // namespace fsm::backend::cpp

namespace fsm::backend {
using cpp::CppStandard;
using cpp::GeneratorOptions;
}  // namespace fsm::backend

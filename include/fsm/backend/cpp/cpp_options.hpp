/**
 * @file cpp_options.hpp
 * @brief Configuration flags and target dialect options for the C++ code generator.
 */

#pragma once

#include <cstdint>
#include <string>

namespace fsm::backend::cpp {

/**
 * @struct GeneratorOptions
 * @brief User configuration options governing unified C++17 code emission.
 */
struct GeneratorOptions {
    bool standalone = true;                  ///< Generate self-contained header embedding minimal runtime
    bool include_stubs = true;               ///< Generate default functor stub implementations for guards and actions
    bool thread_safe = true;                 ///< Generate thread_safe_fsm synchronized wrapper
    bool freestanding = false;               ///< Emit zero-STL freestanding header (-fno-exceptions -fno-rtti safe)
    std::string target_namespace = "";       ///< Explicit enclosing namespace override
    std::string runtime_header = "fsm.hpp";  ///< External runtime header included when standalone is false
};

}  // namespace fsm::backend::cpp

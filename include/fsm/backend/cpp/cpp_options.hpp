/**
 * @file cpp_options.hpp
 * @brief Configuration flags and target dialect options for the C++ code generator.
 */

#pragma once

#include <cstdint>
#include <string>

namespace fsm::backend::cpp {

/**
 * @enum CppStandard
 * @brief ISO C++ language dialect target.
 */
enum class CppStandard : std::uint8_t {
    Cpp17,  ///< ISO C++17
    Cpp20   ///< ISO C++20 (enables concepts and designated initializers)
};

/**
 * @struct GeneratorOptions
 * @brief User configuration options governing C++ code emission.
 */
struct GeneratorOptions {
    CppStandard cpp_standard = CppStandard::Cpp17;  ///< Target language dialect
    bool standalone = true;                         ///< Generate self-contained header embedding minimal runtime
    bool include_stubs = true;          ///< Generate default functor stub implementations for guards and actions
    bool thread_safe = true;            ///< Generate thread_safe_fsm synchronized wrapper
    std::string target_namespace = "";  ///< Explicit enclosing namespace override
};

}  // namespace fsm::backend::cpp

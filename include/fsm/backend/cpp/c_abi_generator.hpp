/**
 * @file c_abi_generator.hpp
 * @brief Zero-allocation pure C99 ABI wrapper generator (*_c.h + *_c.cpp) over the unified C++17 FSM backend.
 */

#pragma once

#include <string>
#include <string_view>

#include "fsm/backend/cpp/cpp_options.hpp"
#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::cpp {

using ir::FsmIr;

/**
 * @struct GeneratedCAbiBundle
 * @brief Pair of generated C ABI files: a pure C99 header (*_c.h) and a zero-alloc C++17 bridge (*_c.cpp).
 */
struct GeneratedCAbiBundle {
    std::string header;  ///< Pure C99 / extern "C" header (*_c.h) with static opaque aligned storage
    std::string source;  ///< Zero-allocation C++17 bridge (*_c.cpp) using placement-new and static_assert
};

/**
 * @class CAbiGenerator
 * @brief Synthesizes a zero-heap, C99-compliant extern "C" FFI layer for C, Rust, Python, and legacy C++ callers.
 */
class CAbiGenerator {
  public:
    /**
     * @brief Generates the C ABI header and bridge source for a canonical FsmIr model.
     * @param model Canonical FsmIr model.
     * @param options Target C++ generator options (namespace, etc.).
     * @param fsm_header_include Include path for the C++ FSM header (e.g. "motor_fsm.hpp").
     * @param c_header_include Include path for the C ABI header itself (e.g. "motor_c.h").
     * @return Pair of generated C header and C++17 bridge source strings.
     */
    static GeneratedCAbiBundle generate(const FsmIr& model, const GeneratorOptions& options = {},
                                        std::string_view fsm_header_include = "",
                                        std::string_view c_header_include = "");
};

}  // namespace fsm::backend::cpp

/**
 * @file plugin_loader.hpp
 * @brief Dynamic shared library plugin loader for middle-end compiler passes.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"

namespace fsm::middleend {
class PassManager;
}

namespace fsm::middleend::plugin {

using diagnostic::DiagnosticEngine;

/// Current C++ plugin ABI version for same-toolchain dynamic pass plugins.
inline constexpr std::uint32_t kPluginAbiVersion = 1U;

/// Compile-time toolchain & standard library fingerprint for ABI compatibility verification.
inline constexpr const char* kPluginAbiFingerprint =
#if defined(__clang__)
    "clang"
#elif defined(__GNUC__)
    "gcc"
#elif defined(_MSC_VER)
    "msvc"
#else
    "unknown_cc"
#endif
    "-"
#if defined(_LIBCPP_VERSION)
    "libcxx"
#elif defined(__GLIBCXX__)
    "libstdcxx"
#elif defined(_MSVC_STL_VERSION)
    "msvcstl"
#else
    "unknown_stl"
#endif
    "-"
#if INTPTR_MAX == INT64_MAX
    "64";
#else
    "32";
#endif

#if defined(_WIN32)
#define FSMC_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FSMC_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/// Convenience macro for plugins to export the required ABI version and toolchain fingerprint symbols.
#define FSMC_DEFINE_PLUGIN_ABI()                                   \
    extern "C" {                                                   \
    FSMC_PLUGIN_EXPORT std::uint32_t fsmc_plugin_abi_version() {   \
        return ::fsm::middleend::plugin::kPluginAbiVersion;        \
    }                                                              \
    FSMC_PLUGIN_EXPORT const char* fsmc_plugin_abi_fingerprint() { \
        return ::fsm::middleend::plugin::kPluginAbiFingerprint;    \
    }                                                              \
    }

/**
 * @class PluginLoader
 * @brief Dynamic Shared Library Plugin Loader for Middle-End Compiler Passes.
 *
 * Uses dlopen/dlsym (POSIX) or LoadLibraryA/GetProcAddress (Windows) to load shared libraries
 * (.so/.so.N/.dylib/.dll) exposing:
 *   - `extern "C" uint32_t fsmc_plugin_abi_version()`
 *   - `extern "C" const char* fsmc_plugin_abi_fingerprint()` (optional, checked if exported)
 *   - `extern "C" void fsmc_register_passes(PassManager&)`
 */
class PluginLoader {
  public:
    using AbiVersionFn = std::uint32_t (*)();
    using AbiFingerprintFn = const char* (*)();
    using RegisterPassesFn = void (*)(PassManager&);

    PluginLoader() = default;
    ~PluginLoader() { unload_all(); }

    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;
    PluginLoader(PluginLoader&& other) noexcept : handles_(std::move(other.handles_)) {}
    PluginLoader& operator=(PluginLoader&& other) noexcept {
        if (this != &other) {
            unload_all();
            handles_ = std::move(other.handles_);
        }
        return *this;
    }

    /**
     * @brief Loads a shared library plugin and invokes its pass registration hook.
     * @param plugin_path Path to the compiled plugin (.so or .dylib).
     * @param pm Target PassManager to register passes with.
     * @param diag Diagnostic engine for error reporting.
     * @return True if plugin loaded and registered successfully, false otherwise.
     */
    bool load_plugin(const std::string& plugin_path, PassManager& pm, DiagnosticEngine& diag);

    /**
     * @brief Closes all open dynamic library handles.
     */
    void unload_all() noexcept;

  private:
    std::vector<void*> handles_;
};

}  // namespace fsm::middleend::plugin

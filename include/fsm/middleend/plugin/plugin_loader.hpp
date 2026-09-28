/**
 * @file plugin_loader.hpp
 * @brief Dynamic shared library plugin loader for middle-end compiler passes.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"

namespace fsm::middleend {
class PassManager;
}

namespace fsm::middleend::plugin {

using diagnostic::DiagnosticEngine;

/**
 * @class PluginLoader
 * @brief Dynamic Shared Library Plugin Loader for Middle-End Compiler Passes.
 *
 * Uses dlopen/dlsym on POSIX platforms to load shared libraries (.so/.dylib)
 * exposing the `fsmc_register_passes(PassManager&)` entry point.
 */
class PluginLoader {
  public:
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

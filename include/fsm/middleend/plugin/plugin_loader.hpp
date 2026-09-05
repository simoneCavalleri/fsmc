#pragma once

#include <dlfcn.h>

#include <memory>
#include <string>
#include <vector>

#include "fsm/diagnostic/diagnostic_engine.hpp"

namespace fsm::middleend {
class PassManager;
}

namespace fsm::middleend::plugin {

/**
 * @brief Dynamic Shared Library Plugin Loader for Middle-End Compiler Passes.
 *
 * Loads external C/C++ shared objects (.so / .dylib) at runtime using dlopen/dlsym,
 * executing the exported registration entry point:
 * `extern "C" void fsmc_register_passes(fsm::middleend::PassManager& pm)`
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

    bool load_plugin(const std::string& plugin_path, PassManager& pm, DiagnosticEngine& diag) {
        dlerror();  // Clear existing error
        void* handle = dlopen(plugin_path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr) {
            const char* err = dlerror();
            std::string err_str = (err != nullptr) ? err : "unknown dlopen error";
            diag.report(
                Diagnostic::error("E_PLUGIN_LOAD", "Failed to load pass plugin '" + plugin_path + "': " + err_str));
            return false;
        }

        dlerror();  // Clear existing error
        auto* reg_sym = dlsym(handle, "fsmc_register_passes");
        const char* dlsym_err = dlerror();
        if (dlsym_err != nullptr || reg_sym == nullptr) {
            diag.report(Diagnostic::error(
                "E_PLUGIN_SYMBOL",
                "Plugin '" + plugin_path +
                    "' does not export required entry symbol 'extern \"C\" void fsmc_register_passes(PassManager&)': " +
                    (dlsym_err != nullptr ? dlsym_err : "symbol not found")));
            dlclose(handle);
            return false;
        }

        auto reg_fn = reinterpret_cast<RegisterPassesFn>(reg_sym);
        reg_fn(pm);

        handles_.push_back(handle);
        diag.report(Diagnostic::info("I_PLUGIN_REGISTERED",
                                     "Successfully loaded and registered passes from plugin: " + plugin_path));
        return true;
    }

    void unload_all() noexcept {
        for (void* h : handles_) {
            if (h != nullptr) {
                dlclose(h);
            }
        }
        handles_.clear();
    }

  private:
    std::vector<void*> handles_;
};

}  // namespace fsm::middleend::plugin

namespace fsm::middleend {
using plugin::PluginLoader;
}  // namespace fsm::middleend

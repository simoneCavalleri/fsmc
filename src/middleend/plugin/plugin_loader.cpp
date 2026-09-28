#include "fsm/middleend/plugin/plugin_loader.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <filesystem>

#include "fsm/middleend/pass_manager.hpp"

namespace fs = std::filesystem;

namespace fsm::middleend::plugin {

bool PluginLoader::load_plugin(const std::string& plugin_path, PassManager& pm, DiagnosticEngine& diag) {
    if (plugin_path.empty() || plugin_path.find('\0') != std::string::npos) {
        diag.report(Diagnostic::error("E_PLUGIN_PATH_INVALID", "Plugin path is empty or contains invalid characters"));
        return false;
    }

    std::error_code ec;
    fs::path p(plugin_path);
    if (!fs::exists(p, ec) || !fs::is_regular_file(p, ec)) {
        diag.report(Diagnostic::error("E_PLUGIN_LOAD",
                                      "Plugin file does not exist or is not a regular file: '" + plugin_path + "'"));
        return false;
    }

    // Verify valid shared library file extension to prevent loading unauthorized file types
    std::string ext = p.extension().string();
    if (ext != ".so" && ext != ".dylib" && ext != ".dll") {
        diag.report(Diagnostic::error(
            "E_PLUGIN_LOAD",
            "Plugin '" + plugin_path + "' must have a valid shared library extension (.so, .dylib, .dll)"));
        return false;
    }

    // Resolve canonical representation to eliminate directory traversal sequences (..)
    fs::path canonical_path = fs::canonical(p, ec);
    if (ec || !fs::is_regular_file(canonical_path, ec)) {
        diag.report(
            Diagnostic::error("E_PLUGIN_LOAD", "Failed to resolve canonical path for plugin: '" + plugin_path + "'"));
        return false;
    }
    const std::string safe_path = canonical_path.string();

#if defined(_WIN32)
    HMODULE handle = LoadLibraryA(safe_path.c_str());
    if (handle == nullptr) {
        DWORD err = GetLastError();
        diag.report(Diagnostic::error(
            "E_PLUGIN_LOAD", "Failed to load pass plugin '" + safe_path + "': error code " + std::to_string(err)));
        return false;
    }

    FARPROC reg_sym = GetProcAddress(handle, "fsmc_register_passes");
    if (reg_sym == nullptr) {
        diag.report(
            Diagnostic::error("E_PLUGIN_SYMBOL", "Plugin '" + safe_path +
                                                     "' does not export required entry symbol 'extern \"C\" void "
                                                     "fsmc_register_passes(PassManager&)': symbol not found"));
        FreeLibrary(handle);
        return false;
    }

    auto reg_fn = reinterpret_cast<RegisterPassesFn>(reg_sym);
    reg_fn(pm);

    handles_.push_back(reinterpret_cast<void*>(handle));
    diag.report(
        Diagnostic::info("I_PLUGIN_REGISTERED", "Successfully loaded and registered passes from plugin: " + safe_path));
    return true;
#else
    dlerror();  // Clear existing error
    void* handle = dlopen(safe_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* err = dlerror();
        std::string err_str = (err != nullptr) ? err : "unknown dlopen error";
        diag.report(Diagnostic::error("E_PLUGIN_LOAD", "Failed to load pass plugin '" + safe_path + "': " + err_str));
        return false;
    }

    dlerror();  // Clear existing error
    auto* reg_sym = dlsym(handle, "fsmc_register_passes");
    const char* dlsym_err = dlerror();
    if (dlsym_err != nullptr || reg_sym == nullptr) {
        diag.report(Diagnostic::error(
            "E_PLUGIN_SYMBOL",
            "Plugin '" + safe_path +
                "' does not export required entry symbol 'extern \"C\" void fsmc_register_passes(PassManager&)': " +
                (dlsym_err != nullptr ? dlsym_err : "symbol not found")));
        dlclose(handle);
        return false;
    }

    auto reg_fn = reinterpret_cast<RegisterPassesFn>(reg_sym);
    reg_fn(pm);

    handles_.push_back(handle);
    diag.report(
        Diagnostic::info("I_PLUGIN_REGISTERED", "Successfully loaded and registered passes from plugin: " + safe_path));
    return true;
#endif
}

void PluginLoader::unload_all() noexcept {
    for (void* h : handles_) {
        if (h != nullptr) {
#if defined(_WIN32)
            FreeLibrary(reinterpret_cast<HMODULE>(h));
#else
            dlclose(h);
#endif
        }
    }
    handles_.clear();
}

}  // namespace fsm::middleend::plugin

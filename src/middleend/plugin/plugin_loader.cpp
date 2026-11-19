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

#include <cctype>
#include <cstring>
#include <exception>
#include <filesystem>

#include "fsm/middleend/pass_manager.hpp"

namespace fs = std::filesystem;

namespace fsm::middleend::plugin {

using diagnostic::Diagnostic;

namespace {

bool is_valid_shared_lib_filename(const fs::path& p) {
    const std::string ext = p.extension().string();
    if (ext == ".so" || ext == ".dylib" || ext == ".dll") {
        return true;
    }
    // Support versioned POSIX shared libraries such as libfoo.so.1 or libfoo.so.1.2
    const std::string fname = p.filename().string();
    const std::size_t so_pos = fname.find(".so.");
    if (so_pos == std::string::npos || so_pos + 4 >= fname.size()) {
        return false;
    }
    for (std::size_t i = so_pos + 4; i < fname.size(); ++i) {
        const char ch = fname[i];
        if (!std::isdigit(static_cast<unsigned char>(ch)) && ch != '.') {
            return false;
        }
    }
    return true;
}

bool verify_abi_symbols(PluginLoader::AbiVersionFn abi_ver_fn, PluginLoader::AbiFingerprintFn abi_fp_fn,
                        const std::string& safe_path, DiagnosticEngine& diag) {
    if (abi_ver_fn == nullptr) {
        diag.report(Diagnostic::error("E_PLUGIN_ABI",
                                      "Plugin '" + safe_path +
                                          "' does not export required ABI symbol 'extern \"C\" uint32_t "
                                          "fsmc_plugin_abi_version()' (use FSMC_DEFINE_PLUGIN_ABI())"));
        return false;
    }
    const std::uint32_t plugin_ver = abi_ver_fn();
    if (plugin_ver != kPluginAbiVersion) {
        diag.report(Diagnostic::error("E_PLUGIN_ABI", "Plugin '" + safe_path + "' ABI version mismatch: expected " +
                                                          std::to_string(kPluginAbiVersion) + ", got " +
                                                          std::to_string(plugin_ver)));
        return false;
    }
    if (abi_fp_fn != nullptr) {
        const char* plugin_fp = abi_fp_fn();
        if (plugin_fp == nullptr || std::strcmp(plugin_fp, kPluginAbiFingerprint) != 0) {
            diag.report(Diagnostic::error("E_PLUGIN_ABI",
                                          "Plugin '" + safe_path + "' toolchain ABI fingerprint mismatch: expected '" +
                                              std::string(kPluginAbiFingerprint) + "', got '" +
                                              std::string(plugin_fp != nullptr ? plugin_fp : "<null>") + "'"));
            return false;
        }
    }
    return true;
}

}  // namespace

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
    if (!is_valid_shared_lib_filename(p)) {
        diag.report(Diagnostic::error(
            "E_PLUGIN_LOAD",
            "Plugin '" + plugin_path + "' must have a valid shared library extension (.so, .so.N, .dylib, .dll)"));
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

    auto abi_ver_fn = reinterpret_cast<AbiVersionFn>(GetProcAddress(handle, "fsmc_plugin_abi_version"));
    auto abi_fp_fn = reinterpret_cast<AbiFingerprintFn>(GetProcAddress(handle, "fsmc_plugin_abi_fingerprint"));
    if (!verify_abi_symbols(abi_ver_fn, abi_fp_fn, safe_path, diag)) {
        FreeLibrary(handle);
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
    const std::size_t prev_pass_count = pm.pass_count();
    try {
        reg_fn(pm);
    } catch (const std::exception& ex) {
        pm.truncate_passes(prev_pass_count);
        diag.report(
            Diagnostic::error("E_PLUGIN_INIT", "Plugin '" + safe_path + "' threw during registration: " + ex.what()));
        FreeLibrary(handle);
        return false;
    } catch (...) {
        pm.truncate_passes(prev_pass_count);
        diag.report(Diagnostic::error("E_PLUGIN_INIT",
                                      "Plugin '" + safe_path + "' threw an unknown exception during registration"));
        FreeLibrary(handle);
        return false;
    }

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

    dlerror();
    auto abi_ver_fn = reinterpret_cast<AbiVersionFn>(dlsym(handle, "fsmc_plugin_abi_version"));
    auto abi_fp_fn = reinterpret_cast<AbiFingerprintFn>(dlsym(handle, "fsmc_plugin_abi_fingerprint"));
    if (!verify_abi_symbols(abi_ver_fn, abi_fp_fn, safe_path, diag)) {
        dlclose(handle);
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
    const std::size_t prev_pass_count = pm.pass_count();
    try {
        reg_fn(pm);
    } catch (const std::exception& ex) {
        pm.truncate_passes(prev_pass_count);
        diag.report(
            Diagnostic::error("E_PLUGIN_INIT", "Plugin '" + safe_path + "' threw during registration: " + ex.what()));
        dlclose(handle);
        return false;
    } catch (...) {
        pm.truncate_passes(prev_pass_count);
        diag.report(Diagnostic::error("E_PLUGIN_INIT",
                                      "Plugin '" + safe_path + "' threw an unknown exception during registration"));
        dlclose(handle);
        return false;
    }

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

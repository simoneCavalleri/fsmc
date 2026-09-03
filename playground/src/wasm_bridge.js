/**
 * fsmc Playground — WebAssembly Bridge
 *
 * fsmc.js is loaded as a classic <script> before this ESM module,
 * which registers `createFsmcModule` on globalThis (IIFE Emscripten pattern).
 * This module wraps that global into a lazy-initialised singleton promise.
 */

let _module = null;
let _initPromise = null;

/**
 * Lazily initialises the fsmc WASM module and returns it.
 * Safe to call multiple times — returns the same Promise.
 * @returns {Promise<object|null>}
 */
export function initWasm() {
  if (!_initPromise) {
    _initPromise = (async () => {
      const createFn = globalThis.createFsmcModule;
      if (typeof createFn === "function") {
        try {
          _module = await createFn();
          globalThis.fsmcModule = _module;
          return _module;
        } catch (err) {
          console.warn("fsmc WASM init notice:", err);
        }
      } else if (globalThis.Module) {
        const M = globalThis.Module;
        if (M._malloc || M.compile) {
          _module = M;
          return _module;
        }
        return new Promise(resolve => {
          M.onRuntimeInitialized = () => {
            _module = M;
            globalThis.fsmcModule = M;
            resolve(_module);
          };
        });
      }
      return null;
    })();
  }
  return _initPromise;
}

/**
 * Returns the loaded WASM module, or null if not yet initialised.
 * Always prefer awaiting initWasm() for reliable access.
 * @returns {object|null}
 */
export function getModule() {
  return _module
    ?? globalThis.fsmcModule
    ?? null;
}

// Start loading immediately in the background.
initWasm();

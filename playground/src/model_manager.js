/**
 * fsmc Playground — Model Manager
 * Orchestrates parse, serialize, export, optimize, generateCpp, and validate.
 * Delegates format-specific work to parsers/ and serializers/ modules.
 * Maintains the single currentModel instance shared across the application.
 */

import { initWasm, getModule } from './wasm_bridge.js';
import { detectFormat, fallbackParse } from './parsers/index.js';
import { serialize }  from './serializers/index.js';
import { generateCpp as jsCppGen } from './cpp_generator.js';

export const ModelManager = {
  currentModel: {
    name: "GeneratedFSM",
    states: [],
    stateDetails: [],
    events: [],
    transitions: [],
    initialState: "Disconnected",
    activeState: "Disconnected",
    zoom: 1.0,
    panX: 0,
    panY: 0
  },

  detectFormat(text) {
    return detectFormat(text);
  },

  setModule(m) {
    // Legacy shim: prefer getModule() from wasm_bridge
    globalThis.fsmcModule = m;
  },

  async parse(text, format) {
    const fmt = format || detectFormat(text);
    const mod = getModule();

    if (mod && mod.getModel && text && text.trim()) {
      try {
        const res = JSON.parse(mod.getModel(text, fmt));
        if (res && !res.error && res.states && res.states.length > 0) {
          const statesArr = res.states;
          for (const s of statesArr) {
            s.is_composite = statesArr.some(child => child.parent === s.name);
          }
          return {
            name: res.name || "GeneratedFSM",
            states: statesArr.map(s => s.name),
            stateDetails: statesArr,
            events: res.events || [],
            transitions: res.transitions || [],
            initialState: res.initialState || statesArr[0].name,
            ports: res.ports || [],
            variables: res.variables || []
          };
        }
      } catch (e) {
        console.warn("WASM getModel notice:", e);
      }
    }

    return fallbackParse(text, fmt);
  },

  serialize(model, toFormat) {
    return serialize(model, toFormat);
  },

  export(source, fromFormat, toFormat) {
    if (fromFormat === toFormat) return source;
    const mod = getModule();
    if (mod && mod.exportDiagram && source && source.trim()) {
      try {
        const exported = mod.exportDiagram(source, fromFormat, toFormat);
        if (exported && !exported.startsWith("// [FSMC ERROR]")) return exported;
      } catch (e) {
        console.warn("WASM exportDiagram notice:", e);
      }
    }
    return this.parse(source, fromFormat).then(model => serialize(model, toFormat));
  },

  optimize(source, format, outFormat = "") {
    const mod = getModule();
    if (mod && mod.optimize && source && source.trim()) {
      try {
        const opt = mod.optimize(source, format, outFormat || format);
        if (opt && !opt.startsWith("// [FSMC ERROR]")) return opt;
      } catch (e) {
        console.warn("WASM optimize notice:", e);
      }
    }
    return source;
  },

  async generateCpp(source, format, isCpp20 = true, isStandalone = true) {
    const mod = getModule();
    if (mod && mod.compile && source && source.trim()) {
      try {
        const code = mod.compile(source, format, isCpp20 ? 20 : 17, isStandalone);
        if (code && !code.startsWith("// [FSMC ERROR]")) return code;
      } catch (e) {
        console.warn("WASM compile notice:", e);
      }
    }
    const model = await this.parse(source, format);
    return jsCppGen(model, isCpp20);
  },

  async validate(source, format) {
    const mod = getModule();
    if (mod && mod.verify && source && source.trim()) {
      try {
        const res = JSON.parse(mod.verify(source, format));
        if (res && res.diagnostics) return res.diagnostics;
      } catch (e) {
        console.warn("WASM verify notice:", e);
      }
    }
    const model = await this.parse(source, format);
    const diags = [];
    if (model.states.length === 0) {
      diags.push({ severity: "ERROR", category: "Parser", message: "No states could be parsed from the diagram specification." });
    }
    return diags;
  }
};

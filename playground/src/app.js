/**
 * fsmc Playground — Application Coordinator
 * Top-level orchestrator: initialises all subsystems, wires UI events,
 * manages tab state, and runs the main update loop on editor changes.
 */

import { initWasm }          from './wasm_bridge.js';
import { CANONICAL_PRESETS } from './presets.js';
import { ModelManager }      from './model_manager.js';
import { GraphRenderer }     from './graph_renderer.js';
import { ViewportController } from './viewport.js';
import { SimulatorController } from './simulator.js';
import { resolveLeafState }  from './fsm_utils.js';

export const App = {
  currentCanvasView:   'split',
  currentInspectorTab: 'simulator',

  async init() {
    if (window.mermaid) {
      mermaid.initialize({
        startOnLoad: false,
        theme: 'dark',
        securityLevel: 'loose',
        state: { nodeSpacing: 50, rankSpacing: 50, defaultRenderer: 'dagre-wrapper' }
      });
    }

    ViewportController.init();

    const editorEl = document.getElementById("editor");
    editorEl.oninput  = () => this.update();
    editorEl.onchange = () => this.update();
    editorEl.onkeyup  = () => this.update();
    editorEl.onpaste  = () => setTimeout(() => this.update(), 10);

    document.getElementById("presetSelect").onchange = () => this.loadPreset();
    document.getElementById("formatSelect").onchange = () => this.onFormatChange();
    document.getElementById("stdSelect").onchange    = () => this.update();

    const clearBtn = document.getElementById("clearLogBtn");
    if (clearBtn) clearBtn.onclick = (e) => { e.preventDefault(); SimulatorController.clearLog(); };

    const resetBtn = document.getElementById("resetSimBtn");
    if (resetBtn) resetBtn.onclick = () => {
      const initLeaf = resolveLeafState(ModelManager.currentModel, ModelManager.currentModel.initialState);
      SimulatorController.setState(initLeaf);
    };

    const copyBtn = document.getElementById("copyBtn");
    if (copyBtn) copyBtn.onclick = () => {
      const code = document.getElementById("cppPreview").textContent;
      navigator.clipboard.writeText(code).then(() => {
        const textSpan = document.getElementById("copyBtnText");
        if (textSpan) {
          const orig = textSpan.textContent;
          textSpan.textContent = "Copied!";
          setTimeout(() => { textSpan.textContent = orig; }, 2000);
        }
      });
    };

    const optBtn = document.getElementById("optBtn");
    if (optBtn) optBtn.onclick = () => {
      const code = document.getElementById("editor").value;
      const fmt  = document.getElementById("formatSelect").value;
      const optimized = ModelManager.optimize(code, fmt, fmt);
      if (optimized && optimized !== code) {
        document.getElementById("editor").value = optimized;
        const span = optBtn.querySelector("span");
        if (span) { const orig = span.textContent; span.textContent = "Optimized!"; setTimeout(() => { span.textContent = orig; }, 2000); }
        this.update();
      }
    };

    const downloadBtn = document.getElementById("downloadBtn");
    if (downloadBtn) downloadBtn.onclick = () => {
      const code = document.getElementById("cppPreview").textContent;
      const blob = new Blob([code], { type: "text/plain;charset=utf-8" });
      const a = document.createElement("a");
      a.href = URL.createObjectURL(blob);
      a.download = "generated_fsm.hpp";
      a.click();
    };

    const stepBtn = document.getElementById("stepBtn");
    if (stepBtn) stepBtn.onclick = () => SimulatorController.step();

    const mcdcBtn = document.getElementById("mcdcBtn");
    if (mcdcBtn) mcdcBtn.onclick = () => this.switchCanvasTab("mcdc");

    const rtmBtn = document.getElementById("rtmBtn");
    if (rtmBtn) rtmBtn.onclick = () => this.switchCanvasTab("rtm");

    const recBackBtn = document.getElementById("recBackBtn");
    if (recBackBtn) recBackBtn.onclick = () => SimulatorController.stepBack();

    const recForwardBtn = document.getElementById("recForwardBtn");
    if (recForwardBtn) recForwardBtn.onclick = () => SimulatorController.stepForward();

    const recLiveBtn = document.getElementById("recLiveBtn");
    if (recLiveBtn) recLiveBtn.onclick = () => SimulatorController.returnToLive();

    const timeSlider = document.getElementById("timeTravelSlider");
    if (timeSlider) timeSlider.oninput = () => SimulatorController.timeTravelTo(parseInt(timeSlider.value, 10));

    this.initCanvasTabs();
    this.initInspectorTabs();
    this.initLineNumbers();
    this.initSvgExport();
    this.initResizers();

    await initWasm();
    this.loadPreset();
  },

  initCanvasTabs() {
    const tabs    = document.querySelectorAll("#canvasTabs .tab-item");
    const content = document.getElementById("canvasContent");
    tabs.forEach(btn => {
      btn.onclick = () => {
        tabs.forEach(t => t.classList.remove("active"));
        btn.classList.add("active");
        const view = btn.getAttribute("data-view");
        this.currentCanvasView = view;
        if (content) content.className = `canvas-content view-${view}`;
        if (view === 'visual' || view === 'split') ViewportController.applyTransform();
        if (view === 'cpp') this.renderCppOutput();
        if (view === 'mcdc') this.renderMcdcOutput();
        if (view === 'rtm') this.renderRtmOutput();
      };
    });
  },

  switchCanvasTab(view) {
    const tabs    = document.querySelectorAll("#canvasTabs .tab-item");
    const content = document.getElementById("canvasContent");
    tabs.forEach(t => {
      if (t.getAttribute("data-view") === view) t.classList.add("active");
      else t.classList.remove("active");
    });
    this.currentCanvasView = view;
    if (content) content.className = `canvas-content view-${view}`;
    if (view === 'visual' || view === 'split') ViewportController.applyTransform();
    if (view === 'cpp') this.renderCppOutput();
    if (view === 'mcdc') this.renderMcdcOutput();
    if (view === 'rtm') this.renderRtmOutput();
  },

  async renderCppOutput() {
    const code    = document.getElementById("editor").value;
    const format  = document.getElementById("formatSelect").value;
    const isCpp20 = document.getElementById("stdSelect").value === "20";
    const preview = document.getElementById("cppPreview");
    if (preview) {
      preview.textContent = await ModelManager.generateCpp(code, format, isCpp20, true);
    }
  },

  async renderMcdcOutput() {
    const code   = document.getElementById("editor").value;
    const format = document.getElementById("formatSelect").value;
    const preview = document.getElementById("mcdcPreview");
    if (preview) {
      preview.textContent = await ModelManager.generateMcdc(code, format);
    }
  },

  async renderRtmOutput() {
    const code   = document.getElementById("editor").value;
    const format = document.getElementById("formatSelect").value;
    const preview = document.getElementById("rtmPreview");
    if (preview) {
      preview.textContent = await ModelManager.auditRtm(code, format);
    }
  },

  initLineNumbers() {
    const editor      = document.getElementById("editor");
    const lineNumbers = document.getElementById("lineNumbers");
    if (!editor || !lineNumbers) return;
    const update = () => {
      const lines = editor.value.split('\n').length;
      lineNumbers.innerHTML = Array.from({ length: lines }, (_, i) => `<div>${i + 1}</div>`).join('');
    };
    editor.addEventListener("input",  update);
    editor.addEventListener("scroll", () => { lineNumbers.scrollTop = editor.scrollTop; });
    update();
  },

  initSvgExport() {
    const btn = document.getElementById("exportSvgBtn");
    if (!btn) return;
    btn.onclick = () => {
      const svg = document.querySelector("#mermaidCanvas svg");
      if (!svg) return;
      const blob = new Blob([new XMLSerializer().serializeToString(svg)], { type: "image/svg+xml;charset=utf-8" });
      const a = document.createElement("a");
      a.href = URL.createObjectURL(blob);
      a.download = `${ModelManager.currentModel.name || "fsm_diagram"}.svg`;
      a.click();
      URL.revokeObjectURL(a.href);
    };
  },

  initResizers() {
    this._makeResizer("resizerMain",      "mainCanvas",  "inspectorPanel", "workspace", 280);
    this._makeResizer("resizerSplitView", "editorPane",  "visualPane",     "canvasContent", 180);
  },

  _makeResizer(resizerId, leftId, rightId, _containerId, minW) {
    const resizer = document.getElementById(resizerId);
    const left    = document.getElementById(leftId);
    const right   = document.getElementById(rightId);
    if (!resizer || !left || !right) return;

    let isDragging = false, startX = 0, startL = 0, startR = 0;

    resizer.addEventListener("mousedown", (e) => {
      isDragging = true;
      startX = e.clientX;
      startL = left.getBoundingClientRect().width;
      startR = right.getBoundingClientRect().width;
      resizer.classList.add("resizing");
      document.body.style.cssText += ";cursor:col-resize;user-select:none";
    });

    window.addEventListener("mousemove", (e) => {
      if (!isDragging) return;
      const dx    = e.clientX - startX;
      const total = startL + startR;
      const newL  = Math.max(minW, Math.min(total - minW, startL + dx));
      left.style.flex  = `0 0 ${newL}px`;
      right.style.flex = `1 1 ${total - newL}px`;
    });

    window.addEventListener("mouseup", () => {
      if (!isDragging) return;
      isDragging = false;
      resizer.classList.remove("resizing");
      document.body.style.cursor = "";
      document.body.style.userSelect = "";
    });

    resizer.addEventListener("dblclick", () => {
      left.style.flex  = "50 1 0%";
      right.style.flex = "50 1 0%";
    });
  },

  loadPreset() {
    const val = document.getElementById("presetSelect").value;
    const rawCanonical = CANONICAL_PRESETS[val];
    if (!rawCanonical) return;

    const FMT_MAP = {
      autonomous_uav_mission:  "sysml2",
      industrial_press:        "scxml",
      connection_manager:      "plantuml",
      smart_thermostat:        "json",
      satellite_mission:       "cameo",
      async_motor_controller:  "mermaid"
    };
    const nativeFmt = FMT_MAP[val] || "sysml2";

    const formatSel = document.getElementById("formatSelect");
    if (formatSel) formatSel.value = nativeFmt;
    const formatBadge = document.getElementById("formatBadge");
    if (formatBadge) formatBadge.textContent = nativeFmt.toUpperCase();

    document.getElementById("editor").value = rawCanonical;
    ModelManager.currentModel.panX = 0;
    ModelManager.currentModel.panY = 0;
    ModelManager.currentModel.zoom = 1.0;

    SimulatorController.initDatapath(rawCanonical, null, true);
    this.update();
  },

  async onFormatChange() {
    const currentCode = document.getElementById("editor").value;
    const newFmt      = document.getElementById("formatSelect").value;
    const detectedFmt = ModelManager.detectFormat(currentCode);
    const badge       = document.getElementById("formatBadge");

    if (detectedFmt === newFmt) {
      if (badge) badge.textContent = newFmt.toUpperCase();
      this.update();
      return;
    }

    const converted = await ModelManager.export(currentCode, detectedFmt, newFmt);
    document.getElementById("editor").value = converted;
    if (badge) badge.textContent = newFmt.toUpperCase();
    this.update();
  },

  async update() {
    const code    = document.getElementById("editor").value;
    let format    = document.getElementById("formatSelect").value;
    const isCpp20 = document.getElementById("stdSelect").value === "20";

    const detected = ModelManager.detectFormat(code);
    if (detected && detected !== format) {
      format = detected;
      const sel = document.getElementById("formatSelect");
      if (sel && Array.from(sel.options).some(o => o.value === detected)) sel.value = detected;
      const badge = document.getElementById("formatBadge");
      if (badge) badge.textContent = detected.toUpperCase();
    }

    const parsed   = await ModelManager.parse(code, format);
    const initLeaf = resolveLeafState(parsed, parsed.initialState);
    const prevActive = ModelManager.currentModel.activeState;
    const activeLeaf = prevActive && parsed.states.includes(prevActive) ? prevActive : initLeaf;

    ModelManager.currentModel = { ...ModelManager.currentModel, ...parsed, activeState: activeLeaf };

    // 1. C++ output
    this.renderCppOutput();

    // 2. Diagnostics
    const diags = await ModelManager.validate(code, format);
    const diagContainer = document.getElementById("diagnostics");
    const statusBadge   = document.getElementById("modelStatusBadge");
    if (diagContainer) diagContainer.innerHTML = "";

    const hasErrors   = diags.some(d => d.severity === "ERROR" || d.severity === "SafetyCritical" || d.severity === "Error");
    const hasWarnings = diags.some(d => d.severity === "WARNING" || d.severity === "Warning");
    const hasInfo     = diags.some(d => d.severity === "INFO"    || d.severity === "Info");

    if (statusBadge) {
      if (hasErrors)        { statusBadge.textContent = "ERRORS";   statusBadge.className = "status-pill status-err"; }
      else if (hasWarnings) { statusBadge.textContent = "WARNINGS"; statusBadge.className = "status-pill status-warn"; }
      else if (hasInfo)     { statusBadge.textContent = "INFO";     statusBadge.className = "status-pill status-info"; }
      else                  { statusBadge.textContent = "SOUND";    statusBadge.className = "status-pill status-ok"; }
    }

    if (diagContainer) {
      if (diags.length === 0) {
        diagContainer.innerHTML = `<div class="diag-item PASS">[PASS] Model verified and sound: 0 errors, 0 warnings. Invariants hold.</div>`;
      } else {
        for (const d of diags) {
          const isErr  = d.severity === "SafetyCritical" || d.severity === "Error" || d.severity === "ERROR";
          const isWarn = d.severity === "Warning" || d.severity === "WARNING";
          const sevClass = isErr ? "ERROR" : (isWarn ? "WARNING" : "INFO");
          const prefix   = isErr ? "[ERROR]" : (isWarn ? "[WARN]" : "[INFO]");
          const item = document.createElement("div");
          item.className   = `diag-item ${sevClass}`;
          item.textContent = `${prefix} ${!["INFO","ERROR","WARNING"].includes(d.severity) ? `[${d.severity}] ` : ""}${d.category ? `(${d.category}) ` : ""}${d.message}`;
          diagContainer.appendChild(item);
        }
      }
    }

    // 3. Graph render
    GraphRenderer.render(ModelManager.currentModel, code, format);
    SimulatorController.initDatapath(code, ModelManager.currentModel, false);
    SimulatorController.updateControls();
    SimulatorController.updateActiveStateBadge(ModelManager.currentModel.activeState);

    // 4. Line numbers
    const lines = code.split('\n').length;
    const chars = code.length;
    const lineNumbers = document.getElementById("lineNumbers");
    if (lineNumbers) {
      lineNumbers.innerHTML = Array.from({ length: lines }, (_, i) => `<div>${i + 1}</div>`).join('');
    }

    // 5. IDE status bar
    const statusPreset = document.getElementById("statusPreset");
    const statusFormat = document.getElementById("statusFormat");
    const statusStats  = document.getElementById("statusEditorStats");
    const statusSound  = document.getElementById("statusSoundness");
    const statusTarget = document.getElementById("statusTarget");
    const presetSel    = document.getElementById("presetSelect");

    if (statusPreset && presetSel) {
      statusPreset.textContent = `Preset: ${(presetSel.options[presetSel.selectedIndex]?.text || "").split('(')[0].trim()}`;
    }
    if (statusFormat) statusFormat.textContent = `Format: ${format.toUpperCase()}`;
    if (statusStats)  statusStats.textContent  = `Ln ${lines} \u2022 ${chars.toLocaleString()} chars`;
    if (statusSound) {
      if (hasErrors)        { statusSound.textContent = "SMT Invariants: Errors Detected";   statusSound.style.color = "var(--accent-red)"; }
      else if (hasWarnings) { statusSound.textContent = "SMT Invariants: Warnings Detected"; statusSound.style.color = "var(--accent-yellow)"; }
      else                  { statusSound.textContent = "SMT Safety Invariants: Sound (0 Violations)"; statusSound.style.color = "var(--accent-green)"; }
    }
    if (statusTarget) statusTarget.textContent = isCpp20 ? "Target: C++20 Standalone" : "Target: C++17 Standalone";
  }
};

if (typeof window !== 'undefined') {
  window.addEventListener("DOMContentLoaded", () => App.init());
}

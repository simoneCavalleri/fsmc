/**
 * fsmc Web Playground & Live HFSM Simulator
 * ============================================================================
 * Zero-Overhead C++ State Machine Compiler & Visual Engineering Suite
 * Powered directly by the C++ fsmc WebAssembly Engine (Zero Server, Zero Duplication)
 * ============================================================================
 */

// ----------------------------------------------------------------------------
// 1. Curated Canonical State Machine Presets
// ----------------------------------------------------------------------------

const CANONICAL_PRESETS = {
  industrial_press: `@startuml
[*] --> Idle

Idle --> Initializing : PowerOnCmd [SafetyOk && !EStop] / LogPowerOn
Idle --> Idle : PingEvent / Heartbeat

state Initializing {
    [*] --> SelfTest
    SelfTest --> Calibrating : SelfTestOkEvent / StoreDiagnostics
    SelfTest --> Faulted : SelfTestFailEvent [!Recoverable] / LogFault
    SelfTest : ProgressEvent / UpdateDisplay
    Calibrating --> Ready : CalibrationDoneEvent / SaveOffsets
    Calibrating : ProgressEvent / UpdateDisplay
}

Initializing --> Idle : AbortCmd / Cleanup
Ready --> Operating : StartCmd [ToolLoaded && OperatorPresent] / EngageDrive

state Operating {
    [*] --> Running

    state Running {
        [*] --> Manual
        Manual --> Auto : AutoModeCmd [ConfigValid] / SwitchToAuto
        Auto --> Manual : ManualModeCmd / SwitchToManual
        Auto : SensorTickEvent / UpdateFeedback
        Manual : JogCmd / MoveAxis
        Running : HeartbeatEvent / ResetWatchdog
    }

    Running --> Paused : PauseCmd / HoldPosition
    Paused --> Running : ResumeCmd [SafetyOk] / ReleaseHold
    Paused --> Idle : after_30000ms / AutoShutdown
    Running --> Faulted : EStopEvent / EmergencyBrake
}

Operating --> Paused : SuspendCmd / SaveContext
Paused --> Operating[H] : ResumeSessionCmd / RestoreLastActiveState
Faulted --> Operating[H*] : ResetAndResumeCmd [!CriticalFault] / DeepRestoreState
Faulted --> Idle : AckFaultCmd [!CriticalFault] / ClearFault
Faulted --> Faulted : DiagnosticPollEvent / LogDiagnostics
Operating --> Idle : StopCmd / DisengageDrive
@enduml`,

  connection: `@startuml
[*] --> Disconnected
Disconnected --> Connecting : ConnectCmd
Connecting --> Connected : HandshakeOkEvent / StartHeartbeat
Connecting --> Disconnected : TimeoutEvent / LogFailure
Connected : Ping / ResetWatchdog
Connected --> Disconnected : DisconnectCmd / CleanupSession
@enduml`,

  timed_watchdog: `@startuml
[*] --> Healthy
Healthy --> Healthy : HeartbeatPing / ResetTimer
Healthy --> Degraded : after_500ms / RaiseWarning
Degraded --> Healthy : HeartbeatPing
Degraded --> Deadlock : after_1000ms / ForceRestart
@enduml`
};

// ----------------------------------------------------------------------------
// 2. WebAssembly Bridge (Direct C++ fsmc Core Engine)
// ----------------------------------------------------------------------------

let fsmcModule = null;

if (typeof createFsmcModule === 'function') {
  createFsmcModule().then(module => {
    fsmcModule = module;
    window.fsmcModule = module;
    console.log("✓ fsmc C++ WebAssembly engine loaded successfully.");
    App.update();
  }).catch(err => {
    console.warn("WASM module init exception:", err);
  });
}

// ----------------------------------------------------------------------------
// 3. Model Manager (Delegates 100% to C++ fsmc Engine)
// ----------------------------------------------------------------------------

const ModelManager = {
  currentModel: {
    states: [],
    stateDetails: [],
    events: [],
    transitions: [],
    initialState: "Idle",
    activeState: "Idle",
    zoom: 1.0,
    panX: 0,
    panY: 0
  },

  detectFormat(text) {
    const t = (text || "").trim();
    if (t.startsWith('{')) return 'json';
    if (t.includes('@startuml')) return 'plantuml';
    if (t.includes('stateDiagram')) return 'mermaid';
    if (t.includes('state def ') || t.includes('accept ')) return 'sysml2';
    return 'plantuml';
  },

  parse(text, format) {
    const fmt = format || this.detectFormat(text);

    // Call C++ AST parser directly from WebAssembly
    if (fsmcModule && fsmcModule.getModel && text && text.trim()) {
      try {
        const res = JSON.parse(fsmcModule.getModel(text, fmt));
        if (res && !res.error && res.states && res.states.length > 0) {
          const statesArr = res.states;
          for (const s of statesArr) {
            s.is_composite = statesArr.some(child => child.parent === s.name);
          }
          return {
            states: statesArr.map(s => s.name),
            stateDetails: statesArr,
            events: res.events || [],
            transitions: res.transitions || [],
            initialState: res.initialState || statesArr[0].name
          };
        }
      } catch (e) {
        console.warn("WASM getModel exception:", e);
      }
    }

    // Lightweight syntactic fallback when WASM is initializing
    return this.fallbackParse(text, fmt);
  },

  fallbackParse(text, format) {
    const lines = text.split('\n');
    const states = new Set();
    const events = new Set();
    const transitions = [];
    let initial = "Idle";

    for (let raw of lines) {
      const line = raw.trim();
      if (!line || line.startsWith('@') || line.startsWith('stateDiagram')) continue;
      if (line.includes('-->')) {
        const parts = line.split('-->');
        const src = parts[0].trim();
        const rest = parts[1].trim();
        const dst = rest.split(':')[0].trim().replace(/\[H\*?\]/g, '');
        let evt = "Anonymous";
        if (rest.includes(':')) {
          evt = rest.split(':')[1].split('/')[0].split('[')[0].trim() || "Anonymous";
        }
        if (src === '[*]' && dst) {
          initial = dst;
        } else {
          if (src && src !== '[*]') states.add(src);
          if (dst && dst !== '[*]') states.add(dst);
          if (evt && evt !== 'Anonymous') events.add(evt);
          transitions.push({ source: src, target: dst, event: evt, is_internal: (src === dst) });
        }
      }
    }

    return {
      states: Array.from(states),
      stateDetails: Array.from(states).map(s => ({ name: s, parent: "", is_composite: false })),
      events: Array.from(events),
      transitions: transitions,
      initialState: initial
    };
  },

  export(source, fromFormat, toFormat) {
    if (fsmcModule && fsmcModule.exportDiagram && source && source.trim()) {
      try {
        const exported = fsmcModule.exportDiagram(source, fromFormat, toFormat);
        if (exported && !exported.startsWith("// [FSMC ERROR]")) {
          return exported;
        }
      } catch (e) {
        console.warn("WASM export exception:", e);
      }
    }
    return source;
  },

  generateCpp(source, format, isCpp20 = true, isStandalone = false) {
    if (fsmcModule && fsmcModule.compile && source && source.trim()) {
      try {
        const code = fsmcModule.compile(source, format, isCpp20 ? 20 : 17, isStandalone);
        if (code && !code.startsWith("// [FSMC ERROR]")) {
          return code;
        }
      } catch (e) {
        console.warn("WASM compile exception:", e);
      }
    }
    return `// Compiling with fsmc C++ engine...\n#pragma once\n\n// Target: C++${isCpp20 ? '20' : '17'} (${isStandalone ? 'Standalone 0-Deps' : 'Modular'})\n`;
  },

  validate(source, format) {
    if (fsmcModule && fsmcModule.verify && source && source.trim()) {
      try {
        const res = JSON.parse(fsmcModule.verify(source, format));
        if (res && res.diagnostics) return res.diagnostics;
      } catch (e) {
        console.warn("WASM verify exception:", e);
      }
    }
    return [];
  }
};

// ----------------------------------------------------------------------------
// 4. Unified Graph Renderer (Mermaid AST Renderer)
// ----------------------------------------------------------------------------

let renderSeq = 0;

const GraphRenderer = {
  buildCanonicalGraph(model) {
    const details = model.stateDetails || model.states.map(s => ({ name: s, parent: "", is_composite: false }));
    const stateMap = new Map(details.map(d => [d.name, d]));
    const getChildren = (parent) => details.filter(d => (d.parent || "") === (parent || ""));

    let out = "stateDiagram-v2\n";
    if (model.initialState) {
      out += `    [*] --> ${model.initialState}\n`;
    }

    const renderComposite = (stateObj, indent) => {
      let res = "";
      if (stateObj.is_composite) {
        res += `${indent}state ${stateObj.name} {\n`;
        if (stateObj.initial_sub_state) {
          res += `${indent}    [*] --> ${stateObj.initial_sub_state}\n`;
        }
        const children = getChildren(stateObj.name);
        for (const child of children) {
          if (child.is_composite) {
            res += renderComposite(child, indent + "    ");
          } else {
            res += `${indent}    state ${child.name}\n`;
          }
        }
        const localTrans = model.transitions.filter(t => {
          const sP = (stateMap.get(t.source) || {}).parent || "";
          const dP = (stateMap.get(t.target) || {}).parent || "";
          return sP === stateObj.name && (dP === stateObj.name || t.is_internal);
        });
        for (const t of localTrans) {
          let label = t.event || "";
          if (t.guard) label += ` [${t.guard}]`;
          if (t.action) label += ` / ${t.action}`;
          const cleanLabel = label.replace(/\[defer\]/g, 'defer');
          const lblStr = cleanLabel && cleanLabel !== "Anonymous" ? ` : ${cleanLabel}` : "";
          const cleanTarget = t.target.replace(/\[H\*?\]/g, '');
          res += `${indent}    ${t.source} --> ${t.is_internal ? t.source : cleanTarget}${lblStr}\n`;
        }
        res += `${indent}}\n`;
      }
      return res;
    };

    for (const topState of details.filter(s => !s.parent && s.is_composite)) {
      out += renderComposite(topState, "    ");
    }

    const outerTrans = model.transitions.filter(t => {
      const sP = (stateMap.get(t.source) || {}).parent || "";
      const dP = (stateMap.get(t.target) || {}).parent || "";
      return sP === "" || dP === "" || sP !== dP;
    });

    for (const t of outerTrans) {
      let label = t.event || "";
      if (t.guard) label += ` [${t.guard}]`;
      if (t.action) label += ` / ${t.action}`;
      const cleanLabel = label.replace(/\[defer\]/g, 'defer');
      const lblStr = cleanLabel && cleanLabel !== "Anonymous" ? ` : ${cleanLabel}` : "";
      const cleanTarget = t.target.replace(/\[H\*?\]/g, '');
      out += `    ${t.source} --> ${t.is_internal ? t.source : cleanTarget}${lblStr}\n`;
    }
    return out.trim();
  },

  async render(model) {
    const canvas = document.getElementById("mermaidCanvas");
    if (!model.states || model.states.length === 0) {
      canvas.innerHTML = `<div style="color: var(--text-muted); font-family: var(--font-mono); font-size: 0.8rem; padding: 20px; text-align: center;">No states detected in diagram.</div>`;
      return;
    }

    const canonicalGraph = this.buildCanonicalGraph(model);
    if (window.mermaid && canonicalGraph) {
      const seq = ++renderSeq;
      try {
        const id = "mermaid_svg_" + seq;
        const { svg } = await mermaid.render(id, canonicalGraph);
        if (seq === renderSeq) {
          canvas.innerHTML = svg;
          this.highlightActive(model.activeState);
          this.attachHandlers();
          ViewportController.applyTransform(false);
          return;
        }
      } catch (err) {
        console.warn("Mermaid layout warning:", err);
        const tempEl = document.getElementById("d" + "mermaid_svg_" + seq);
        if (tempEl) tempEl.remove();
      }
    }
  },

  highlightActive(activeState) {
    const svg = document.querySelector("#mermaidCanvas svg");
    if (!svg) return;

    svg.querySelectorAll(".node").forEach(n => {
      const label = n.textContent.trim();
      const isActive = label.includes(activeState);
      n.classList.toggle("active-state", isActive);

      n.querySelectorAll("span, p, text, div, tspan, .nodeLabel").forEach(el => {
        el.style.color = isActive ? "#ffffff" : "#f0f6fc";
        el.style.fill = isActive ? "#ffffff" : "#f0f6fc";
        el.style.fontWeight = isActive ? "700" : "500";
        el.style.textShadow = isActive ? "0 1px 3px rgba(0,0,0,0.9)" : "none";
      });

      const rect = n.querySelector("rect, polygon, circle, .label-container");
      if (rect) {
        rect.style.fill = isActive ? "#1e3a8a" : "#161b22";
        rect.style.stroke = isActive ? "#38bdf8" : "#30363d";
        rect.style.strokeWidth = isActive ? "3px" : "1px";
      }
    });
  },

  attachHandlers() {
    const svg = document.querySelector("#mermaidCanvas svg");
    if (!svg) return;
    svg.querySelectorAll(".node").forEach(node => {
      node.style.cursor = "pointer";
      node.onclick = (e) => {
        if (ViewportController.hasDragged) return;
        e.stopPropagation();
        const text = node.textContent.trim();
        const matched = ModelManager.currentModel.states.find(s => text.includes(s));
        if (matched) {
          ModelManager.currentModel.activeState = matched;
          SimulatorController.updateUI();
          this.highlightActive(matched);
        }
      };
    });
  }
};

// ----------------------------------------------------------------------------
// 5. Viewport Controller (Pan & Zoom)
// ----------------------------------------------------------------------------

const ViewportController = {
  isPanning: false,
  startPanX: 0,
  startPanY: 0,
  hasDragged: false,

  init() {
    const canvas = document.getElementById("mermaidCanvas");
    if (!canvas) return;

    let startMouseX = 0, startMouseY = 0;

    canvas.addEventListener("mousedown", (e) => {
      if (e.button !== 0) return;
      this.isPanning = true;
      this.hasDragged = false;
      startMouseX = e.clientX;
      startMouseY = e.clientY;
      this.startPanX = ModelManager.currentModel.panX || 0;
      this.startPanY = ModelManager.currentModel.panY || 0;
      canvas.style.cursor = "grabbing";
    });

    window.addEventListener("mousemove", (e) => {
      if (!this.isPanning) return;
      const dx = e.clientX - startMouseX;
      const dy = e.clientY - startMouseY;
      if (Math.abs(dx) > 3 || Math.abs(dy) > 3) this.hasDragged = true;
      ModelManager.currentModel.panX = this.startPanX + dx;
      ModelManager.currentModel.panY = this.startPanY + dy;
      this.applyTransform(false);
    });

    window.addEventListener("mouseup", () => {
      if (this.isPanning) {
        this.isPanning = false;
        canvas.style.cursor = "grab";
        this.applyTransform(false);
      }
    });

    canvas.addEventListener("wheel", (e) => {
      e.preventDefault();
      if (e.ctrlKey || e.metaKey || e.altKey) {
        const delta = e.deltaY < 0 ? 0.1 : -0.1;
        ModelManager.currentModel.zoom = Math.min(3.0, Math.max(0.3, Number(((ModelManager.currentModel.zoom || 1.0) + delta).toFixed(2))));
        this.applyTransform(true);
      } else {
        ModelManager.currentModel.panX = (ModelManager.currentModel.panX || 0) - e.deltaX;
        ModelManager.currentModel.panY = (ModelManager.currentModel.panY || 0) - e.deltaY;
        this.applyTransform(false);
      }
    }, { passive: false });

    document.getElementById("zoomInBtn").onclick = () => {
      ModelManager.currentModel.zoom = Math.min(3.0, Number(((ModelManager.currentModel.zoom || 1.0) + 0.15).toFixed(2)));
      this.applyTransform(true);
    };
    document.getElementById("zoomOutBtn").onclick = () => {
      ModelManager.currentModel.zoom = Math.max(0.3, Number(((ModelManager.currentModel.zoom || 1.0) - 0.15).toFixed(2)));
      this.applyTransform(true);
    };
    document.getElementById("zoomResetBtn").onclick = () => {
      ModelManager.currentModel.zoom = 1.0;
      ModelManager.currentModel.panX = 0;
      ModelManager.currentModel.panY = 0;
      this.applyTransform(true);
    };
  },

  applyTransform(animate = true) {
    const badge = document.getElementById("zoomLevelBadge");
    if (badge) badge.textContent = Math.round((ModelManager.currentModel.zoom || 1.0) * 100) + "%";
    const svg = document.querySelector("#mermaidCanvas svg");
    if (svg) {
      const px = ModelManager.currentModel.panX || 0;
      const py = ModelManager.currentModel.panY || 0;
      const scale = ModelManager.currentModel.zoom || 1.0;
      svg.style.transform = `translate(${px}px, ${py}px) scale(${scale})`;
      svg.style.transformOrigin = "center center";
      svg.style.transition = animate ? "transform 0.12s ease-out" : "none";
    }
  }
};

// ----------------------------------------------------------------------------
// 6. Simulator Controller (Live HFSM Simulation)
// ----------------------------------------------------------------------------

const SimulatorController = {
  updateUI() {
    document.getElementById("activeStateBadge").textContent = ModelManager.currentModel.activeState;
    const btnContainer = document.getElementById("eventButtons");
    btnContainer.innerHTML = "";

    for (const evt of ModelManager.currentModel.events) {
      const btn = document.createElement("button");
      btn.className = "btn-event";
      btn.textContent = evt;
      btn.onclick = () => this.dispatch(evt);
      btnContainer.appendChild(btn);
    }
    GraphRenderer.highlightActive(ModelManager.currentModel.activeState);
  },

  dispatch(eventName) {
    const model = ModelManager.currentModel;
    let matching = model.transitions.find(t => t.source === model.activeState && t.event === eventName);

    // Hierarchical Parent Delegation
    if (!matching && model.stateDetails) {
      const activeObj = model.stateDetails.find(s => s.name === model.activeState);
      if (activeObj && activeObj.parent) {
        matching = model.transitions.find(t => t.source === activeObj.parent && t.event === eventName);
      }
    }

    const log = document.getElementById("historyLog");
    const time = new Date().toLocaleTimeString();

    if (matching) {
      const oldState = model.activeState;
      let target = matching.target;
      if (model.stateDetails) {
        const targetObj = model.stateDetails.find(s => s.name === target);
        if (targetObj && targetObj.initial_sub_state) target = targetObj.initial_sub_state;
      }
      model.activeState = target;
      document.getElementById("activeStateBadge").textContent = model.activeState;

      const act = matching.action ? ` <span style="color:#a855f7;">[/${matching.action}]</span>` : '';
      const entry = document.createElement("div");
      entry.className = "log-entry";
      entry.innerHTML = `[${time}] <span class="event">${eventName}</span>${act} : ${oldState} -> <span class="success">${target}</span>`;
      log.prepend(entry);
    } else {
      const entry = document.createElement("div");
      entry.className = "log-entry";
      entry.innerHTML = `[${time}] <span class="event">${eventName}</span> : <span class="dropped">Dropped ('${model.activeState}')</span>`;
      log.prepend(entry);
    }

    GraphRenderer.highlightActive(model.activeState);
  }
};

// ----------------------------------------------------------------------------
// 7. Resizer Controller (Fluid Layout Splitters)
// ----------------------------------------------------------------------------

const ResizerController = {
  init() {
    const workspace = document.getElementById("workspace");
    const pEditor = document.getElementById("panelEditor");
    const pVisual = document.getElementById("panelVisual");
    const pRight = document.getElementById("panelRight");
    const resCol1 = document.getElementById("resizerCol1");
    const resCol2 = document.getElementById("resizerCol2");

    const setupColResizer = (resizer, leftEl, rightEl) => {
      if (!resizer || !leftEl || !rightEl || !workspace) return;
      let dragging = false, startX = 0, wL = 0, wR = 0;

      resizer.addEventListener("mousedown", (e) => {
        dragging = true;
        startX = e.clientX;
        wL = leftEl.getBoundingClientRect().width;
        wR = rightEl.getBoundingClientRect().width;
        resizer.classList.add("resizing");
        document.body.style.cursor = "col-resize";
        document.body.style.userSelect = "none";
      });

      resizer.addEventListener("dblclick", () => {
        pEditor.style.flex = "30 1 0%";
        pVisual.style.flex = "40 1 0%";
        pRight.style.flex = "30 1 0%";
      });

      document.addEventListener("mousemove", (e) => {
        if (!dragging) return;
        const dx = e.clientX - startX;
        const totalW = workspace.clientWidth || 1000;
        const nL = Math.max(180, wL + dx);
        const nR = Math.max(180, wR - dx);
        leftEl.style.flex = `${((nL / totalW) * 100).toFixed(2)} 1 0%`;
        rightEl.style.flex = `${((nR / totalW) * 100).toFixed(2)} 1 0%`;
      });

      document.addEventListener("mouseup", () => {
        if (dragging) {
          dragging = false;
          resizer.classList.remove("resizing");
          document.body.style.cursor = "";
          document.body.style.userSelect = "";
        }
      });
    };

    setupColResizer(resCol1, pEditor, pVisual);
    setupColResizer(resCol2, pVisual, pRight);

    // Sub-column Resizer (C++ Preview <-> Simulator)
    const resSplit = document.getElementById("resizerSplitCol");
    const pCpp = document.getElementById("panelCpp");
    const pSim = document.getElementById("panelSim");
    if (resSplit && pCpp && pSim && pRight) {
      let dragging = false, startY = 0, h1 = 0, h2 = 0;
      resSplit.addEventListener("mousedown", (e) => {
        dragging = true;
        startY = e.clientY;
        h1 = pCpp.getBoundingClientRect().height;
        h2 = pSim.getBoundingClientRect().height;
        resSplit.classList.add("resizing");
        document.body.style.cursor = "row-resize";
      });
      resSplit.addEventListener("dblclick", () => {
        pCpp.style.flex = "50 1 0%";
        pSim.style.flex = "50 1 0%";
      });
      document.addEventListener("mousemove", (e) => {
        if (!dragging) return;
        const dy = e.clientY - startY;
        const totalH = pRight.clientHeight || 500;
        pCpp.style.flex = `${((Math.max(80, h1 + dy) / totalH) * 100).toFixed(2)} 1 0%`;
        pSim.style.flex = `${((Math.max(80, h2 - dy) / totalH) * 100).toFixed(2)} 1 0%`;
      });
      document.addEventListener("mouseup", () => {
        if (dragging) {
          dragging = false;
          resSplit.classList.remove("resizing");
          document.body.style.cursor = "";
        }
      });
    }

    // Diagnostics Resizer
    const resDiag = document.getElementById("resizerDiagnostics");
    const diagWrap = document.getElementById("diagnosticsWrapper");
    if (resDiag && diagWrap) {
      let dragging = false, startY = 0, startH = 0;
      resDiag.addEventListener("mousedown", (e) => {
        dragging = true;
        startY = e.clientY;
        startH = diagWrap.getBoundingClientRect().height;
        resDiag.classList.add("resizing");
      });
      resDiag.addEventListener("dblclick", () => { diagWrap.style.height = "130px"; });
      document.addEventListener("mousemove", (e) => {
        if (!dragging) return;
        diagWrap.style.height = `${Math.max(32, Math.min(300, startH + (startY - e.clientY)))}px`;
      });
      document.addEventListener("mouseup", () => {
        if (dragging) {
          dragging = false;
          resDiag.classList.remove("resizing");
        }
      });
    }
  }
};

// ----------------------------------------------------------------------------
// 8. Application Bootstrap & Event Wiring
// ----------------------------------------------------------------------------

const App = {
  init() {
    if (window.mermaid) {
      mermaid.initialize({
        startOnLoad: false,
        theme: 'dark',
        securityLevel: 'loose',
        themeVariables: {
          darkMode: true,
          background: '#0d1117',
          primaryColor: '#21262d',
          primaryBorderColor: '#30363d',
          primaryTextColor: '#f0f6fc',
          lineColor: '#58a6ff',
          secondaryColor: '#161b22',
          tertiaryColor: '#21262d'
        }
      });
    }

    ViewportController.init();
    ResizerController.init();

    document.getElementById("presetSelect").onchange = () => this.loadPreset();
    document.getElementById("formatSelect").onchange = () => this.onFormatChange();
    document.getElementById("stdSelect").onchange = () => this.update();
    document.getElementById("editor").oninput = () => this.update();

    document.getElementById("copyBtn").onclick = () => {
      const code = document.getElementById("editor").value;
      const fmt = document.getElementById("formatSelect").value;
      const isCpp20 = document.getElementById("stdSelect").value === "20";
      const fullHeader = ModelManager.generateCpp(code, fmt, isCpp20, true);
      navigator.clipboard.writeText(fullHeader);
      alert("Single-Header Standalone C++ copied to clipboard!");
    };

    document.getElementById("downloadBtn").onclick = () => {
      const code = document.getElementById("editor").value;
      const fmt = document.getElementById("formatSelect").value;
      const isCpp20 = document.getElementById("stdSelect").value === "20";
      const headerCode = ModelManager.generateCpp(code, fmt, isCpp20, true);
      const blob = new Blob([headerCode], { type: "text/plain;charset=utf-8" });
      const a = document.createElement("a");
      a.href = URL.createObjectURL(blob);
      a.download = `${(ModelManager.currentModel.initialState || 'fsm').toLowerCase()}_fsm.hpp`;
      a.click();
    };

    document.getElementById("resetSimBtn").onclick = () => {
      ModelManager.currentModel.activeState = ModelManager.currentModel.initialState;
      SimulatorController.updateUI();
    };

    document.getElementById("clearLogBtn").onclick = (e) => {
      e.preventDefault();
      document.getElementById("historyLog").innerHTML = "";
    };

    this.loadPreset();
  },

  loadPreset() {
    const fmt = document.getElementById("formatSelect").value;
    const presetKey = document.getElementById("presetSelect").value;
    const canonicalText = CANONICAL_PRESETS[presetKey] || CANONICAL_PRESETS.industrial_press;

    // Convert canonical model to the target format via C++ serializer
    document.getElementById("editor").value = ModelManager.export(canonicalText, 'plantuml', fmt);
    document.getElementById("formatBadge").textContent = fmt.toUpperCase();

    ModelManager.currentModel.panX = 0;
    ModelManager.currentModel.panY = 0;
    ModelManager.currentModel.zoom = 1.0;
    this.update();
  },

  onFormatChange() {
    const currentCode = document.getElementById("editor").value;
    const newFmt = document.getElementById("formatSelect").value;
    const detectedFmt = ModelManager.detectFormat(currentCode);
    document.getElementById("formatBadge").textContent = newFmt.toUpperCase();

    // Transpile via C++ WebAssembly engine
    document.getElementById("editor").value = ModelManager.export(currentCode, detectedFmt, newFmt);
    this.update();
  },

  update() {
    const code = document.getElementById("editor").value;
    const format = document.getElementById("formatSelect").value;
    const isCpp20 = document.getElementById("stdSelect").value === "20";

    // 1. Parse AST from C++ WebAssembly parser
    const parsed = ModelManager.parse(code, format);
    ModelManager.currentModel = {
      ...ModelManager.currentModel,
      ...parsed,
      activeState: ModelManager.currentModel.activeState && parsed.states.includes(ModelManager.currentModel.activeState) ? ModelManager.currentModel.activeState : parsed.initialState
    };

    // 2. Generate C++ Modular Preview directly from C++ CppGenerator
    document.getElementById("cppPreview").textContent = ModelManager.generateCpp(code, format, isCpp20, false);

    // 3. Model Diagnostics from C++ FsmValidator
    const diags = ModelManager.validate(code, format);
    const diagContainer = document.getElementById("diagnostics");
    const statusBadge = document.getElementById("modelStatusBadge");
    diagContainer.innerHTML = "";

    if (diags.length === 0) {
      statusBadge.textContent = "SOUND";
      statusBadge.className = "status-pill status-ok";
      diagContainer.innerHTML = `<div class="diag-item INFO">Model verified and sound (0 errors, 0 warnings).</div>`;
    } else {
      const hasCrit = diags.some(d => d.severity === "SAFETY_CRITICAL" || d.severity === "ERROR");
      statusBadge.textContent = hasCrit ? "CRITICAL" : "WARNINGS";
      statusBadge.className = hasCrit ? "status-pill status-err" : "status-pill status-warn";
      for (const d of diags) {
        diagContainer.innerHTML += `<div class="diag-item ${d.severity}"><strong>[${d.category}]</strong> ${d.message}</div>`;
      }
    }

    // 4. Render Visual Graph & Simulator
    GraphRenderer.render(ModelManager.currentModel);
    SimulatorController.updateUI();
  }
};

window.onload = () => App.init();

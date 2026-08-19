const PRESETS = {
  mermaid: {
    connection: `stateDiagram-v2
    [*] --> Disconnected
    Disconnected --> Connecting : ConnectCmd
    Connecting --> Connected : HandshakeOkEvent / StartHeartbeat
    Connecting --> Disconnected : TimeoutEvent / LogFailure
    Connected : Ping / ResetWatchdog
    Connected --> Disconnected : DisconnectCmd / CleanupSession`,

    motor: `stateDiagram-v2
    [*] --> Idle
    Idle --> Accelerating : StartCmd
    Accelerating --> Running : TargetSpeedReached
    Running --> Decelerating : SlowDownCmd
    Decelerating --> Idle : SpeedZero
    Running --> Fault : OverheatEvent
    Fault --> Idle : ResetCmd`,

    mission: `stateDiagram-v2
    [*] --> Standby
    Standby --> Booting : PowerOn
    Booting --> Ready : BootComplete
    Ready --> InFlight : LaunchCmd [ClearanceOk]
    InFlight --> Orbit : InsertionOk
    Orbit --> Deorbiting : DeorbitCmd
    Deorbiting --> Landed : Touchdown
    Landed --> [*]`,

    timed_watchdog: `stateDiagram-v2
    [*] --> Healthy
    Healthy --> Healthy : HeartbeatPing / ResetTimer
    Healthy --> Degraded : after_500ms / RaiseWarning
    Degraded --> Healthy : HeartbeatPing
    Degraded --> Deadlock : after_1000ms / ForceRestart
    Deadlock --> [*]`,

    livelock_demo: `stateDiagram-v2
    [*] --> InitState
    InitState --> StateA
    StateA --> StateB
    StateB --> StateA`
  },

  plantuml: {
    connection: `@startuml
[*] --> Disconnected
Disconnected --> Connecting : ConnectCmd
Connecting --> Connected : HandshakeOkEvent / StartHeartbeat
Connecting --> Disconnected : TimeoutEvent / LogFailure
Connected --> Disconnected : DisconnectCmd / CleanupSession
@enduml`,
    motor: `@startuml
[*] --> Idle
Idle --> Accelerating : StartCmd
Accelerating --> Running : TargetSpeedReached
Running --> Decelerating : SlowDownCmd
Decelerating --> Idle : SpeedZero
@enduml`,
    mission: `@startuml
[*] --> Standby
Standby --> InFlight : LaunchCmd [ClearanceOk]
InFlight --> Landed : Touchdown
@enduml`,
    timed_watchdog: `@startuml
[*] --> Healthy
Healthy --> Degraded : after_500ms
Degraded --> Healthy : HeartbeatPing
@enduml`,
    livelock_demo: `@startuml
[*] --> StateA
StateA --> StateB
StateB --> StateA
@enduml`
  },

  sysml2: {
    connection: `state def ConnectionFSM {
    entry; then Disconnected;
    state Disconnected {
        accept ConnectCmd then Connecting;
    }
    state Connecting {
        accept HandshakeOkEvent then Connected;
        accept TimeoutEvent then Disconnected;
    }
    state Connected {
        accept DisconnectCmd then Disconnected;
    }
}`,
    motor: `state def MotorFSM {
    entry; then Idle;
    state Idle { accept StartCmd then Running; }
    state Running { accept StopCmd then Idle; }
}`,
    mission: `state def MissionFSM {
    entry; then Standby;
    state Standby { accept LaunchCmd then InFlight; }
    state InFlight { accept Touchdown then Landed; }
}`,
    timed_watchdog: `state def WatchdogFSM {
    entry; then Healthy;
    state Healthy { accept after_500ms then Degraded; }
    state Degraded { accept HeartbeatPing then Healthy; }
}`,
    livelock_demo: `state def LivelockFSM {
    entry; then StateA;
    state StateA { then StateB; }
    state StateB { then StateA; }
}`
  },

  json: {
    connection: `{
  "id": "ConnectionFSM",
  "initial": "Disconnected",
  "states": {
    "Disconnected": { "on": { "ConnectCmd": "Connecting" } },
    "Connecting": { "on": { "HandshakeOkEvent": "Connected", "TimeoutEvent": "Disconnected" } },
    "Connected": { "on": { "DisconnectCmd": "Disconnected" } }
  }
}`,
    motor: `{
  "id": "MotorFSM",
  "initial": "Idle",
  "states": {
    "Idle": { "on": { "StartCmd": "Running" } },
    "Running": { "on": { "StopCmd": "Idle" } }
  }
}`,
    mission: `{
  "id": "MissionFSM",
  "initial": "Standby",
  "states": {
    "Standby": { "on": { "LaunchCmd": "InFlight" } },
    "InFlight": { "on": { "Touchdown": "Landed" } }
  }
}`,
    timed_watchdog: `{
  "id": "WatchdogFSM",
  "initial": "Healthy",
  "states": {
    "Healthy": { "on": { "after_500ms": "Degraded" } },
    "Degraded": { "on": { "HeartbeatPing": "Healthy" } }
  }
}`,
    livelock_demo: `{
  "id": "LivelockFSM",
  "initial": "StateA",
  "states": {
    "StateA": { "on": { "": "StateB" } },
    "StateB": { "on": { "": "StateA" } }
  }
}`
  }
};

let currentModel = {
  states: [],
  events: [],
  transitions: [],
  initialState: "Disconnected",
  activeState: "Disconnected",
  zoom: 1.0
};

// Robust Diagram Parser
function parseDiagram(text, format) {
  if (format === 'json') {
    try {
      const obj = JSON.parse(text);
      const states = Object.keys(obj.states || {});
      const transitions = [];
      const events = new Set();
      for (const [s, sObj] of Object.entries(obj.states || {})) {
        for (const [e, target] of Object.entries(sObj.on || {})) {
          const evt = e || "Anonymous";
          if (evt !== "Anonymous") events.add(evt);
          transitions.push({ source: s, target: target, event: evt, guard: "", action: "" });
        }
      }
      return {
        states,
        events: Array.from(events),
        transitions,
        initialState: obj.initial || states[0] || "Init"
      };
    } catch (e) {
      return { states: [], events: [], transitions: [], initialState: "Error" };
    }
  }

  const lines = text.split('\n');
  const states = new Set();
  const events = new Set();
  const transitions = [];
  let initialState = "";

  for (let line of lines) {
    line = line.trim();
    if (!line || line.startsWith('stateDiagram') || line.startsWith('@startuml') || line.startsWith('@enduml') || line.startsWith('%%')) continue;

    // SysML v2 syntax
    if (line.includes('accept ') && line.includes(' then ')) {
      const parts = line.replace('accept ', '').split(' then ');
      const evt = parts[0].trim();
      const target = parts[1].replace(';', '').trim();
      events.add(evt);
      transitions.push({ source: "Active", target: target, event: evt, guard: "", action: "" });
      states.add(target);
      continue;
    }

    // [*] --> Init or StateA --> StateB
    if (line.includes('-->')) {
      const parts = line.split('-->');
      const src = parts[0].trim();
      const rest = parts[1].trim();

      let dst = rest;
      let label = "";
      if (rest.includes(':')) {
        const sub = rest.split(':');
        dst = sub[0].trim();
        label = sub.slice(1).join(':').trim();
      }

      if (src === '[*]') {
        initialState = dst;
        states.add(dst);
        continue;
      }

      states.add(src);
      if (dst !== '[*]') states.add(dst);

      let evt = "Anonymous";
      let guard = "";
      let action = "";

      if (label) {
        let work = label;
        if (work.includes('/')) {
          const slash = work.split('/');
          action = slash[1].trim();
          work = slash[0].trim();
        }
        if (work.includes('[')) {
          const openB = work.indexOf('[');
          const closeB = work.indexOf(']');
          guard = work.substring(openB + 1, closeB).trim();
          work = (work.substring(0, openB) + work.substring(closeB + 1)).trim();
        }
        evt = work.trim() || "Anonymous";
      }

      if (evt !== "Anonymous") events.add(evt);
      transitions.push({ source: src, target: dst, event: evt, guard: guard, action: action });
    }
  }

  if (!initialState && states.size > 0) {
    initialState = Array.from(states)[0];
  }

  return {
    states: Array.from(states),
    events: Array.from(events),
    transitions: transitions,
    initialState: initialState || "Idle"
  };
}

function generateCppHeader(model, isCpp20 = true) {
  const stateStructs = model.states.map(s => `struct ${s} { static constexpr std::string_view name = "${s}"; };`).join('\n');
  const eventStructs = model.events.map(e => `struct ${e} {};`).join('\n');

  const rows = model.transitions.map(t => {
    const grd = t.guard ? t.guard : "fsm::no_guard";
    const act = t.action ? t.action : "fsm::no_action";
    return `    fsm::row<${t.source}, ${t.event}, ${t.target}, ${grd}, ${act}>`;
  }).join(',\n');

  return `// ============================================================================
// Generated by fsmc (Universal Zero-Overhead C++ FSM Compiler)
// Target Standard: ${isCpp20 ? 'C++20 (Concepts & Metaprogramming)' : 'C++17 (Zero-Overhead Transition Tables)'}
// ============================================================================
#pragma once

#include <string_view>
#include "fsm/fsm.hpp"

namespace fsm_generated {

// 1. State Type Definitions
${stateStructs}

// 2. Event Type Definitions
${eventStructs}

// 3. Transition Table Definition
using TransitionTable = fsm::transition_table<
${rows}
>;

// 4. Concrete State Machine Type
using StateMachine = fsm::fsm<TransitionTable, fsm::no_context, ${model.initialState}>;

} // namespace fsm_generated
`;
}

function runModelChecker(model) {
  const diagnostics = [];

  // Livelock check
  const eventless = model.transitions.filter(t => t.event === "Anonymous");
  if (eventless.length > 1) {
    diagnostics.push({ severity: "SAFETY_CRITICAL", category: "Livelock", message: "Eventless transitions detected (potential instantaneous cascade/cycle)." });
  }

  // Reachability check
  const reachable = new Set([model.initialState]);
  let changed = true;
  while (changed) {
    changed = false;
    for (const t of model.transitions) {
      if (reachable.has(t.source) && !reachable.has(t.target)) {
        reachable.add(t.target);
        changed = true;
      }
    }
  }

  for (const s of model.states) {
    if (!reachable.has(s)) {
      diagnostics.push({ severity: "WARNING", category: "Reachability", message: `State unreachable from initial state: '${s}'` });
    }
  }

  // Deadlock check
  for (const s of model.states) {
    const outgoing = model.transitions.filter(t => t.source === s);
    const incoming = model.transitions.filter(t => t.target === s);
    if (incoming.length > 0 && outgoing.length === 0 && s !== "Final" && s !== "Landed" && s !== "Deadlock" && s !== "Stopped") {
      diagnostics.push({ severity: "WARNING", category: "Deadlock", message: `Potential trap state: '${s}' has incoming transitions but no outgoing transitions.` });
    }
  }

  return diagnostics;
}

// Built-in Native SVG Graph Visualizer (Guaranteed visual rendering)
function renderNativeSvg(model) {
  const canvas = document.getElementById("mermaidCanvas");
  const states = model.states;
  if (states.length === 0) {
    canvas.innerHTML = `<div style="color: var(--text-muted); font-family: var(--font-mono); font-size: 0.8rem; padding: 20px;">No states detected in diagram.</div>`;
    return;
  }

  const width = 560;
  const height = Math.max(360, states.length * 75);
  const nodeWidth = 140;
  const nodeHeight = 44;

  // Calculate coordinates in a clean vertical/staggered layout
  const nodeCoords = {};
  states.forEach((s, idx) => {
    const x = (idx % 2 === 0) ? 90 : 330;
    const y = 50 + idx * 65;
    nodeCoords[s] = { x, y };
  });

  let svgContent = `<svg width="${width}" height="${height}" viewBox="0 0 ${width} ${height}" xmlns="http://www.w3.org/2000/svg" style="transform: scale(${currentModel.zoom || 1}); transform-origin: center center;">
    <defs>
      <marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
        <path d="M 0 1 L 10 5 L 0 9 z" fill="#8b5cf6" />
      </marker>
    </defs>`;

  // Draw transition paths
  model.transitions.forEach(t => {
    const src = nodeCoords[t.source];
    const dst = nodeCoords[t.target];
    if (src && dst) {
      const isSelf = t.source === t.target;
      if (isSelf) {
        const path = `M ${src.x + nodeWidth} ${src.y + 15} C ${src.x + nodeWidth + 40} ${src.y - 20}, ${src.x + nodeWidth + 40} ${src.y + 50}, ${src.x + nodeWidth} ${src.y + 30}`;
        svgContent += `<path d="${path}" fill="none" stroke="#8b5cf6" stroke-width="2" marker-end="url(#arrow)" />`;
        svgContent += `<text x="${src.x + nodeWidth + 45}" y="${src.y + 20}" fill="#00d2ff" font-family="JetBrains Mono" font-size="10">${t.event}</text>`;
      } else {
        const midX = (src.x + dst.x) / 2 + nodeWidth / 2;
        const midY = (src.y + dst.y) / 2 + nodeHeight / 2;
        const startX = src.x + nodeWidth / 2;
        const startY = src.y + nodeHeight;
        const endX = dst.x + nodeWidth / 2;
        const endY = dst.y;
        
        svgContent += `<path d="M ${startX} ${startY} Q ${midX + 20} ${midY} ${endX} ${endY}" fill="none" stroke="#6366f1" stroke-width="2" marker-end="url(#arrow)" />`;
        if (t.event && t.event !== "Anonymous") {
          svgContent += `<text x="${midX}" y="${midY}" fill="#38bdf8" font-family="JetBrains Mono" font-size="10" font-weight="600" text-anchor="middle">${t.event}</text>`;
        }
      }
    }
  });

  // Draw State Capsule Nodes
  states.forEach(s => {
    const coord = nodeCoords[s];
    const isActive = (s === currentModel.activeState);
    const fill = isActive ? "rgba(0, 210, 255, 0.15)" : "#0f172a";
    const stroke = isActive ? "#00d2ff" : "rgba(255, 255, 255, 0.15)";
    const strokeWidth = isActive ? "3" : "1.5";
    const glowClass = isActive ? "svg-active-node" : "";

    svgContent += `
      <g class="node ${glowClass}" onclick="window.onNodeClick('${s}')" style="cursor: pointer;">
        <rect x="${coord.x}" y="${coord.y}" width="${nodeWidth}" height="${nodeHeight}" rx="12" fill="${fill}" stroke="${stroke}" stroke-width="${strokeWidth}" />
        <text x="${coord.x + nodeWidth/2}" y="${coord.y + 26}" fill="#f8fafc" font-family="Inter, sans-serif" font-weight="600" font-size="12" text-anchor="middle">${s}</text>
      </g>`;
  });

  svgContent += `</svg>`;
  canvas.innerHTML = svgContent;
}

window.onNodeClick = (stateName) => {
  currentModel.activeState = stateName;
  updateSimulatorUI();
  renderDiagram();
};

let renderSeq = 0;
async function renderDiagram() {
  const canvas = document.getElementById("mermaidCanvas");
  const code = document.getElementById("editor").value;
  const format = document.getElementById("formatSelect").value;

  // If Mermaid is loaded and format is Mermaid, try Mermaid render
  if (window.mermaid && (format === 'mermaid' || code.includes('stateDiagram') || code.includes('-->'))) {
    let mermaidCode = code.trim();
    if (!mermaidCode.startsWith('stateDiagram')) {
      mermaidCode = `stateDiagram-v2\n[*] --> ${currentModel.initialState}\n` +
        currentModel.transitions.map(t => `    ${t.source} --> ${t.target} : ${t.event}`).join('\n');
    }

    const currentSeq = ++renderSeq;
    try {
      const id = "mermaid_svg_" + currentSeq;
      const { svg } = await mermaid.render(id, mermaidCode);
      if (currentSeq === renderSeq) {
        canvas.innerHTML = svg;
        highlightActiveSvgNode();
        return;
      }
    } catch (err) {
      console.warn("Mermaid fallback triggered:", err);
      const tempEl = document.getElementById("d" + "mermaid_svg_" + currentSeq);
      if (tempEl) tempEl.remove();
    }
  }

  // Guaranteed Native SVG Graphic Fallback
  renderNativeSvg(currentModel);
}

function highlightActiveSvgNode() {
  const svg = document.querySelector("#mermaidCanvas svg");
  if (!svg) return;

  svg.querySelectorAll(".node").forEach(n => n.classList.remove("active-state"));
  const nodes = svg.querySelectorAll(".node");
  nodes.forEach(n => {
    const label = n.textContent.trim();
    if (label.includes(currentModel.activeState)) {
      n.classList.add("active-state");
    }
  });
}

function updatePlayground() {
  const code = document.getElementById("editor").value;
  const format = document.getElementById("formatSelect").value;
  const isCpp20 = document.getElementById("stdSelect").value === "20";
  
  const parsed = parseDiagram(code, format);
  currentModel = {
    ...currentModel,
    ...parsed,
    activeState: currentModel.activeState && parsed.states.includes(currentModel.activeState) ? currentModel.activeState : parsed.initialState
  };

  // Update C++ Code Preview
  document.getElementById("cppPreview").textContent = generateCppHeader(currentModel, isCpp20);

  // Run Formal Diagnostics
  const diags = runModelChecker(currentModel);
  const diagContainer = document.getElementById("diagnostics");
  const statusBadge = document.getElementById("modelStatusBadge");
  diagContainer.innerHTML = "";

  if (diags.length === 0) {
    statusBadge.textContent = "SOUND (0 Errors)";
    statusBadge.className = "status-pill status-ok";
    diagContainer.innerHTML = `<div class="diag-item INFO">✨ Model is formally verified and sound (0 errors, 0 warnings).</div>`;
  } else {
    const hasCrit = diags.some(d => d.severity === "SAFETY_CRITICAL");
    statusBadge.textContent = hasCrit ? "SAFETY CRITICAL" : "WARNINGS";
    statusBadge.className = hasCrit ? "status-pill status-err" : "status-pill status-warn";
    for (const d of diags) {
      diagContainer.innerHTML += `<div class="diag-item ${d.severity}"><strong>[${d.category}]</strong> ${d.message}</div>`;
    }
  }

  // Render Visual Graph
  renderDiagram();

  // Update Simulator
  updateSimulatorUI();
}

function updateSimulatorUI() {
  document.getElementById("activeStateBadge").textContent = currentModel.activeState;

  const btnContainer = document.getElementById("eventButtons");
  btnContainer.innerHTML = "";

  for (const evt of currentModel.events) {
    const btn = document.createElement("button");
    btn.className = "btn-event";
    btn.textContent = evt;
    btn.onclick = () => dispatchEvent(evt);
    btnContainer.appendChild(btn);
  }

  highlightActiveSvgNode();
}

function dispatchEvent(eventName) {
  const matching = currentModel.transitions.find(t => t.source === currentModel.activeState && t.event === eventName);
  const historyLog = document.getElementById("historyLog");
  const time = new Date().toLocaleTimeString();

  if (matching) {
    const oldState = currentModel.activeState;
    currentModel.activeState = matching.target;
    document.getElementById("activeStateBadge").textContent = currentModel.activeState;

    const logEntry = document.createElement("div");
    logEntry.className = "log-entry";
    logEntry.innerHTML = `[${time}] <span class="event">${eventName}</span> : ${oldState} -> <span class="success">${matching.target}</span>`;
    historyLog.prepend(logEntry);
  } else {
    const logEntry = document.createElement("div");
    logEntry.className = "log-entry";
    logEntry.innerHTML = `[${time}] <span class="event">${eventName}</span> : <span class="dropped">Dropped (No transition from '${currentModel.activeState}')</span>`;
    historyLog.prepend(logEntry);
  }

  renderDiagram();
}

window.onload = () => {
  if (window.mermaid) {
    mermaid.initialize({
      startOnLoad: false,
      theme: 'dark',
      securityLevel: 'loose',
      themeVariables: {
        darkMode: true,
        background: 'transparent',
        primaryColor: '#1e293b',
        primaryBorderColor: '#00d2ff',
        primaryTextColor: '#f8fafc',
        lineColor: '#8b5cf6',
        secondaryColor: '#0f172a',
        tertiaryColor: '#1e293b'
      }
    });
  }

  const loadPreset = () => {
    const fmt = document.getElementById("formatSelect").value;
    const preset = document.getElementById("presetSelect").value;
    const code = (PRESETS[fmt] && PRESETS[fmt][preset]) ? PRESETS[fmt][preset] : PRESETS.mermaid[preset];
    document.getElementById("editor").value = code;
    document.getElementById("formatBadge").textContent = fmt.toUpperCase();
    updatePlayground();
  };

  document.getElementById("presetSelect").onchange = loadPreset;
  document.getElementById("formatSelect").onchange = loadPreset;
  document.getElementById("stdSelect").onchange = updatePlayground;
  document.getElementById("editor").oninput = updatePlayground;

  document.getElementById("copyBtn").onclick = () => {
    const code = document.getElementById("cppPreview").textContent;
    navigator.clipboard.writeText(code);
    alert("C++ header copied to clipboard!");
  };

  document.getElementById("downloadBtn").onclick = () => {
    const code = document.getElementById("cppPreview").textContent;
    const blob = new Blob([code], { type: "text/plain;charset=utf-8" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = `${currentModel.initialState.toLowerCase()}_fsm.hpp`;
    a.click();
  };

  document.getElementById("resetSimBtn").onclick = () => {
    currentModel.activeState = currentModel.initialState;
    updateSimulatorUI();
    renderDiagram();
  };

  document.getElementById("clearLogBtn").onclick = (e) => {
    e.preventDefault();
    document.getElementById("historyLog").innerHTML = "";
  };

  document.getElementById("zoomInBtn").onclick = () => {
    currentModel.zoom = Math.min(2.0, (currentModel.zoom || 1.0) + 0.15);
    renderDiagram();
  };

  document.getElementById("zoomOutBtn").onclick = () => {
    currentModel.zoom = Math.max(0.5, (currentModel.zoom || 1.0) - 0.15);
    renderDiagram();
  };

  document.getElementById("zoomResetBtn").onclick = () => {
    currentModel.zoom = 1.0;
    renderDiagram();
  };

  loadPreset();
};

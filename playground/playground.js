/**
 * fsmc Web Playground & Live HFSM Simulator
 * ============================================================================
 * Zero-Overhead C++ State Machine Compiler & Visual Engineering Suite
 * Powered by fsmc Universal AST Parser & Dynamic Multi-Format Serializers
 * ============================================================================
 */

// ----------------------------------------------------------------------------
// 1. Canonical State Machine Presets (Single-Source AST Source)
// ----------------------------------------------------------------------------

const CANONICAL_PRESETS = {
  connection_manager: `@startuml
[*] --> Disconnected

Disconnected --> Connecting : ConnectCmd [HasNetworkGuard && HasValidCredentialsGuard] / InitSocketAction
Disconnected --> Disconnected : ConnectCmd [!HasNetworkGuard || !HasValidCredentialsGuard] / LogErrorAction
Connecting --> Connected : HandshakeOkEvent / SetupSessionAction
Connecting --> Disconnected : HandshakeFailedEvent / CleanupAction
Connecting --> Disconnected : TimeoutEvent / CleanupAction
Connected --> Suspended : NetworkDegradedEvent / PauseQueueAction
Connected --> Disconnected : DisconnectCmd / CloseSocketAction
Connected --> Disconnected : ConnectionLostEvent / CleanupAction
Suspended --> Connected : NetworkRestoredEvent / ResumeQueueAction
Suspended --> Disconnected : DisconnectCmd / CloseSocketAction
Suspended --> Disconnected : TimeoutEvent / CleanupAction
@enduml`,

  async_motor_controller: `@startuml
[*] --> Halted

Halted --> Accelerating : StartCmd [HasValidRpmGuard] / PowerOnInverterAction
Accelerating --> RunningAtSpeed : SpeedReachedEvent / EngageSpeedPidAction
RunningAtSpeed --> Decelerating : StopCmd / ApplyRegenerativeBrakeAction
Decelerating --> Halted : StoppedEvent / DisengageInverterAction
Accelerating --> Faulted : OvercurrentEvent / EmergencyCutoffAction
RunningAtSpeed --> Faulted : OvercurrentEvent / EmergencyCutoffAction
Decelerating --> Faulted : OvercurrentEvent / EmergencyCutoffAction
Faulted --> Halted : ResetFaultCmd [IsThermalSafeGuard] / ClearFaultFlagsAction
@enduml`,

  mission_controller: `@startuml
[*] --> Standby

Standby --> Ascending : LaunchCmd [ValidClearanceGuard] / ArmEnginesAction
Ascending --> Cruising : AltitudeReachedEvent / DeploySolarPanelsAction
Cruising --> Orbiting : OrbitInsertedEvent / StabilizeAttitudeAction
Orbiting --> Landing : ReturnHomeCmd / RetractPanelsAction
Landing --> MissionCompleted : TouchdownEvent / ShutdownSystemsAction
Ascending --> Aborted : AbortCmd / TriggerAlarmAction
Cruising --> Aborted : AbortCmd / TriggerAlarmAction
Orbiting --> Aborted : AbortCmd / TriggerAlarmAction
@enduml`,

  industrial_press: `@startuml
[*] --> Idle

Idle --> Initializing : PowerOnCmd [SafetyOk] / LogPowerOn
Initializing --> Ready : InitDone / StoreDiagnostics
Initializing --> Idle : AbortCmd / Cleanup
Ready --> Running : StartCmd [ToolLoaded] / EngageDrive
Running --> Paused : PauseCmd / HoldPosition
Paused --> Running : ResumeCmd [SafetyOk] / ReleaseHold
Running --> Faulted : EStopEvent / EmergencyBrake
Faulted --> Idle : ResetFaultCmd / ClearFault
Running --> Idle : StopCmd / DisengageDrive
@enduml`,

  sysml2_spacecraft: `state def SpacecraftController {
    initial state Standby;
    
    state Standby;
    state InFlight;
    state Landing;
    state MissionCompleted;
    state Aborted;

    transition from Standby accept AuthorizeCmd if ValidClearanceGuard then InFlight;
    transition from InFlight accept ReturnHomeCmd then Landing;
    transition from Landing accept TouchdownEvent then MissionCompleted;
    transition from InFlight accept EmergencyAbortCmd then Aborted;
}`
};

// ----------------------------------------------------------------------------
// 2. WebAssembly Bridge Initialization (fsmc Compiler Engine)
// ----------------------------------------------------------------------------

let fsmcModule = null;
let wasmInitPromise = null;

function initWasm() {
  if (!wasmInitPromise) {
    wasmInitPromise = (async () => {
      if (typeof createFsmcModule === 'function') {
        try {
          fsmcModule = await createFsmcModule();
          if (typeof window !== 'undefined') window.fsmcModule = fsmcModule;
          return fsmcModule;
        } catch (err) {
          console.warn("fsmc WASM initialization notice (using JS engine):", err);
        }
      } else if (typeof Module !== 'undefined') {
        if (Module._malloc || Module.compile) {
          fsmcModule = Module;
          return fsmcModule;
        }
        return new Promise(resolve => {
          Module.onRuntimeInitialized = function() {
            fsmcModule = Module;
            if (typeof window !== 'undefined') window.fsmcModule = Module;
            resolve(fsmcModule);
          };
        });
      }
      return null;
    })();
  }
  return wasmInitPromise;
}

// Start loading immediately in background
initWasm();

// ----------------------------------------------------------------------------
// 3. Universal Dynamic Model Manager & Multi-Format Transpiler
// ----------------------------------------------------------------------------

function unescapeXml(str) {
  return (str || '').replace(/&amp;/g, '&').replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&apos;/g, "'");
}

const ModelManager = {
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
    const t = (text || "").trim();
    if (t.includes('<scxml') || t.includes('xmlns="http://www.w3.org/2005/07/scxml"')) return 'scxml';
    if (t.includes('<xmi:') || t.includes('<uml:') || t.includes('<packagedElement')) return 'cameo';
    if (t.includes('@startuml') || t.includes('@enduml')) return 'plantuml';
    if (t.includes('stateDiagram') || t.includes('stateDiagram-v2')) return 'mermaid';
    if (t.includes('state def ') || t.includes('transition from ')) return 'sysml2';
    if (t.includes('digraph ') || t.startsWith('digraph{') || t.includes('graph ')) return 'dot';
    if (t.startsWith('{') || (t.includes('"states"') && t.includes('"id"'))) return 'json';
    return 'plantuml';
  },

  parse(text, format) {
    const fmt = format || this.detectFormat(text);

    // 1. Try C++ WebAssembly parser if loaded
    if (fsmcModule && fsmcModule.getModel && text && text.trim()) {
      try {
        const res = JSON.parse(fsmcModule.getModel(text, fmt));
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
            initialState: res.initialState || statesArr[0].name
          };
        }
      } catch (e) {
        console.warn("WASM getModel notice:", e);
      }
    }

    // 2. High-Fidelity Universal JavaScript AST Parser
    return this.fallbackParse(text, fmt);
  },

  fallbackParse(text, format) {
    const fmt = format || this.detectFormat(text);
    const states = new Set();
    const stateDetails = [];
    const events = new Set();
    const transitions = [];
    let initial = "";
    let name = "GeneratedFSM";

    // Format: W3C SCXML
    if (fmt === 'scxml') {
      const nameMatch = text.match(/name="([^"]+)"/);
      if (nameMatch) name = nameMatch[1];
      const initMatch = text.match(/initial="([^"]+)"/);
      if (initMatch) initial = initMatch[1];

      // Pass 1: Extract all explicitly defined states in exact document order
      const stateHeaderRegex = /<state\s+([^>]*?)(\/?>|>)/g;
      let sh;
      while ((sh = stateHeaderRegex.exec(text)) !== null) {
        const idMatch = sh[1].match(/id="([^"]+)"/);
        if (idMatch) {
          const sId = idMatch[1];
          if (!states.has(sId)) {
            states.add(sId);
            stateDetails.push({ name: sId, parent: "", is_composite: false });
          }
        }
      }

      // Pass 2: Extract transitions
      const stateBlocks = text.matchAll(/<state\s+([^>]*?)>([\s\S]*?)<\/state>/g);
      for (const sb of stateBlocks) {
        const stateAttrs = sb[1];
        const stateBody = sb[2];
        const idMatch = stateAttrs.match(/id="([^"]+)"/);
        if (!idMatch) continue;
        const srcId = idMatch[1];

        const transTags = stateBody.matchAll(/<transition\s+([^>]*?)\/?>/g);
        for (const tm of transTags) {
          const tAttrs = tm[1];
          const evtM = tAttrs.match(/event="([^"]+)"/);
          const condM = tAttrs.match(/cond="([^"]*)"/);
          const targetM = tAttrs.match(/target="([^"]+)"/);
          const actM = tAttrs.match(/action="([^"]*)"/) || tAttrs.match(/effect="([^"]*)"/);

          const evt = evtM ? evtM[1] : "Anonymous";
          const cond = condM ? condM[1].replace(/&amp;/g, '&').replace(/&lt;/g, '<').replace(/&gt;/g, '>') : "";
          const dst = targetM ? targetM[1] : srcId;
          const act = actM ? actM[1] : "";

          if (evt !== "Anonymous") events.add(evt);
          if (dst && !states.has(dst)) {
            states.add(dst);
            stateDetails.push({ name: dst, parent: "", is_composite: false });
          }
          transitions.push({ source: srcId, target: dst, event: evt, guard: cond, action: act, is_internal: (srcId === dst) });
        }
      }
    }

    // Format: Cameo / MagicDraw (OMG XMI)
    else if (fmt === 'cameo') {
      const stateMap = new Map();
      const stateMatches = text.matchAll(/<subvertex\s+([^>]*?)\/?>/g);
      for (const m of stateMatches) {
        const attrs = m[1];
        const idMatch = attrs.match(/xmi:id="([^"]+)"/);
        const nameMatch = attrs.match(/name="([^"]+)"/);
        const typeMatch = attrs.match(/xmi:type="([^"]+)"/);
        if (idMatch && nameMatch && (!typeMatch || typeMatch[1].includes("State"))) {
          stateMap.set(idMatch[1], nameMatch[1]);
          states.add(nameMatch[1]);
          stateDetails.push({ name: nameMatch[1], parent: "", is_composite: false });
        }
      }

      const initMatch = text.match(/<transition\s+[^>]*source="[^"]*ps[^"]*"\s+target="([^"]+)"/);
      if (initMatch && stateMap.has(initMatch[1])) {
        initial = stateMap.get(initMatch[1]);
      }

      const transMatches = text.matchAll(/<transition\s+([^>]*?)\/?>/g);
      for (const t of transMatches) {
        const attrs = t[1];
        const srcM = attrs.match(/source="([^"]+)"/);
        const dstM = attrs.match(/target="([^"]+)"/);
        const trigM = attrs.match(/trigger="([^"]*)"/);
        const guardM = attrs.match(/guard="([^"]*)"/);
        const effM = attrs.match(/effect="([^"]*)"/) || attrs.match(/action="([^"]*)"/);

        if (!srcM || !dstM) continue;
        const src = stateMap.get(srcM[1]) || srcM[1];
        const dst = stateMap.get(dstM[1]) || dstM[1];
        const evt = trigM ? trigM[1] : "Anonymous";
        const guard = guardM ? unescapeXml(guardM[1]) : "";
        const act = effM ? unescapeXml(effM[1]) : "";

        if (src.includes('ps') || src.includes('initial')) {
          initial = dst;
          continue;
        }
        if (evt !== "Anonymous") events.add(evt);
        transitions.push({ source: src, target: dst, event: evt, guard: guard, action: act, is_internal: (src === dst) });
      }
    }

    // Format: XState JSON
    else if (fmt === 'json') {
      try {
        const obj = JSON.parse(text);
        name = obj.id || "GeneratedFSM";
        initial = obj.initial || "";
        if (obj.states) {
          for (const [stName, stObj] of Object.entries(obj.states)) {
            states.add(stName);
            stateDetails.push({ name: stName, parent: "", is_composite: false });
            if (stObj.on) {
              for (const [evtName, targetVal] of Object.entries(stObj.on)) {
                events.add(evtName);
                if (Array.isArray(targetVal)) {
                  for (const item of targetVal) {
                    const targetState = typeof item === 'string' ? item : item.target;
                    states.add(targetState);
                    transitions.push({
                      source: stName,
                      target: targetState,
                      event: evtName,
                      guard: item.guard || item.cond || "",
                      action: item.action || item.actions || "",
                      is_internal: (stName === targetState)
                    });
                  }
                } else if (typeof targetVal === 'string') {
                  states.add(targetVal);
                  transitions.push({ source: stName, target: targetVal, event: evtName, guard: "", action: "", is_internal: (stName === targetVal) });
                } else if (typeof targetVal === 'object' && targetVal.target) {
                  states.add(targetVal.target);
                  transitions.push({
                    source: stName,
                    target: targetVal.target,
                    event: evtName,
                    guard: targetVal.guard || targetVal.cond || "",
                    action: targetVal.action || targetVal.actions || "",
                    is_internal: (stName === targetVal.target)
                  });
                }
              }
            }
          }
        }
      } catch (e) {}
    }

    // Format: Graphviz DOT
    else if (fmt === 'dot') {
      const nameMatch = text.match(/digraph\s+([A-Za-z0-9_]+)/);
      if (nameMatch) name = nameMatch[1];
      const lines = text.split('\n');
      for (const raw of lines) {
        const line = raw.trim();
        if (line.includes('->')) {
          const parts = line.split('->');
          const src = parts[0].replace(/;/g, '').trim();
          const rest = parts[1].split(';')[0].trim();
          const dst = rest.split('[')[0].trim();

          let evt = "Anonymous";
          let guard = "";
          let action = "";

          const lblMatch = line.match(/label="([^"]+)"/);
          if (lblMatch) {
            const rawLbl = lblMatch[1];
            let evPart = rawLbl.split('[')[0].split('/')[0].trim();
            if (evPart) evt = evPart;
            const gMatch = rawLbl.match(/\[([^\]]+)\]/);
            if (gMatch) guard = gMatch[1];
            const aMatch = rawLbl.match(/\/([^"]+)/);
            if (aMatch) action = aMatch[1].trim();
          }

          if (src === '__start__' || src === '[*]') {
            initial = dst;
          } else {
            states.add(src);
            states.add(dst);
            if (evt !== "Anonymous") events.add(evt);
            transitions.push({ source: src, target: dst, event: evt, guard: guard, action: action, is_internal: (src === dst) });
          }
        }
      }
    }

    // Format: OMG SysML v2
    else if (fmt === 'sysml2') {
      const nameMatch = text.match(/state\s+def\s+([A-Za-z0-9_]+)/);
      if (nameMatch) name = nameMatch[1];
      const lines = text.split('\n');
      for (const raw of lines) {
        const line = raw.trim();
        if (line.startsWith("state def") || !line) continue;

        const stateDecl = line.match(/^(?:initial\s+)?state\s+([A-Za-z0-9_]+);?/);
        if (stateDecl && stateDecl[1] !== "def") {
          const stName = stateDecl[1];
          states.add(stName);
          stateDetails.push({ name: stName, parent: "", is_composite: false });
          if (line.includes('initial')) initial = stName;
        }

        if (line.startsWith("transition")) {
          const fromMatch = line.match(/from\s+([A-Za-z0-9_]+)/);
          const acceptMatch = line.match(/accept\s+([A-Za-z0-9_]+)/);
          const ifMatch = line.match(/\sif\s+(.+?)(?=\sdo\s|\sthen\s|;|$)/);
          const doMatch = line.match(/\sdo\s+(.+?)(?=\sthen\s|;|$)/);
          const thenMatch = line.match(/then\s+([A-Za-z0-9_]+)/);

          if (fromMatch && thenMatch) {
            const src = fromMatch[1];
            const dst = thenMatch[1];
            const evt = acceptMatch ? acceptMatch[1] : "Anonymous";
            const guard = ifMatch ? ifMatch[1].trim() : "";
            const action = doMatch ? doMatch[1].trim() : "";
            states.add(src);
            states.add(dst);
            if (evt !== "Anonymous") events.add(evt);
            transitions.push({ source: src, target: dst, event: evt, guard: guard, action: action, is_internal: (src === dst) });
          }
        }
      }
    }

    // Format: PlantUML / Mermaid
    else {
      const lines = text.split('\n');
      for (const raw of lines) {
        const line = raw.trim();
        if (!line || line.startsWith('@') || line.startsWith('stateDiagram')) continue;

        if (line.startsWith('state ') && !line.includes('-->')) {
          const stName = line.replace('state ', '').split('{')[0].split('[')[0].trim();
          if (stName) {
            states.add(stName);
            stateDetails.push({ name: stName, parent: "", is_composite: line.includes('{') });
          }
        }

        if (line.includes('-->')) {
          const parts = line.split('-->');
          const src = parts[0].trim();
          const rest = parts[1].trim();
          const dst = rest.split(':')[0].trim().replace(/\[H\*?\]/g, '');
          let evt = "Anonymous";
          let guard = "";
          let action = "";

          if (rest.includes(':')) {
            const rawLabel = rest.split(':')[1].trim();
            evt = rawLabel.split('[')[0].split('/')[0].trim() || "Anonymous";
            const gMatch = rawLabel.match(/\[([^\]]+)\]/);
            if (gMatch) guard = gMatch[1];
            const aMatch = rawLabel.match(/\/([^\n]+)/);
            if (aMatch) action = aMatch[1].trim();
          }

          if (src === '[*]' && dst) {
            initial = dst;
          } else {
            if (src && src !== '[*]') states.add(src);
            if (dst && dst !== '[*]') states.add(dst);
            if (evt && evt !== 'Anonymous') events.add(evt);
            transitions.push({ source: src, target: dst, event: evt, guard: guard, action: action, is_internal: (src === dst) });
          }
        }
      }
    }

    const stateList = Array.from(states);
    if (!initial && stateList.length > 0) {
      initial = stateList[0];
    }

    return {
      name: name,
      states: stateList,
      stateDetails: stateDetails.length > 0 ? stateDetails : stateList.map(s => ({ name: s, parent: "", is_composite: false })),
      events: Array.from(events),
      transitions: transitions,
      initialState: initial || "Disconnected"
    };
  },

  serialize(model, toFormat) {
    const fsmName = model.name || "GeneratedFSM";

    // 1. Mermaid (stateDiagram-v2)
    if (toFormat === 'mermaid') {
      let out = "stateDiagram-v2\n";
      if (model.initialState) out += `    [*] --> ${model.initialState}\n`;
      for (const t of model.transitions) {
        let label = t.event || "";
        if (t.guard) label += ` [${t.guard}]`;
        if (t.action) label += ` / ${t.action}`;
        const lblStr = label && label !== "Anonymous" ? ` : ${label}` : "";
        out += `    ${t.source} --> ${t.is_internal ? t.source : t.target}${lblStr}\n`;
      }
      return out.trim();
    }

    // 2. PlantUML (@startuml)
    if (toFormat === 'plantuml') {
      let out = "@startuml\n";
      if (model.initialState) out += `[*] --> ${model.initialState}\n\n`;
      for (const t of model.transitions) {
        let label = t.event || "";
        if (t.guard) label += ` [${t.guard}]`;
        if (t.action) label += ` / ${t.action}`;
        const lblStr = label && label !== "Anonymous" ? ` : ${label}` : "";
        out += `${t.source} --> ${t.is_internal ? t.source : t.target}${lblStr}\n`;
      }
      out += "@enduml";
      return out;
    }

    // 3. OMG SysML v2
    if (toFormat === 'sysml2') {
      let out = `state def ${fsmName} {\n`;
      if (model.initialState) out += `    initial state ${model.initialState};\n\n`;
      for (const s of model.states) {
        out += `    state ${s};\n`;
      }
      out += "\n";
      for (const t of model.transitions) {
        out += `    transition from ${t.source}`;
        if (t.event && t.event !== "Anonymous") out += ` accept ${t.event}`;
        if (t.guard) out += ` if ${t.guard}`;
        if (t.action) out += ` do ${t.action}`;
        out += ` then ${t.is_internal ? t.source : t.target};\n`;
      }
      out += "}";
      return out;
    }

    // 4. W3C SCXML
    if (toFormat === 'scxml') {
      let out = `<?xml version="1.0" encoding="UTF-8"?>\n`;
      out += `<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="${model.initialState}" name="${fsmName}">\n`;
      for (const s of model.states) {
        const transFromS = model.transitions.filter(t => t.source === s);
        if (transFromS.length === 0) {
          out += `  <state id="${s}"/>\n`;
        } else {
          out += `  <state id="${s}">\n`;
          for (const t of transFromS) {
            out += `    <transition`;
            if (t.event && t.event !== "Anonymous") out += ` event="${t.event}"`;
            if (t.guard) out += ` cond="${t.guard.replace(/&/g, '&amp;')}"`;
            if (t.action) out += ` action="${t.action}"`;
            if (t.target && !t.is_internal) out += ` target="${t.target}"`;
            out += `/>\n`;
          }
          out += `  </state>\n`;
        }
      }
      out += `</scxml>`;
      return out;
    }

    // 5. Cameo / MagicDraw (OMG XMI 2.1)
    if (toFormat === 'cameo') {
      let out = `<?xml version="1.0" encoding="UTF-8"?>\n`;
      out += `<xmi:XMI xmi:version="2.1" xmlns:uml="http://www.omg.org/spec/UML/20090901" xmlns:xmi="http://schema.omg.org/spec/XMI/2.1">\n`;
      out += `  <uml:Model xmi:id="_m1" name="${fsmName}Model">\n`;
      out += `    <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm1" name="${fsmName}">\n`;
      out += `      <region xmi:id="_r1">\n`;
      out += `        <subvertex xmi:type="uml:Pseudostate" xmi:id="_ps1" kind="initial"/>\n`;
      let sIdx = 1;
      const idMap = new Map();
      for (const s of model.states) {
        const id = `_s${sIdx++}`;
        idMap.set(s, id);
        out += `        <subvertex xmi:type="uml:State" xmi:id="${id}" name="${s}"/>\n`;
      }
      if (model.initialState && idMap.has(model.initialState)) {
        out += `        <transition xmi:id="_t0" source="_ps1" target="${idMap.get(model.initialState)}"/>\n`;
      }
      let tIdx = 1;
      for (const t of model.transitions) {
        const srcId = idMap.get(t.source) || t.source;
        const dstId = idMap.get(t.target) || t.target;
        out += `        <transition xmi:id="_t${tIdx++}" source="${srcId}" target="${dstId}"`;
        if (t.event && t.event !== "Anonymous") out += ` trigger="${t.event}"`;
        if (t.guard) out += ` guard="${t.guard.replace(/&/g, '&amp;')}"`;
        if (t.action) out += ` effect="${t.action}"`;
        out += `/>\n`;
      }
      out += `      </region>\n    </packagedElement>\n  </uml:Model>\n</xmi:XMI>`;
      return out;
    }

    // 6. XState JSON
    if (toFormat === 'json') {
      const obj = {
        id: fsmName,
        initial: model.initialState,
        states: {}
      };
      for (const s of model.states) {
        obj.states[s] = { on: {} };
      }
      for (const t of model.transitions) {
        if (!obj.states[t.source]) obj.states[t.source] = { on: {} };
        const transObj = { target: t.is_internal ? t.source : t.target };
        if (t.guard) transObj.guard = t.guard;
        if (t.action) transObj.action = t.action;
        const evtKey = t.event || "Anonymous";
        const existing = obj.states[t.source].on[evtKey];
        if (existing) {
          if (Array.isArray(existing)) {
            existing.push(transObj);
          } else {
            obj.states[t.source].on[evtKey] = [existing, transObj];
          }
        } else {
          obj.states[t.source].on[evtKey] = transObj;
        }
      }
      return JSON.stringify(obj, null, 2);
    }

    // 7. Graphviz DOT
    if (toFormat === 'dot') {
      let out = `digraph ${fsmName} {\n`;
      out += `    __start__ [shape=point];\n`;
      if (model.initialState) out += `    __start__ -> ${model.initialState};\n`;
      for (const t of model.transitions) {
        let label = t.event || "";
        if (t.guard) label += ` [${t.guard}]`;
        if (t.action) label += ` / ${t.action}`;
        const lblStr = label && label !== "Anonymous" ? ` [label="${label}"]` : "";
        out += `    ${t.source} -> ${t.is_internal ? t.source : t.target}${lblStr};\n`;
      }
      out += `}`;
      return out;
    }

    return this.serialize(model, 'plantuml');
  },

  export(source, fromFormat, toFormat) {
    if (fromFormat === toFormat) return source;

    // 1. Try C++ WebAssembly transpile
    if (fsmcModule && fsmcModule.exportDiagram && source && source.trim()) {
      try {
        const exported = fsmcModule.exportDiagram(source, fromFormat, toFormat);
        if (exported && !exported.startsWith("// [FSMC ERROR]")) {
          return exported;
        }
      } catch (e) {
        console.warn("WASM exportDiagram notice:", e);
      }
    }

    // 2. Dynamic AST Parsing + Dynamic Serialization
    const model = this.parse(source, fromFormat);
    return this.serialize(model, toFormat);
  },

  generateCpp(source, format, isCpp20 = true, isStandalone = true) {
    // 1. Try C++ WebAssembly compiler
    if (fsmcModule && fsmcModule.compile && source && source.trim()) {
      try {
        const code = fsmcModule.compile(source, format, isCpp20 ? 20 : 17, isStandalone);
        if (code && !code.startsWith("// [FSMC ERROR]")) {
          return code;
        }
      } catch (e) {
        console.warn("WASM compile notice:", e);
      }
    }

    // 2. High-Fidelity C++ Code Generation from AST
    const model = this.parse(source, format);
    let rawName = model.name;
    if (!rawName || rawName === "GeneratedFSM" || rawName === "StateMachine") {
      const sel = typeof document !== 'undefined' ? document.getElementById("presetSelect") : null;
      const pKey = sel ? sel.value : "";
      if (pKey) {
        rawName = pKey.split('_').map(w => w.charAt(0).toUpperCase() + w.slice(1)).join('');
      } else {
        rawName = model.initialState || "IndustrialController";
      }
    }
    const fsmName = rawName.endsWith("FSM") ? rawName : rawName + "FSM";

    let code = `// ============================================================================\n`;
    code += `// Generated by fsmc v1.0.0 (The Universal State Machine Compiler)\n`;
    code += `// Target: C++${isCpp20 ? '20' : '17'} (${isStandalone ? 'Standalone 0-Deps' : 'Modular'})\n`;
    code += `// ============================================================================\n#pragma once\n\n`;

    code += `#include <iostream>\n#include <string_view>\n\n`;
    code += `namespace fsm_generated {\n\n`;

    // Events
    code += `// --- Event Trigger Definitions ---\n`;
    for (const evt of model.events) {
      code += `struct ${evt} {};\n`;
    }
    code += `\n`;

    // States Enum
    code += `// --- State Enumeration ---\n`;
    code += `enum class State {\n`;
    for (const st of model.states) {
      code += `    ${st},\n`;
    }
    code += `};\n\n`;

    // Collect all guards and actions
    const guards = new Set();
    const actions = new Set();
    for (const t of model.transitions) {
      if (t.guard) {
        const gTokens = t.guard.match(/[A-Za-z0-9_]+/g);
        if (gTokens) {
          for (const tok of gTokens) {
            if (tok !== "true" && tok !== "false" && tok !== "and" && tok !== "or" && tok !== "not") {
              guards.add(tok);
            }
          }
        }
      }
      if (t.action) {
        const aTokens = t.action.match(/[A-Za-z0-9_]+/g);
        if (aTokens) {
          for (const tok of aTokens) actions.add(tok);
        }
      }
    }

    // FSM Class
    code += `// --- Finite State Machine Implementation ---\n`;
    code += `class ${fsmName} {\n`;
    code += `public:\n`;
    code += `    constexpr ${fsmName}() noexcept : current_state_(State::${model.initialState}) {}\n`;
    code += `    virtual ~${fsmName}() = default;\n\n`;

    code += `    [[nodiscard]] constexpr State current_state() const noexcept { return current_state_; }\n`;
    code += `    [[nodiscard]] std::string_view current_state_name() const noexcept {\n`;
    code += `        switch (current_state_) {\n`;
    for (const st of model.states) {
      code += `            case State::${st}: return "${st}";\n`;
    }
    code += `        }\n        return "Unknown";\n    }\n\n`;

    // Guard Hook Overrides
    if (guards.size > 0) {
      code += `    // --- Guard Predicates (Override in derived class or lambda) ---\n`;
      for (const g of guards) {
        code += `    [[nodiscard]] virtual bool check_${g}() const noexcept { return true; }\n`;
      }
      code += `\n`;
    }

    // Action Hook Overrides
    if (actions.size > 0) {
      code += `    // --- Action Handlers (Override in derived class) ---\n`;
      for (const a of actions) {
        code += `    virtual void on_${a}() noexcept {}\n`;
      }
      code += `\n`;
    }

    // Dispatch methods
    code += `    // --- Event Dispatchers ---\n`;
    for (const evt of model.events) {
      code += `    bool dispatch(const ${evt}&) noexcept {\n`;
      code += `        switch (current_state_) {\n`;
      const transForEvt = model.transitions.filter(t => t.event === evt);
      for (const t of transForEvt) {
        code += `            case State::${t.source}: {\n`;
        if (t.guard) {
          let gCond = t.guard;
          const gTokens = t.guard.match(/[A-Za-z0-9_]+/g);
          if (gTokens) {
            for (const tok of gTokens) {
              if (guards.has(tok)) {
                gCond = gCond.replace(new RegExp(`\\b${tok}\\b`, 'g'), `check_${tok}()`);
              }
            }
          }
          code += `                if (!(${gCond})) return false;\n`;
        }
        if (t.action) {
          const aTokens = t.action.match(/[A-Za-z0-9_]+/g);
          if (aTokens) {
            for (const tok of aTokens) {
              if (actions.has(tok)) code += `                on_${tok}();\n`;
            }
          }
        }
        if (!t.is_internal && t.target) {
          code += `                current_state_ = State::${t.target};\n`;
        }
        code += `                return true;\n            }\n`;
      }
      code += `            default: return false;\n`;
      code += `        }\n    }\n\n`;
    }

    code += `private:\n    State current_state_;\n};\n\n} // namespace fsm_generated\n`;
    return code;
  },

  validate(source, format) {
    if (fsmcModule && fsmcModule.verify && source && source.trim()) {
      try {
        const res = JSON.parse(fsmcModule.verify(source, format));
        if (res && res.diagnostics) return res.diagnostics;
      } catch (e) {
        console.warn("WASM verify notice:", e);
      }
    }
    const model = this.parse(source, format);
    const diags = [];
    if (model.states.length === 0) {
      diags.push({ severity: "ERROR", category: "Parser", message: "No states could be parsed from the diagram specification." });
    }
    return diags;
  }
};

// ----------------------------------------------------------------------------
// 4. Unified Graph Renderer (Mermaid AST Renderer)
// ----------------------------------------------------------------------------

let renderSeq = 0;

const GraphRenderer = {
  buildCanonicalGraph(model) {
    let out = "stateDiagram-v2\n";
    if (model.initialState) {
      out += `    [*] --> ${model.initialState}\n`;
    }

    for (const st of model.states) {
      out += `    state ${st}\n`;
    }

    for (const t of model.transitions) {
      let label = t.event || "";
      if (t.guard) {
        let cleanGuard = t.guard
          .replace(/fsm::and_<(.+?)>/g, (m, p) => p.replace(/,/g, ' && '))
          .replace(/fsm::or_<(.+?)>/g, (m, p) => p.replace(/,/g, ' || '))
          .replace(/fsm::not_<(.+?)>/g, (m, p) => `!${p.trim()}`)
          .replace(/[<>]/g, '')
          .replace(/&amp;/g, '&');
        label += ` [${cleanGuard}]`;
      }
      if (t.action) label += ` / ${t.action}`;
      const lblStr = label && label !== "Anonymous" ? ` : ${label}` : "";
      out += `    ${t.source} --> ${t.is_internal ? t.source : t.target}${lblStr}\n`;
    }
    return out.trim();
  },

  async render(model, sourceCode, format) {
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
        }
      } catch (err) {
        console.warn("Mermaid layout notice:", err);
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
      const isLeafActive = label.includes(activeState);
      if (isLeafActive) {
        n.classList.add("active-state-node");
        n.style.stroke = "var(--accent-green)";
        n.style.strokeWidth = "3px";
        n.style.filter = "drop-shadow(0 0 8px rgba(16, 185, 129, 0.6))";
      } else {
        n.classList.remove("active-state-node");
        n.style.stroke = "";
        n.style.strokeWidth = "";
        n.style.filter = "";
      }
    });
  },

  attachHandlers() {
    const svg = document.querySelector("#mermaidCanvas svg");
    if (!svg) return;
    svg.querySelectorAll(".node").forEach(n => {
      n.style.cursor = "pointer";
      n.onclick = (e) => {
        e.stopPropagation();
        const text = n.textContent.trim();
        const found = ModelManager.currentModel.states.find(s => text.includes(s));
        if (found) {
          SimulatorController.setState(found);
        }
      };
    });
  }
};

// ----------------------------------------------------------------------------
// 5. Interactive Visual Viewport (Pan & Zoom)
// ----------------------------------------------------------------------------

const ViewportController = {
  init() {
    const canvas = document.getElementById("mermaidCanvas");
    let isDragging = false;
    let startX = 0;
    let startY = 0;

    canvas.onmousedown = (e) => {
      if (e.target.closest('.node')) return;
      isDragging = true;
      startX = e.clientX - ModelManager.currentModel.panX;
      startY = e.clientY - ModelManager.currentModel.panY;
      canvas.style.cursor = "grabbing";
    };

    window.onmousemove = (e) => {
      if (!isDragging) return;
      ModelManager.currentModel.panX = e.clientX - startX;
      ModelManager.currentModel.panY = e.clientY - startY;
      this.applyTransform();
    };

    window.onmouseup = () => {
      if (isDragging) {
        isDragging = false;
        canvas.style.cursor = "grab";
      }
    };

    canvas.onwheel = (e) => {
      e.preventDefault();
      const zoomFactor = e.deltaY < 0 ? 1.1 : 0.9;
      this.zoom(zoomFactor);
    };

    document.getElementById("zoomInBtn").onclick = () => this.zoom(1.15);
    document.getElementById("zoomOutBtn").onclick = () => this.zoom(0.85);
    document.getElementById("zoomResetBtn").onclick = () => this.reset();
  },

  zoom(factor) {
    ModelManager.currentModel.zoom = Math.max(0.2, Math.min(3.0, ModelManager.currentModel.zoom * factor));
    this.applyTransform();
    document.getElementById("zoomLevelBadge").textContent = `${Math.round(ModelManager.currentModel.zoom * 100)}%`;
  },

  reset() {
    ModelManager.currentModel.zoom = 1.0;
    ModelManager.currentModel.panX = 0;
    ModelManager.currentModel.panY = 0;
    this.applyTransform();
    document.getElementById("zoomLevelBadge").textContent = "100%";
  },

  applyTransform(smooth = false) {
    const svg = document.querySelector("#mermaidCanvas svg");
    if (svg) {
      svg.style.transition = smooth ? "transform 0.2s ease-out" : "none";
      svg.style.transform = `translate(${ModelManager.currentModel.panX}px, ${ModelManager.currentModel.panY}px) scale(${ModelManager.currentModel.zoom})`;
      svg.style.transformOrigin = "center center";
    }
  }
};

// ----------------------------------------------------------------------------
// 6. Hard Real-Time Simulation Engine
// ----------------------------------------------------------------------------

const SimulatorController = {
  log(msg, type = "INFO") {
    const logEl = document.getElementById("historyLog");
    const item = document.createElement("div");
    item.className = `log-item ${type}`;
    const time = new Date().toISOString().substring(11, 23);
    item.textContent = `[${time}] ${msg}`;
    logEl.appendChild(item);
    logEl.scrollTop = logEl.scrollHeight;
  },

  clearLog() {
    document.getElementById("historyLog").innerHTML = "";
  },

  setState(targetState, guard = "", action = "") {
    const prev = ModelManager.currentModel.activeState;
    ModelManager.currentModel.activeState = targetState;
    document.getElementById("activeStateBadge").textContent = targetState;
    GraphRenderer.highlightActive(targetState);
    let msg = `State override: ${prev} ➔ ${targetState}`;
    if (guard) msg += ` [guard: ${guard}]`;
    if (action) msg += ` ➔ Action: ${action}()`;
    this.log(msg, "EVENT");
    this.updateControls();
  },

  dispatch(eventName) {
    const curr = ModelManager.currentModel.activeState;
    const trans = ModelManager.currentModel.transitions.filter(t => t.source === curr && t.event === eventName);

    if (trans.length === 0) {
      this.log(`Event '${eventName}' unhandled in state '${curr}' (IGNORED)`, "WARN");
      return;
    }

    const t = trans[0];
    const cleanGuard = t.guard ? t.guard
      .replace(/fsm::and_<(.+?)>/g, (m, p) => p.replace(/,/g, ' && '))
      .replace(/fsm::or_<(.+?)>/g, (m, p) => p.replace(/,/g, ' || '))
      .replace(/fsm::not_<(.+?)>/g, (m, p) => `!${p.trim()}`)
      .replace(/[<>]/g, '') : "";

    if (t.is_internal) {
      let msg = `[${eventName}] Internal in '${curr}'`;
      if (cleanGuard) msg += ` [guard: ${cleanGuard}]`;
      if (t.action) msg += ` ➔ Action: ${t.action}()`;
      this.log(msg, "INFO");
    } else {
      const prev = ModelManager.currentModel.activeState;
      ModelManager.currentModel.activeState = t.target;
      document.getElementById("activeStateBadge").textContent = t.target;
      GraphRenderer.highlightActive(t.target);

      let msg = `[${eventName}] ${prev} ➔ ${t.target}`;
      if (cleanGuard) msg += ` [guard: ${cleanGuard}]`;
      if (t.action) msg += ` ➔ Action: ${t.action}()`;
      this.log(msg, "EVENT");
      this.updateControls();
    }
  },

  updateControls() {
    const container = document.getElementById("eventButtons");
    container.innerHTML = "";
    const curr = ModelManager.currentModel.activeState;
    const availableTrans = ModelManager.currentModel.transitions.filter(t => t.source === curr);

    for (const t of availableTrans) {
      const btn = document.createElement("button");
      btn.className = "btn-event active-trigger";
      btn.textContent = t.event;
      btn.title = `Trigger transition: ${t.source} -> ${t.target}`;
      btn.onclick = () => this.dispatch(t.event);
      container.appendChild(btn);
    }

    const otherEvents = ModelManager.currentModel.events.filter(e => !availableTrans.some(t => t.event === e));
    for (const evt of otherEvents) {
      const btn = document.createElement("button");
      btn.className = "btn-event disabled-trigger";
      btn.textContent = evt;
      btn.title = `Event ${evt} is not handled in state ${curr}`;
      btn.onclick = () => this.dispatch(evt);
      container.appendChild(btn);
    }
  }
};

// ----------------------------------------------------------------------------
// 7. Core Application Coordinator
// ----------------------------------------------------------------------------

const App = {
  async init() {
    if (window.mermaid) {
      mermaid.initialize({
        startOnLoad: false,
        theme: 'dark',
        securityLevel: 'loose',
        state: {
          nodeSpacing: 50,
          rankSpacing: 50,
          defaultRenderer: 'dagre-wrapper'
        }
      });
    }

    ViewportController.init();

    document.getElementById("editor").oninput = () => this.update();
    document.getElementById("presetSelect").onchange = () => this.loadPreset();
    document.getElementById("formatSelect").onchange = () => this.onFormatChange();
    document.getElementById("stdSelect").onchange = () => this.update();
    document.getElementById("clearLogBtn").onclick = (e) => {
      e.preventDefault();
      SimulatorController.clearLog();
    };
    document.getElementById("resetSimBtn").onclick = () => {
      SimulatorController.setState(ModelManager.currentModel.initialState);
    };

    document.getElementById("copyBtn").onclick = () => {
      const code = document.getElementById("cppPreview").textContent;
      navigator.clipboard.writeText(code).then(() => {
        const btn = document.getElementById("copyBtn");
        const originalText = btn.textContent;
        btn.textContent = "✓ Copied!";
        setTimeout(() => btn.textContent = originalText, 2000);
      });
    };

    document.getElementById("downloadBtn").onclick = () => {
      const code = document.getElementById("cppPreview").textContent;
      const blob = new Blob([code], { type: "text/plain;charset=utf-8" });
      const a = document.createElement("a");
      a.href = URL.createObjectURL(blob);
      a.download = "generated_fsm.hpp";
      a.click();
    };

    // Await WebAssembly initialization before initial render so the real C++ compiler runs on frame 0
    await initWasm();
    this.loadPreset();
  },

  loadPreset() {
    const fmt = document.getElementById("formatSelect").value;
    const presetKey = document.getElementById("presetSelect").value;
    const rawCanonical = CANONICAL_PRESETS[presetKey] || CANONICAL_PRESETS.connection_manager;
    const sourceFmt = presetKey === 'sysml2_spacecraft' ? 'sysml2' : 'plantuml';

    // Dynamically transpile preset to the chosen target format
    const text = ModelManager.export(rawCanonical, sourceFmt, fmt);

    document.getElementById("editor").value = text;
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

    // Dynamically transpile user editor content to the newly selected format
    const converted = ModelManager.export(currentCode, detectedFmt, newFmt);
    document.getElementById("editor").value = converted;
    document.getElementById("formatBadge").textContent = newFmt.toUpperCase();
    this.update();
  },

  update() {
    const code = document.getElementById("editor").value;
    let format = document.getElementById("formatSelect").value;
    const isCpp20 = document.getElementById("stdSelect").value === "20";

    const detected = ModelManager.detectFormat(code);
    if (detected && detected !== format) {
      format = detected;
      const sel = document.getElementById("formatSelect");
      if (sel && Array.from(sel.options).some(o => o.value === detected)) {
        sel.value = detected;
      }
      document.getElementById("formatBadge").textContent = detected.toUpperCase();
    }

    const parsed = ModelManager.parse(code, format);
    ModelManager.currentModel = {
      ...ModelManager.currentModel,
      ...parsed,
      activeState: ModelManager.currentModel.activeState && parsed.states.includes(ModelManager.currentModel.activeState) ? ModelManager.currentModel.activeState : parsed.initialState
    };

    // 1. Generate C++ Modular Preview
    document.getElementById("cppPreview").textContent = ModelManager.generateCpp(code, format, isCpp20, true);

    // 2. Model Diagnostics
    const diags = ModelManager.validate(code, format);
    const diagContainer = document.getElementById("diagnostics");
    const statusBadge = document.getElementById("modelStatusBadge");
    diagContainer.innerHTML = "";

    if (diags.length === 0) {
      statusBadge.textContent = "SOUND";
      statusBadge.className = "status-pill status-ok";
      diagContainer.innerHTML = `<div class="diag-item INFO">Model verified and sound (0 errors, 0 warnings).</div>`;
    } else {
      statusBadge.textContent = "ERRORS";
      statusBadge.className = "status-pill status-err";
      for (const d of diags) {
        const item = document.createElement("div");
        item.className = `diag-item ${d.severity}`;
        item.textContent = `[${d.severity}] ${d.category ? `(${d.category}) ` : ""}${d.message}`;
        diagContainer.appendChild(item);
      }
    }

    // 3. Render State Diagram Graph
    GraphRenderer.render(ModelManager.currentModel, code, format);
    SimulatorController.updateControls();
    document.getElementById("activeStateBadge").textContent = ModelManager.currentModel.activeState;
  }
};

if (typeof window !== 'undefined') {
  window.addEventListener("DOMContentLoaded", () => App.init());
}

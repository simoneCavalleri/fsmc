/**
 * fsmc Web Playground & Live HFSM Simulator
 * ============================================================================
 * Zero-Overhead C++ State Machine Compiler & Visual Engineering Suite
 * Bundled distribution built from playground/src/ modules.
 * Supports direct file:// local access without CORS errors.
 * ============================================================================
 */


// --- Module: src/presets.js ---

/**
 * fsmc Playground — Canonical Preset Sources
 * Each preset is a raw diagram string in its native format.
 */

const CANONICAL_PRESETS = {
  autonomous_uav_mission: `state def AutonomousUavMission {
    // Typed I/O Ports with Formal Range Contracts
    in port battery_percent : Real { assert constraint { self >= 0.0 and self <= 100.0; } }
    in port altitude_m : Real { assert constraint { self >= 0.0 and self <= 10000.0; } }
    in port gps_locked : Boolean;
    out port motor_armed : Boolean;
    out port camera_active : Boolean;

    // Internal Attribute Registers (z^-1 Memory)
    attribute waypoints_completed : Integer = 0;
    attribute retry_count : Integer = 0;

    // Native SysML v2 Strongly-Typed Signal Definitions
    item def EvTelemetry {
        attribute battery_mv : Integer;
        attribute altitude_cm : Integer;
        attribute gps_locked : Boolean;
    }

    item def EvWaypointCmd {
        attribute lat : Real;
        attribute lon : Real;
        attribute target_alt : Real;
    }

    event def EvTelemetryPing;
    event def EvCameraSnap;
    event def TakeoffCmd;
    event def PauseCmd;
    event def ResumeMissionCmd;
    event def AbortCmd;
    event def CalibrationOk;
    event def TargetAltitudeReached;
    event def AreaReached;
    event def NextSectorCmd;
    event def HomePointReached;
    event def TouchdownEvent;
    event def LowBatteryEvent;
    event def ShutdownCmd;

    entry; then Preflight;

    state Preflight {
        satisfy requirement REQ_UAV_PRE_01;
        entry; then SensorCalib;
        state SensorCalib;
        state SystemReady;

        transition calib_done
            first SensorCalib
            accept CalibrationOk
            do action { out.motor_armed = true; }
            then SystemReady;
    }

    state InFlight {
        satisfy requirement REQ_UAV_NAV_01;
        do action async_flight_stabilizer;

        entry; then Ascending;
        state Ascending;

        state Navigating {
            defer EvTelemetryPing;
            defer EvCameraSnap;

            entry; then WaypointNav;
            state WaypointNav;
            state SearchPattern;

            transition area_reached
                first WaypointNav
                accept AreaReached
                do action { out.camera_active = true; }
                then SearchPattern;

            transition next_sector
                first SearchPattern
                accept NextSectorCmd
                do action { reg.waypoints_completed = reg.waypoints_completed + 1; }
                then WaypointNav;
        }

        state HoverPause {
            satisfy requirement REQ_UAV_HOLD_01;
        }

        transition alt_reached
            first Ascending
            accept TargetAltitudeReached
            do StartNavigation
            then Navigating;

        transition pause_mission
            first Navigating
            accept PauseCmd
            do HoldPosition
            then HoverPause;

        transition resume_mission
            first HoverPause
            accept ResumeMissionCmd
            do ResumeNavigation
            then Navigating[H];
    }

    state FailSafe {
        satisfy requirement REQ_UAV_SAFE_01;
        entry; then ReturnToHome;
        state ReturnToHome;
        state SafeLanding;
        state Landed;

        transition home_reached
            first ReturnToHome
            accept HomePointReached
            do DescendMotors
            then SafeLanding;

        transition touch_down
            first SafeLanding
            accept TouchdownEvent
            do action { out.motor_armed = false; }
            then Landed;
    }

    state Terminal;

    transition takeoff
        first Preflight
        accept TakeoffCmd
        if in.gps_locked and in.battery_percent > 20.0
        do LaunchUav
        then InFlight;

    transition low_bat
        first InFlight
        if in.battery_percent <= 15.0
        do InitiateFailSafe
        then FailSafe;

    transition abort_flight
        first InFlight
        accept AbortCmd
        do InitiateFailSafe
        then FailSafe;

    transition power_off
        first FailSafe
        accept ShutdownCmd
        do PowerOff
        then Terminal;
}`,

  industrial_press: `<?xml version="1.0" encoding="UTF-8"?>
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Idle" name="IndustrialPressFSM">
  <datamodel>
    <data id="cycle_count" expr="0" type="uint32_t"/>
    <data id="hydraulic_pressure_psi" expr="2200" type="uint32_t"/>
    <data id="safety_curtain_active" expr="true" type="bool"/>
  </datamodel>

  <state id="Idle">
    <onentry>
      <send event="LogIdleStatus"/>
    </onentry>
    <transition event="PowerOnCmd" cond="SafetyOk" target="Initializing">
      <send event="LogPowerOn"/>
    </transition>
  </state>

  <state id="Initializing">
    <onentry>
      <assign location="cycle_count" expr="0"/>
      <send event="CalibrateSensors"/>
    </onentry>
    <onexit>
      <send event="DiagnosticsComplete"/>
    </onexit>
    <transition event="InitDone" target="Ready">
      <send event="StoreDiagnostics"/>
    </transition>
    <transition event="AbortCmd" target="Idle">
      <send event="Cleanup"/>
    </transition>
  </state>

  <state id="Ready">
    <transition event="StartCmd" cond="ToolLoaded" target="Running">
      <send event="EngageDrive"/>
    </transition>
  </state>

  <state id="Running">
    <onentry>
      <assign location="cycle_count" expr="cycle_count + 1"/>
      <send event="StartStrokeTimer"/>
    </onentry>
    <transition event="PauseCmd" target="Paused">
      <send event="HoldPosition"/>
    </transition>
    <transition event="EStopEvent" target="Faulted">
      <send event="EmergencyBrake"/>
    </transition>
    <transition event="StopCmd" target="Idle">
      <send event="DisengageDrive"/>
    </transition>
  </state>

  <state id="Paused">
    <transition event="ResumeCmd" cond="SafetyOk" target="Running">
      <send event="ReleaseHold"/>
    </transition>
    <transition event="AbortCmd" target="Idle">
      <send event="Cleanup"/>
    </transition>
  </state>

  <state id="Faulted">
    <onentry>
      <send event="TriggerSafetyAlarm"/>
    </onentry>
    <transition event="ResetFaultCmd" target="Idle">
      <send event="ClearFault"/>
    </transition>
  </state>
</scxml>`,

  connection_manager: `@startuml
' Formal Verification Specifications (LTL)
' @fsm:property name=AlwaysReconnect kind=Safety ltl="G (ConnectionLost -> F Connected)" req="REQ-NET-01" desc="Guarantees eventual reconnection"
' @fsm:property name=NoDirectConnected kind=Invariant ltl="G (! (Disconnected && Connected))" req="REQ-NET-02" desc="Mutual exclusion between disconnected and connected"

' Typed Ports with Range Contracts
' @fsm:port name=in_latency_ms dir=in type=float min=0.0 max=5000.0
' @fsm:port name=out_socket_open dir=out type=bool

' Internal Registers (z^-1 Memory)
' @fsm:var name=retry_count type=uint32_t init=0 min=0 max=10 desc="Connection attempt counter"

' Strongly-Typed Signals & Payloads
' @fsm:signal ConnectCmd{uint32_t timeout_ms, bool use_tls} validator="timeout_ms > 0"
' @fsm:signal EvTelemetryPing

[*] --> Disconnected

state Disconnected {
  Disconnected : entry / ResetRetryCount
  Disconnected : exit / PrepareNetworkInterface
}

Disconnected --> Connecting : ConnectCmd [HasNetworkGuard && HasValidCredentialsGuard] / InitSocketAction
Disconnected --> Disconnected : ConnectCmd [!HasNetworkGuard || !HasValidCredentialsGuard] / LogErrorAction

state Connecting {
  Connecting : entry / StartConnectTimer
  Connecting : defer EvTelemetryPing
  Connecting : exit / StopConnectTimer
}

Connecting --> Connected : HandshakeOkEvent / SetupSessionAction
Connecting --> Disconnected : HandshakeFailedEvent / CleanupAction
Connecting --> Disconnected : TimeoutEvent / CleanupAction

state Connected {
  Connected : entry / EnableKeepAlive
  Connected : exit / FlushSendQueue
}

Connected --> Suspended : NetworkDegradedEvent / PauseQueueAction
Connected --> Disconnected : DisconnectCmd / CloseSocketAction
Connected --> Disconnected : ConnectionLostEvent / CleanupAction

state Suspended {
  Suspended : entry / PauseDataStreams
  Suspended : exit / ResumeDataStreams
}

Suspended --> Connected : NetworkRestoredEvent / ResumeQueueAction
Suspended --> Disconnected : DisconnectCmd / CloseSocketAction
Suspended --> Disconnected : TimeoutEvent / CleanupAction
@enduml`,

  smart_thermostat: `{
  "id": "SmartThermostatFSM",
  "initial": "Off",
  "ports": [
    { "name": "sensor_temp_c", "direction": "in", "type": "float", "min_value": -40.0, "max_value": 85.0 },
    { "name": "heater_relay", "direction": "out", "type": "bool" },
    { "name": "cooler_relay", "direction": "out", "type": "bool" }
  ],
  "variables": [
    { "name": "target_temp_c", "type": "float", "initial_value": "21.5", "description": "User setpoint temperature" },
    { "name": "hysteresis_c", "type": "float", "initial_value": "0.5", "description": "Deadband differential" },
    { "name": "eco_mode", "type": "bool", "initial_value": "false", "description": "Energy saving mode flag" }
  ],
  "signals": [
    {
      "name": "EvSetTemp",
      "attributes": [
        { "name": "new_target_c", "type": "float" }
      ]
    },
    {
      "name": "EvSensorReading",
      "attributes": [
        { "name": "ambient_temp_c", "type": "float" },
        { "name": "humidity_pct", "type": "float" }
      ]
    }
  ],
  "properties": [
    {
      "name": "SafeHeatingLimit",
      "kind": "Safety",
      "ltl": "G (OverheatAlert -> F Off)",
      "req": "REQ-HVAC-SAFE-01",
      "desc": "Shutdown heating upon overheat detection"
    }
  ],
  "states": {
    "Off": {
      "entry": ["DisplayOffMode"],
      "on": {
        "PowerOnCmd": { "target": "Standby", "action": "InitHvacControllers" }
      }
    },
    "Standby": {
      "entry": ["LogStandbyTelemetry"],
      "satisfies": ["REQ-HVAC-NORM-01"],
      "on": {
        "HeatReqCmd": { "target": "Heating", "guard": "IsBelowTargetTemp", "action": "IgniteBurner" },
        "CoolReqCmd": { "target": "Cooling", "guard": "IsAboveTargetTemp", "action": "StartCompressor" },
        "PowerOffCmd": { "target": "Off", "action": "ShutdownDisplay" }
      }
    },
    "Heating": {
      "entry": ["EngageHeatingRelay"],
      "exit": ["DisengageHeatingRelay"],
      "do": "async_temperature_monitor",
      "on": {
        "TargetReached": { "target": "Standby", "action": "LogTargetReached" },
        "PowerOffCmd": { "target": "Off", "action": "EmergencyCutoff" }
      }
    },
    "Cooling": {
      "entry": ["EngageCoolingCompressor"],
      "exit": ["DisengageCompressor"],
      "do": "async_temperature_monitor",
      "on": {
        "TargetReached": { "target": "Standby", "action": "LogTargetReached" },
        "PowerOffCmd": { "target": "Off", "action": "EmergencyCutoff" }
      }
    }
  }
}`,

  satellite_mission: `<?xml version="1.0" encoding="UTF-8"?>
<xmi:XMI xmlns:xmi="http://www.omg.org/spec/XMI/20131001" xmlns:uml="http://www.omg.org/spec/UML/20131001">
  <uml:Model xmi:id="_m1" name="CubeSatFlightModel">
    <packagedElement xmi:type="uml:StateMachine" xmi:id="_sm1" name="SatelliteMissionFSM">
      <region xmi:id="_r1">
        <subvertex xmi:type="uml:Pseudostate" xmi:id="_ps_init" kind="initial"/>
        <subvertex xmi:type="uml:State" xmi:id="_s_boot" name="Boot">
          <entry xmi:type="uml:Activity" name="RunPowerOnSelfTest"/>
          <exit xmi:type="uml:Activity" name="InitializeRadio"/>
        </subvertex>
        <subvertex xmi:type="uml:State" xmi:id="_s_detumble" name="Detumbling">
          <entry xmi:type="uml:Activity" name="ActivateMagnetorquers"/>
          <doActivity xmi:type="uml:Activity" name="StabilizeAngularRates"/>
          <exit xmi:type="uml:Activity" name="DeactivateMagnetorquers"/>
        </subvertex>
        <subvertex xmi:type="uml:State" xmi:id="_s_science" name="ScienceOperations">
          <entry xmi:type="uml:Activity" name="DeployPayloadSensors"/>
          <doActivity xmi:type="uml:Activity" name="CollectSpectrometerData"/>
          <deferrableTrigger name="GroundStationBeaconPing"/>
        </subvertex>
        <subvertex xmi:type="uml:State" xmi:id="_s_safe" name="SafeHold">
          <entry xmi:type="uml:Activity" name="PointSolarPanelsSun"/>
          <doActivity xmi:type="uml:Activity" name="RechargeBatteries"/>
        </subvertex>

        <transition xmi:id="_t0" source="_ps_init" target="_s_boot"/>
        <transition xmi:id="_t1" source="_s_boot" target="_s_detumble" trigger="PostOkEvent" effect="DeployAntennas"/>
        <transition xmi:id="_t2" source="_s_detumble" target="_s_science" trigger="RatesStabilizedEvent" guard="BatteryLevelOk" effect="StartMissionSchedule"/>
        <transition xmi:id="_t3" source="_s_science" target="_s_safe" trigger="LowBatteryAnomaly" effect="IsolateNonCriticalLoads"/>
        <transition xmi:id="_t4" source="_s_safe" target="_s_science" trigger="BatteryRecoveredCmd" guard="BatteryLevelOk" effect="ResumeScienceSchedule"/>
      </region>
    </packagedElement>
  </uml:Model>
</xmi:XMI>`,

  async_motor_controller: `stateDiagram-v2
    [*] --> Halted

    state Halted {
        Halted : entry / ClearPwmOutputs
        Halted : exit / PrechargeInverter
    }

    state Accelerating {
        Accelerating : entry / RampCurrentReference
    }

    state RunningAtSpeed {
        RunningAtSpeed : entry / EnableClosedLoopFluxVector
        RunningAtSpeed : exit / DisableClosedLoop
    }

    state Decelerating {
        Decelerating : entry / ApplyRegenerativeBrake
    }

    state Faulted {
        Faulted : entry / TripBridgeIsolationRelay
        Faulted : exit / ResetFaultLatch
    }

    Halted --> Accelerating : StartCmd [HasValidRpmGuard] / PowerOnInverterAction
    Accelerating --> RunningAtSpeed : SpeedReachedEvent / EngageSpeedPidAction
    RunningAtSpeed --> Decelerating : StopCmd / ApplyRegenerativeBrakeAction
    Decelerating --> Halted : StoppedEvent / DisengageInverterAction
    Accelerating --> Faulted : OvercurrentEvent / EmergencyCutoffAction
    RunningAtSpeed --> Faulted : OvercurrentEvent / EmergencyCutoffAction
    Decelerating --> Faulted : OvercurrentEvent / EmergencyCutoffAction
    Faulted --> Halted : ResetFaultCmd [IsThermalSafeGuard] / ClearFaultFlagsAction`
};

// --- Module: src/fsm_utils.js ---

/**
 * fsmc Playground — FSM Utility Functions
 * Pure functions for state machine graph traversal. No DOM, no WASM deps.
 */

/**
 * Resolves a potentially composite state to its deepest initial leaf state.
 * Follows initial_sub_state links recursively until a leaf is reached.
 * @param {object} model
 * @param {string} targetState
 * @returns {string}
 */
function resolveLeafState(model, targetState) {
  if (!targetState) return "";
  let curr = targetState.replace(/\[H\*?\]/g, "").trim();
  const visited = new Set();
  while (curr && !visited.has(curr)) {
    visited.add(curr);
    const detail = (model.stateDetails || []).find(d => d.name === curr);
    if (detail && detail.is_composite && detail.initial_sub_state) {
      curr = detail.initial_sub_state;
    } else {
      break;
    }
  }
  return curr;
}

/**
 * Returns the ancestor chain from a leaf state up to the root,
 * as an ordered array [leaf, parent, grandparent, ...].
 * @param {object} model
 * @param {string} stateName
 * @returns {string[]}
 */
function getAncestorChain(model, stateName) {
  const chain = [];
  let curr = stateName;
  const visited = new Set();
  while (curr && !visited.has(curr)) {
    visited.add(curr);
    chain.push(curr);
    const detail = (model.stateDetails || []).find(d => d.name === curr);
    if (detail && detail.parent) {
      curr = detail.parent;
    } else {
      break;
    }
  }
  return chain;
}

/**
 * Returns the set of transitions available in the current state,
 * including those defined in ancestor (composite) states.
 * Respects UML priority: innermost state wins for duplicate events.
 * @param {object} model
 * @param {string} currState
 * @returns {object[]}
 */
function getAvailableTransitions(model, currState) {
  const ancestors = getAncestorChain(model, currState);
  const result = [];
  const seenEvents = new Set();
  for (const st of ancestors) {
    const matching = (model.transitions || []).filter(t => t.source === st);
    for (const t of matching) {
      if (!seenEvents.has(t.event)) {
        seenEvents.add(t.event);
        result.push(t);
      }
    }
  }
  return result;
}

// --- Module: src/wasm_bridge.js ---

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
function initWasm() {
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
function getModule() {
  return _module
    ?? globalThis.fsmcModule
    ?? null;
}

// Start loading immediately in the background.
initWasm();

// --- Module: src/parsers/format_detect.js ---

/**
 * fsmc Playground — Format Detector
 * Determines the diagram format from a raw text string via heuristic matching.
 */

/**
 * Detect the FSM diagram format from raw text.
 * @param {string} text
 * @returns {'scxml'|'cameo'|'plantuml'|'mermaid'|'sysml2'|'dot'|'smv'|'json'|'plantuml'}
 */
function detectFormat(text) {
  const t = (text || "").trim();
  if (t.includes('<scxml') || t.includes('xmlns="http://www.w3.org/2005/07/scxml"')) return 'scxml';
  if (t.includes('<xmi:') || t.includes('<uml:') || t.includes('<packagedElement')) return 'cameo';
  if (t.includes('@startuml') || t.includes('@enduml')) return 'plantuml';
  if (t.includes('stateDiagram') || t.includes('stateDiagram-v2')) return 'mermaid';
  if (
    t.includes('state def ') || t.includes('transition from ') ||
    t.includes('item def ') || t.includes('event def ') ||
    t.includes('entry; then') || t.includes('attribute ')
  ) return 'sysml2';
  if (t.includes('digraph ') || t.startsWith('digraph{') || t.includes('graph ')) return 'dot';
  if (
    t.includes('MODULE main') || t.includes('ASSIGN next(state)') ||
    t.includes('LTLSPEC') || t.includes('INVARSPEC')
  ) return 'smv';
  if (t.startsWith('{') || (t.includes('"states"') && t.includes('"id"'))) return 'json';
  return 'plantuml';
}

// --- Module: src/parsers/scxml.js ---

/**
 * fsmc Playground — XML/SCXML Parser
 * Parses W3C SCXML and OMG XMI/Cameo-style XML into the canonical AST model.
 */

/** @param {string} str */
function unescapeXml(str) {
  return (str || "")
    .replace(/&amp;/g, "&")
    .replace(/&lt;/g, "<")
    .replace(/&gt;/g, ">")
    .replace(/&quot;/g, '"')
    .replace(/&apos;/g, "'");
}

/**
 * Parse W3C SCXML into the canonical model.
 * @param {string} text
 * @returns {object}
 */
function parseScxml(text) {
  const states = new Set();
  const stateDetails = [];
  const events = new Set();
  const transitions = [];
  let initial = "";
  let name = "GeneratedFSM";

  const nameMatch = text.match(/name="([^"]+)"/);
  if (nameMatch) name = nameMatch[1];
  const initMatch = text.match(/initial="([^"]+)"/);
  if (initMatch) initial = initMatch[1];

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
      const evtM  = tAttrs.match(/event="([^"]+)"/);
      const condM = tAttrs.match(/cond="([^"]*)"/);
      const targetM = tAttrs.match(/target="([^"]+)"/);
      const actM  = tAttrs.match(/action="([^"]*)"/) || tAttrs.match(/effect="([^"]*)"/);

      const evt  = evtM   ? evtM[1]  : "Anonymous";
      const cond = condM  ? condM[1].replace(/&amp;/g, "&").replace(/&lt;/g, "<").replace(/&gt;/g, ">") : "";
      const dst  = targetM ? targetM[1] : srcId;
      const act  = actM   ? actM[1]  : "";

      if (evt !== "Anonymous") events.add(evt);
      if (dst && !states.has(dst)) {
        states.add(dst);
        stateDetails.push({ name: dst, parent: "", is_composite: false });
      }
      transitions.push({
        source: srcId, target: dst,
        event: evt, guard: cond, action: act,
        is_internal: (srcId === dst)
      });
    }
  }

  const stateList = Array.from(states);
  return {
    name,
    states: stateList,
    stateDetails: stateDetails.length > 0 ? stateDetails : stateList.map(s => ({ name: s, parent: "", is_composite: false })),
    events: Array.from(events),
    transitions,
    initialState: initial || stateList[0] || "State"
  };
}

/**
 * Parse OMG XMI / Cameo-MagicDraw into the canonical model.
 * @param {string} text
 * @returns {object}
 */
function parseCameo(text) {
  const states = new Set();
  const stateDetails = [];
  const events = new Set();
  const transitions = [];
  let initial = "";
  const name = "GeneratedFSM";

  const stateMap = new Map();
  const stateMatches = text.matchAll(/<subvertex\s+([^>]*?)\/?>/g);
  for (const m of stateMatches) {
    const attrs = m[1];
    const idMatch   = attrs.match(/xmi:id="([^"]+)"/);
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
    const srcM   = attrs.match(/source="([^"]+)"/);
    const dstM   = attrs.match(/target="([^"]+)"/);
    const trigM  = attrs.match(/trigger="([^"]*)"/);
    const guardM = attrs.match(/guard="([^"]*)"/);
    const effM   = attrs.match(/effect="([^"]*)"/) || attrs.match(/action="([^"]*)"/);

    if (!srcM || !dstM) continue;
    const src   = stateMap.get(srcM[1]) || srcM[1];
    const dst   = stateMap.get(dstM[1]) || dstM[1];
    const evt   = trigM  ? trigM[1]           : "Anonymous";
    const guard = guardM ? unescapeXml(guardM[1]) : "";
    const act   = effM   ? unescapeXml(effM[1])   : "";

    if (src.includes("ps") || src.includes("initial")) {
      initial = dst;
      continue;
    }
    if (evt !== "Anonymous") events.add(evt);
    transitions.push({ source: src, target: dst, event: evt, guard, action: act, is_internal: (src === dst) });
  }

  const stateList = Array.from(states);
  return {
    name,
    states: stateList,
    stateDetails: stateDetails.length > 0 ? stateDetails : stateList.map(s => ({ name: s, parent: "", is_composite: false })),
    events: Array.from(events),
    transitions,
    initialState: initial || stateList[0] || "State"
  };
}

// --- Module: src/parsers/plantuml.js ---

/**
 * fsmc Playground — PlantUML / Mermaid stateDiagram Parser
 * Parses PlantUML and Mermaid stateDiagram-v2 into the canonical AST model.
 */

/**
 * Parse PlantUML (@startuml) or Mermaid (stateDiagram-v2) text.
 * Both formats share the --> transition arrow syntax.
 * @param {string} text
 * @returns {object}
 */
function parsePlantUmlOrMermaid(text) {
  const states = new Set();
  const stateDetails = [];
  const events = new Set();
  const transitions = [];
  let initial = "";
  let name = "GeneratedFSM";

  const lines = text.split('\n');
  for (const raw of lines) {
    const line = raw.trim();
    if (!line) continue;

    // Title detection
    const titleMatch = line.match(
      /(?:---\s*title:\s*([A-Za-z0-9_]+)|title:\s*([A-Za-z0-9_]+)|@startuml\s+([A-Za-z0-9_]+)|@fsm:name\s+([A-Za-z0-9_]+))/
    );
    if (titleMatch) {
      name = titleMatch[1] || titleMatch[2] || titleMatch[3] || titleMatch[4];
      continue;
    }

    // Signal annotations
    const sigMatch = line.match(/(?:'|%%|<!--)\s*@fsm:signal\s+([A-Za-z0-9_]+)/);
    if (sigMatch) { events.add(sigMatch[1]); continue; }

    // Skip directive lines
    if (line.startsWith('@') || line.startsWith('stateDiagram') || line.startsWith('<?xml') || line.startsWith('<scxml')) continue;

    // State declaration (no transition)
    if (line.startsWith('state ') && !line.includes('-->')) {
      const stName = line.replace('state ', '').split('{')[0].split('[')[0].trim();
      if (stName) {
        states.add(stName);
        stateDetails.push({ name: stName, parent: "", is_composite: line.includes('{') });
      }
    }

    // Transition: A --> B : Event [Guard] / Action
    if (line.includes('-->')) {
      const parts = line.split('-->');
      const src = parts[0].trim();
      const rest = parts[1].trim();
      const rawDst = rest.split(':')[0].trim();
      const isHist = rawDst.includes('[H');
      const isDeepHist = rawDst.includes('[H*]');
      const dst = rawDst.replace(/\[H\*?\]/g, '');
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
        transitions.push({
          source: src, target: dst,
          event: evt, guard, action,
          is_internal: (src === dst),
          target_is_history: isHist,
          target_is_deep_history: isDeepHist
        });
      }
    }
  }

  const stateList = Array.from(states);
  if (!initial && stateList.length > 0) initial = stateList[0];
  return {
    name,
    states: stateList,
    stateDetails: stateDetails.length > 0 ? stateDetails : stateList.map(s => ({ name: s, parent: "", is_composite: false })),
    events: Array.from(events),
    transitions,
    initialState: initial || "Disconnected"
  };
}

// --- Module: src/parsers/sysml.js ---

/**
 * fsmc Playground — OMG SysML v2 Parser
 * Parses the native SysML v2 HFSM notation into the canonical AST model,
 * including composite states, history pseudostates, deferred events, and typed ports.
 */

/**
 * Parse SysML v2 state machine definition text.
 * @param {string} text
 * @returns {object}
 */
function parseSysml2(text) {
  const states = new Set();
  const stateDetails = [];
  const events = new Set();
  const transitions = [];
  let initial = "";
  let name = "GeneratedFSM";

  const nameMatch = text.match(/(?:state\s+def|package)\s+([A-Za-z0-9_]+)/);
  if (nameMatch) name = nameMatch[1];

  // Strip comments and port constraint blocks
  let cleanText = text.replace(/\/\/.*$/gm, '').replace(/\/\*[\s\S]*?\*\//g, '');
  cleanText = cleanText.replace(/\{\s*assert\s+constraint[\s\S]*?\}\s*\}/g, ';');

  const stateStack = [];

  const processSysmlStmt = (stmt, stack) => {
    const s = stmt.replace(/\s+/g, ' ').trim();
    if (!s || s.startsWith("attribute") ||
        s.startsWith("in port") || s.startsWith("out port") || s.startsWith("port") ||
        s === "entry" || s === "initial") return;

    // Event / item definitions
    const evtDefMatch = s.match(/^(?:event\s+def|item\s+def)\s+([A-Za-z0-9_]+)/);
    if (evtDefMatch) { events.add(evtDefMatch[1]); return; }

    // Initial/entry -> sub-state
    const initMatch = s.match(/^(?:(?:entry|initial)(?:\s*;)?\s*)?then\s+([A-Za-z0-9_]+)/);
    if (initMatch) {
      if (stack.length === 0) {
        initial = initMatch[1];
      } else {
        const pObj = stateDetails.find(d => d.name === stack[stack.length - 1]);
        if (pObj) pObj.initial_sub_state = initMatch[1];
      }
      return;
    }

    // do activity
    const doActMatch = s.match(/^do\s+(?:action\s+)?([A-Za-z0-9_]+)/);
    if (doActMatch && stack.length > 0) {
      const pObj = stateDetails.find(d => d.name === stack[stack.length - 1]);
      if (pObj) pObj.do_activity = doActMatch[1];
      return;
    }

    // entry action
    const entryActMatch = s.match(/^entry\s+(?:action\s+|do\s+)([A-Za-z0-9_]+)/);
    if (entryActMatch && stack.length > 0) {
      const pObj = stateDetails.find(d => d.name === stack[stack.length - 1]);
      if (pObj) { pObj.entry_actions = pObj.entry_actions || []; pObj.entry_actions.push(entryActMatch[1]); }
      return;
    }

    // exit action
    const exitActMatch = s.match(/^exit\s+(?:action\s+|do\s+)([A-Za-z0-9_]+)/);
    if (exitActMatch && stack.length > 0) {
      const pObj = stateDetails.find(d => d.name === stack[stack.length - 1]);
      if (pObj) { pObj.exit_actions = pObj.exit_actions || []; pObj.exit_actions.push(exitActMatch[1]); }
      return;
    }

    // defer
    const deferMatch = s.match(/^defer\s+([A-Za-z0-9_]+)/);
    if (deferMatch) {
      events.add(deferMatch[1]);
      if (stack.length > 0) {
        const pObj = stateDetails.find(d => d.name === stack[stack.length - 1]);
        if (pObj) { pObj.deferred_events = pObj.deferred_events || []; pObj.deferred_events.push(deferMatch[1]); }
      }
      return;
    }

    // Leaf state declaration
    const stateDecl = s.match(/^state\s+([A-Za-z0-9_]+)\s*$/);
    if (stateDecl && stateDecl[1] !== "def") {
      const sName = stateDecl[1];
      const parent = stack.length > 0 ? stack[stack.length - 1] : "";
      if (!states.has(sName)) {
        states.add(sName);
        stateDetails.push({ name: sName, parent, is_composite: false });
      }
      return;
    }

    // Transition
    if (s.startsWith("transition") || s.includes("first ") || s.includes("from ")) {
      const fromMatch  = s.match(/(?:first|from)\s+([A-Za-z0-9_]+)/);
      const acceptMatch = s.match(/(?:accept|when)\s+(?:[A-Za-z0-9_]+\s*:\s*)?([A-Za-z0-9_]+)/);
      const ifMatch    = s.match(/\sif\s+(.+?)(?=\sdo\s|\sthen\s|\sto\s|;|$)/);
      const doMatch    = s.match(/\sdo\s+(?:\{([^}]+)\}|([A-Za-z0-9_]+))/);
      const thenMatch  = s.match(/(?:then|to)\s+([A-Za-z0-9_\[\]\*]+)/);

      const src    = fromMatch ? fromMatch[1] : (stack.length > 0 ? stack[stack.length - 1] : "");
      const rawDst = thenMatch ? thenMatch[1] : src;
      let dst = rawDst;
      let isHist = false, isDeepHist = false;
      if (dst && dst.includes("[H")) {
        isHist = true;
        isDeepHist = dst.includes("[H*]");
        dst = dst.replace(/\[H\*?\]/g, '');
      }

      if (src && dst) {
        // Eventless transition: no accept clause → event = ""
        const evt    = acceptMatch ? acceptMatch[1] : "";
        const guard  = ifMatch  ? ifMatch[1].trim()               : "";
        const action = doMatch  ? (doMatch[1] || doMatch[2]).trim() : "";
        states.add(src);
        states.add(dst);
        if (evt) events.add(evt);
        transitions.push({
          source: src, target: dst,
          event: evt, guard, action,
          is_internal: (src === dst && !thenMatch),
          target_is_history: isHist,
          target_is_deep_history: isDeepHist
        });
      }
    }
  };

  // Tokenise the cleaned text character by character
  let currentStmt = "";
  let inActionBrace = 0;
  for (let i = 0; i < cleanText.length; i++) {
    const ch = cleanText[i];
    if (ch === '{') {
      const stmt = currentStmt.trim();
      if (stmt.startsWith("state def") || stmt.startsWith("package")) {
        currentStmt = "";
        continue;
      }
      const stateDecl = stmt.match(/state\s+([A-Za-z0-9_]+)/);
      if (stateDecl && stateDecl[1] !== "def") {
        currentStmt = "";
        const sName  = stateDecl[1];
        const parent = stateStack.length > 0 ? stateStack[stateStack.length - 1] : "";
        states.add(sName);
        stateDetails.push({ name: sName, parent, is_composite: true });
        stateStack.push(sName);
      } else {
        inActionBrace++;
        currentStmt += "{";
      }
    } else if (ch === '}') {
      if (inActionBrace > 0) {
        inActionBrace--;
        currentStmt += "}";
      } else {
        const stmt = currentStmt.trim();
        currentStmt = "";
        if (stmt) processSysmlStmt(stmt, stateStack);
        if (stateStack.length > 0) stateStack.pop();
      }
    } else if (ch === ';') {
      if (inActionBrace > 0) {
        currentStmt += ";";
      } else {
        const s = currentStmt.trim();
        if (s === "entry" || s === "initial") { currentStmt += "; "; continue; }
        currentStmt = "";
        if (s) processSysmlStmt(s, stateStack);
      }
    } else {
      currentStmt += ch;
    }
  }

  const stateList = Array.from(states);
  if (!initial && stateList.length > 0) initial = stateList[0];
  return {
    name,
    states: stateList,
    stateDetails: stateDetails.length > 0 ? stateDetails : stateList.map(s => ({ name: s, parent: "", is_composite: false })),
    events: Array.from(events),
    transitions,
    initialState: initial || "Disconnected"
  };
}

// --- Module: src/parsers/misc.js ---

/**
 * fsmc Playground — Miscellaneous Format Parsers
 * JSON (XState), Graphviz DOT, and NuSMV/nuXmv SMV parsers.
 */

/**
 * Parse XState-style JSON.
 * @param {string} text
 * @returns {object}
 */
function parseJson(text) {
  const states = new Set();
  const stateDetails = [];
  const events = new Set();
  const transitions = [];
  let initial = "";
  let name = "GeneratedFSM";

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
            const push = (target, guard, action) => {
              states.add(target);
              transitions.push({ source: stName, target, event: evtName, guard: guard || "", action: action || "", is_internal: stName === target });
            };
            if (Array.isArray(targetVal)) {
              for (const item of targetVal) {
                push(typeof item === 'string' ? item : item.target, item.guard || item.cond, item.action || item.actions);
              }
            } else if (typeof targetVal === 'string') {
              push(targetVal, "", "");
            } else if (targetVal && targetVal.target) {
              push(targetVal.target, targetVal.guard || targetVal.cond, targetVal.action || targetVal.actions);
            }
          }
        }
      }
    }
  } catch (_) {}

  const stateList = Array.from(states);
  return {
    name,
    states: stateList,
    stateDetails: stateDetails.length > 0 ? stateDetails : stateList.map(s => ({ name: s, parent: "", is_composite: false })),
    events: Array.from(events),
    transitions,
    initialState: initial || stateList[0] || "State"
  };
}

/**
 * Parse Graphviz DOT digraph.
 * @param {string} text
 * @returns {object}
 */
function parseDot(text) {
  const states = new Set();
  const stateDetails = [];
  const events = new Set();
  const transitions = [];
  let initial = "";
  let name = "GeneratedFSM";

  const nameMatch = text.match(/digraph\s+([A-Za-z0-9_]+)/);
  if (nameMatch) name = nameMatch[1];

  for (const raw of text.split('\n')) {
    const line = raw.trim();
    const sigMatch = line.match(/\/\/\s*@fsm:signal\s+([A-Za-z0-9_]+)/);
    if (sigMatch) { events.add(sigMatch[1]); continue; }
    if (!line.includes('->')) continue;

    const parts = line.split('->');
    const src = parts[0].replace(/;/g, '').trim();
    const rest = parts[1].split(';')[0].trim();
    const dst  = rest.split('[')[0].trim();
    let evt = "Anonymous", guard = "", action = "";

    const lblMatch = line.match(/label="([^"]+)"/);
    if (lblMatch) {
      const rawLbl = lblMatch[1];
      const evPart = rawLbl.split('[')[0].split('/')[0].trim();
      if (evPart) evt = evPart;
      const gM = rawLbl.match(/\[([^\]]+)\]/);
      if (gM) guard = gM[1];
      const aM = rawLbl.match(/\/([^"]+)/);
      if (aM) action = aM[1].trim();
    }

    if (src === '__start__' || src === '[*]') {
      initial = dst;
    } else {
      states.add(src); states.add(dst);
      if (evt !== "Anonymous") events.add(evt);
      transitions.push({ source: src, target: dst, event: evt, guard, action, is_internal: src === dst });
    }
  }

  const stateList = Array.from(states);
  return {
    name,
    states: stateList,
    stateDetails: stateDetails.length > 0 ? stateDetails : stateList.map(s => ({ name: s, parent: "", is_composite: false })),
    events: Array.from(events),
    transitions,
    initialState: initial || stateList[0] || "State"
  };
}

/**
 * Parse NuSMV / nuXmv SMV formal verification language.
 * @param {string} text
 * @returns {object}
 */
function parseSmv(text) {
  const states = new Set();
  const stateDetails = [];
  const events = new Set();
  const transitions = [];
  let initial = "";
  let name = "GeneratedFSM";

  const modMatch = text.match(/MODULE\s+([A-Za-z0-9_]+)/);
  if (modMatch && modMatch[1] !== 'main') name = modMatch[1];

  const lines = text.split('\n');
  let currentSection = "";

  for (let raw of lines) {
    let line = raw.trim();
    if (!line || line.startsWith("--")) continue;
    if (line === "VAR")    { currentSection = "var";    continue; }
    if (line === "ASSIGN") { currentSection = "assign"; continue; }
    if (line.includes("next(state)") && line.includes("case")) { currentSection = "next_state"; continue; }
    if (line === "esac;" || line === "esac") { currentSection = ""; continue; }

    if (currentSection === "var") {
      if (line.startsWith("state :") || line.startsWith("state:")) {
        const enumMatch = line.match(/\{([^}]+)\}/);
        if (enumMatch) {
          for (const s of enumMatch[1].split(',').map(s => s.trim()).filter(Boolean)) {
            states.add(s);
            stateDetails.push({ name: s, parent: "", is_composite: false });
          }
        }
      } else if (line.startsWith("event :") || line.startsWith("event:")) {
        const enumMatch = line.match(/\{([^}]+)\}/);
        if (enumMatch) {
          for (const ev of enumMatch[1].split(',').map(e => e.trim()).filter(e => e && e !== "none")) events.add(ev);
        }
      }
    } else if (currentSection === "assign") {
      if (line.startsWith("init(state)")) {
        const eqIdx = line.indexOf(":=");
        if (eqIdx !== -1) initial = line.substring(eqIdx + 2).replace(/;/g, '').trim();
      }
    } else if (currentSection === "next_state") {
      if (line.includes("TRUE :") || line.includes("TRUE:")) continue;
      let colonIdx = -1;
      for (let i = line.length - 1; i >= 0; i--) {
        if (line[i] === ':') {
          const isDouble = (i > 0 && line[i-1] === ':') || (i+1 < line.length && line[i+1] === ':');
          if (!isDouble) { colonIdx = i; break; }
        }
      }
      if (colonIdx === -1) continue;
      let commentAct = "";
      const commentPos = line.indexOf("--");
      if (commentPos !== -1 && commentPos > colonIdx) {
        const cStr = line.substring(commentPos);
        const actMatch = cStr.match(/action:\s*([A-Za-z0-9_]+)/);
        if (actMatch) commentAct = actMatch[1];
        line = line.substring(0, commentPos).trim();
      }
      const condPart   = line.substring(0, colonIdx).trim();
      const targetPart = line.substring(colonIdx + 1).replace(/;/g, '').trim();
      if (!targetPart) continue;
      const clauses = condPart.split('&').map(c => c.trim().replace(/^\(+|\)+$/g, '').trim());
      let srcState = "", evName = "Anonymous", guardParts = [];
      for (const c of clauses) {
        if (c.startsWith("state =") || c.startsWith("state=")) srcState = c.split('=')[1].trim();
        else if (c.startsWith("event =") || c.startsWith("event=")) {
          const ev = c.split('=')[1].trim();
          if (ev && ev !== "none") { evName = ev; events.add(ev); }
        } else if (c) guardParts.push(c);
      }
      if (srcState && targetPart) {
        states.add(srcState); states.add(targetPart);
        transitions.push({ source: srcState, target: targetPart, event: evName, guard: guardParts.join(" && "), action: commentAct, is_internal: srcState === targetPart });
      }
    }
  }

  const stateList = Array.from(states);
  return {
    name,
    states: stateList,
    stateDetails: stateDetails.length > 0 ? stateDetails : stateList.map(s => ({ name: s, parent: "", is_composite: false })),
    events: Array.from(events),
    transitions,
    initialState: initial || stateList[0] || "State"
  };
}

// --- Module: src/parsers/index.js ---

/**
 * fsmc Playground — Parser Index
 * Re-exports all parsers and the format detector for unified access via ModelManager.
 */


/**
 * Dispatch to the correct parser based on detected format.
 * Returns a canonical model object.
 * @param {string} text
 * @param {string} [format]
 * @returns {object}
 */
async function fallbackParse(text, format) {
  
  const fmt = format || detectFormat(text);

  switch (fmt) {
    case 'scxml':   {    return parseScxml(text); }
    case 'cameo':   {    return parseCameo(text); }
    case 'sysml2':  {    return parseSysml2(text); }
    case 'json':    {     return parseJson(text); }
    case 'dot':     {     return parseDot(text); }
    case 'smv':     {     return parseSmv(text); }
    default:        {  return parsePlantUmlOrMermaid(text); }
  }
}

// --- Module: src/serializers/index.js ---

/**
 * fsmc Playground — Serializers
 * Each function takes a canonical model object and returns a diagram string
 * in the target format. No DOM, no WASM deps.
 */

const ANON = new Set(["Anonymous", "AnonymousEvent", "anonymous"]);

/** True if event name represents an anonymous/unnamed event */
function isAnon(evt) { return !evt || ANON.has(evt); }

/**
 * Canonical history suffix for a transition target.
 * @param {object} t - transition object
 * @returns {string}
 */
function histSuffix(t) {
  if (t.target_is_deep_history) return '[H*]';
  if (t.target_is_history)      return '[H]';
  return '';
}

/** Effective target name, including history suffix if applicable */
function effectiveTarget(t) {
  return (t.is_internal ? t.source : t.target) + (t.is_internal ? '' : histSuffix(t));
}

/** Build a transition label: Event [Guard] / Action */
function buildLabel(t) {
  let label = !isAnon(t.event) ? t.event : "";
  if (t.guard)  label += (label ? " " : "") + `[${t.guard}]`;
  if (t.action) label += (label ? " " : "") + `/ ${t.action}`;
  return label;
}

// ---------------------------------------------------------------------------

/**
 * Serialize model to Mermaid stateDiagram-v2.
 * @param {object} model
 * @returns {string}
 */
function toMermaid(model) {
  let out = "";
  if (model.name && model.name !== "GeneratedFSM" && model.name !== "MyStateMachine") {
    out += `--- title: ${model.name} ---\n`;
  }
  out += "stateDiagram-v2\n";
  for (const ev of (model.events || [])) {
    if (!isAnon(ev)) out += `%% @fsm:signal ${ev}\n`;
  }
  if (model.initialState) out += `    [*] --> ${model.initialState}\n`;
  for (const t of model.transitions) {
    const label = buildLabel(t);
    const lblStr = label ? ` : ${label}` : "";
    out += `    ${t.source} --> ${effectiveTarget(t)}${lblStr}\n`;
  }
  return out.trim();
}

/**
 * Serialize model to PlantUML (@startuml).
 * @param {object} model
 * @returns {string}
 */
function toPlantUml(model) {
  const header = (model.name && model.name !== "GeneratedFSM" && model.name !== "MyStateMachine")
    ? `@startuml ${model.name}\n`
    : "@startuml\n";
  let out = header;
  for (const ev of (model.events || [])) {
    if (!isAnon(ev)) out += `' @fsm:signal ${ev}\n`;
  }
  if (model.initialState) out += `[*] --> ${model.initialState}\n\n`;
  for (const t of model.transitions) {
    const label = buildLabel(t);
    const lblStr = label ? ` : ${label}` : "";
    out += `${t.source} --> ${effectiveTarget(t)}${lblStr}\n`;
  }
  return out + "@enduml";
}

/**
 * Serialize model to OMG SysML v2.
 * @param {object} model
 * @returns {string}
 */
function toSysml2(model) {
  const fsmName = model.name || "GeneratedFSM";
  let out = `state def ${fsmName} {\n`;
  for (const ev of (model.events || [])) {
    if (!isAnon(ev)) out += `    event def ${ev};\n`;
  }
  if (model.events?.length) out += "\n";
  if (model.initialState) out += `    initial state ${model.initialState};\n\n`;
  for (const s of model.states) out += `    state ${s};\n`;
  out += "\n";
  for (const t of model.transitions) {
    const tgt = t.is_internal ? t.source : (t.target + histSuffix(t));
    out += `    transition from ${t.source}`;
    if (!isAnon(t.event))  out += ` accept ${t.event}`;
    if (t.guard)   out += ` if ${t.guard}`;
    if (t.action)  out += ` do ${t.action}`;
    out += ` then ${tgt};\n`;
  }
  return out + "}";
}

/**
 * Serialize model to W3C SCXML.
 * @param {object} model
 * @returns {string}
 */
function toScxml(model) {
  const fsmName = model.name || "GeneratedFSM";
  let out = `<?xml version="1.0" encoding="UTF-8"?>\n`;
  out += `<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="${model.initialState}" name="${fsmName}">\n`;
  for (const ev of (model.events || [])) {
    if (!isAnon(ev)) out += `  <!-- @fsm:signal ${ev} -->\n`;
  }
  for (const s of model.states) {
    const transFromS = model.transitions.filter(t => t.source === s);
    if (transFromS.length === 0) {
      out += `  <state id="${s}"/>\n`;
    } else {
      out += `  <state id="${s}">\n`;
      for (const t of transFromS) {
        out += `    <transition`;
        if (!isAnon(t.event)) out += ` event="${t.event}"`;
        if (t.guard)  out += ` cond="${t.guard.replace(/&/g, '&amp;')}"`;
        if (t.action) out += ` action="${t.action}"`;
        if (t.target && !t.is_internal) out += ` target="${t.target}"`;
        out += `/>\n`;
      }
      out += `  </state>\n`;
    }
  }
  return out + "</scxml>";
}

/**
 * Serialize model to OMG XMI 2.1 (Cameo / MagicDraw).
 * @param {object} model
 * @returns {string}
 */
function toCameo(model) {
  const fsmName = model.name || "GeneratedFSM";
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
    if (!isAnon(t.event)) out += ` trigger="${t.event}"`;
    if (t.guard)  out += ` guard="${t.guard.replace(/&/g, '&amp;')}"`;
    if (t.action) out += ` effect="${t.action}"`;
    out += `/>\n`;
  }
  return out + "      </region>\n    </packagedElement>\n  </uml:Model>\n</xmi:XMI>";
}

/**
 * Serialize model to XState JSON.
 * @param {object} model
 * @returns {string}
 */
function toJson(model) {
  const fsmName = model.name || "GeneratedFSM";
  const obj = { id: fsmName, initial: model.initialState, states: {} };
  for (const s of model.states) obj.states[s] = { on: {} };
  for (const t of model.transitions) {
    if (!obj.states[t.source]) obj.states[t.source] = { on: {} };
    const transObj = { target: t.is_internal ? t.source : t.target };
    if (t.guard)  transObj.guard  = t.guard;
    if (t.action) transObj.action = t.action;
    const evtKey  = t.event || "Anonymous";
    const existing = obj.states[t.source].on[evtKey];
    if (existing) {
      obj.states[t.source].on[evtKey] = Array.isArray(existing) ? [...existing, transObj] : [existing, transObj];
    } else {
      obj.states[t.source].on[evtKey] = transObj;
    }
  }
  return JSON.stringify(obj, null, 2);
}

/**
 * Serialize model to Graphviz DOT.
 * @param {object} model
 * @returns {string}
 */
function toDot(model) {
  const fsmName = model.name || "GeneratedFSM";
  let out = `digraph ${fsmName} {\n`;
  for (const ev of (model.events || [])) {
    if (!isAnon(ev) && ev !== "EVENT") out += `    // @fsm:signal ${ev}\n`;
  }
  out += `    __start__ [shape=point];\n`;
  if (model.initialState) out += `    __start__ -> ${model.initialState};\n`;
  for (const t of model.transitions) {
    let label = t.event || "";
    if (t.guard)  label += ` [${t.guard}]`;
    if (t.action) label += ` / ${t.action}`;
    const lblStr = label && label !== "Anonymous" ? ` [label="${label}"]` : "";
    out += `    ${t.source} -> ${t.is_internal ? t.source : t.target}${lblStr};\n`;
  }
  return out + "}";
}

/**
 * Fallback: serialize to the best available format for the model.
 * @param {object} model
 * @param {string} toFormat
 * @returns {string}
 */
function serialize(model, toFormat) {
  switch (toFormat) {
    case 'mermaid':  return toMermaid(model);
    case 'plantuml': return toPlantUml(model);
    case 'sysml2':   return toSysml2(model);
    case 'scxml':    return toScxml(model);
    case 'cameo':    return toCameo(model);
    case 'json':     return toJson(model);
    case 'dot':      return toDot(model);
    default:         return toPlantUml(model);
  }
}

// --- Module: src/cpp_generator.js ---

/**
 * fsmc Playground — C++ Code Generator
 * Generates idiomatic C++17/20 FSM code from a canonical model.
 * Falls back to this JS implementation when WASM is not available.
 */

/**
 * Generate C++ source from a canonical model.
 * @param {object} model
 * @param {boolean} isCpp20
 * @returns {string}
 */
function generateCpp(model, isCpp20 = true) {
  let rawName = model.name;
  if (!rawName || rawName === "GeneratedFSM" || rawName === "StateMachine") {
    rawName = model.initialState || "IndustrialController";
  }
  const fsmName = rawName.endsWith("FSM") ? rawName : rawName + "FSM";

  let code = `// ============================================================================\n`;
  code += `// Generated by fsmc v0.5.0 (The Universal State Machine Compiler)\n`;
  code += `// Target: C++${isCpp20 ? '20' : '17'} (Standalone 0-Deps)\n`;
  code += `// ============================================================================\n#pragma once\n\n`;
  code += `#include <iostream>\n#include <string_view>\n\n`;
  code += `namespace fsm_generated {\n\n`;

  // Events
  code += `// --- Event Trigger Definitions ---\n`;
  for (const evt of model.events) code += `struct ${evt} {};\n`;
  code += `\n`;

  // States enum
  code += `// --- State Enumeration ---\n`;
  code += `enum class State {\n`;
  for (const st of model.states) code += `    ${st},\n`;
  code += `};\n\n`;

  // Collect guards & actions
  const guards  = new Set();
  const actions = new Set();
  for (const t of model.transitions) {
    if (t.guard) {
      for (const tok of (t.guard.match(/[A-Za-z0-9_]+/g) || [])) {
        if (!["true","false","and","or","not"].includes(tok)) guards.add(tok);
      }
    }
    if (t.action) {
      for (const tok of (t.action.match(/[A-Za-z0-9_]+/g) || [])) actions.add(tok);
    }
  }

  // FSM class
  code += `// --- Finite State Machine Implementation ---\n`;
  code += `class ${fsmName} {\npublic:\n`;
  code += `    constexpr ${fsmName}() noexcept : current_state_(State::${model.initialState}) {}\n`;
  code += `    virtual ~${fsmName}() = default;\n\n`;
  code += `    [[nodiscard]] constexpr State current_state() const noexcept { return current_state_; }\n`;
  code += `    [[nodiscard]] std::string_view current_state_name() const noexcept {\n`;
  code += `        switch (current_state_) {\n`;
  for (const st of model.states) code += `            case State::${st}: return "${st}";\n`;
  code += `        }\n        return "Unknown";\n    }\n\n`;

  if (guards.size > 0) {
    code += `    // --- Guard Predicates (Override in derived class) ---\n`;
    for (const g of guards) code += `    [[nodiscard]] virtual bool check_${g}() const noexcept { return true; }\n`;
    code += `\n`;
  }

  if (actions.size > 0) {
    code += `    // --- Action Handlers (Override in derived class) ---\n`;
    for (const a of actions) code += `    virtual void on_${a}() noexcept {}\n`;
    code += `\n`;
  }

  code += `    // --- Event Dispatchers ---\n`;
  for (const evt of model.events) {
    code += `    bool dispatch(const ${evt}&) noexcept {\n`;
    code += `        switch (current_state_) {\n`;
    for (const t of model.transitions.filter(t => t.event === evt)) {
      code += `            case State::${t.source}: {\n`;
      if (t.guard) {
        let gCond = t.guard;
        for (const tok of (t.guard.match(/[A-Za-z0-9_]+/g) || [])) {
          if (guards.has(tok)) gCond = gCond.replace(new RegExp(`\\b${tok}\\b`, 'g'), `check_${tok}()`);
        }
        code += `                if (!(${gCond})) return false;\n`;
      }
      if (t.action) {
        for (const tok of (t.action.match(/[A-Za-z0-9_]+/g) || [])) {
          if (actions.has(tok)) code += `                on_${tok}();\n`;
        }
      }
      if (!t.is_internal && t.target) code += `                current_state_ = State::${t.target};\n`;
      code += `                return true;\n            }\n`;
    }
    code += `            default: return false;\n        }\n    }\n\n`;
  }

  code += `private:\n    State current_state_;\n};\n\n} // namespace fsm_generated\n`;
  return code;
}

// --- Module: src/model_manager.js ---

/**
 * fsmc Playground — Model Manager
 * Orchestrates parse, serialize, export, optimize, generateCpp, and validate.
 * Delegates format-specific work to parsers/ and serializers/ modules.
 * Maintains the single currentModel instance shared across the application.
 */


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

// --- Module: src/viewport.js ---

/**
 * fsmc Playground — Viewport Controller
 * Manages pan, pinch-zoom, scroll-zoom, and keyboard zoom on the canvas SVG.
 */


const ViewportController = {
  init() {
    const canvas = document.getElementById("mermaidCanvas");
    let isDragging = false, startX = 0, startY = 0;
    let initialTouchDist = 0, initialZoom = 1.0;

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
      if (isDragging) { isDragging = false; canvas.style.cursor = "grab"; }
    };

    canvas.addEventListener("touchstart", (e) => {
      if (e.target.closest('.node')) return;
      if (e.touches.length === 1) {
        isDragging = true;
        startX = e.touches[0].clientX - ModelManager.currentModel.panX;
        startY = e.touches[0].clientY - ModelManager.currentModel.panY;
      } else if (e.touches.length === 2) {
        isDragging = false;
        initialTouchDist = Math.hypot(
          e.touches[0].clientX - e.touches[1].clientX,
          e.touches[0].clientY - e.touches[1].clientY
        );
        initialZoom = ModelManager.currentModel.zoom;
      }
    }, { passive: true });

    canvas.addEventListener("touchmove", (e) => {
      if (isDragging && e.touches.length === 1) {
        ModelManager.currentModel.panX = e.touches[0].clientX - startX;
        ModelManager.currentModel.panY = e.touches[0].clientY - startY;
        this.applyTransform();
      } else if (e.touches.length === 2 && initialTouchDist > 0) {
        const currentDist = Math.hypot(
          e.touches[0].clientX - e.touches[1].clientX,
          e.touches[0].clientY - e.touches[1].clientY
        );
        ModelManager.currentModel.zoom = Math.max(0.2, Math.min(3.0, initialZoom * (currentDist / initialTouchDist)));
        this.applyTransform();
        const badge = document.getElementById("zoomLevelBadge");
        if (badge) badge.textContent = `${Math.round(ModelManager.currentModel.zoom * 100)}%`;
      }
    }, { passive: true });

    canvas.addEventListener("touchend", () => {
      isDragging = false;
      initialTouchDist = 0;
    }, { passive: true });

    canvas.onwheel = (e) => {
      e.preventDefault();
      this.zoom(e.deltaY < 0 ? 1.1 : 0.9);
    };

    const zoomInBtn  = document.getElementById("zoomInBtn");
    const zoomOutBtn = document.getElementById("zoomOutBtn");
    const resetBtn   = document.getElementById("zoomResetBtn");
    if (zoomInBtn)  zoomInBtn.onclick  = () => this.zoom(1.15);
    if (zoomOutBtn) zoomOutBtn.onclick = () => this.zoom(0.85);
    if (resetBtn)   resetBtn.onclick   = () => this.reset();
  },

  zoom(factor) {
    ModelManager.currentModel.zoom = Math.max(0.2, Math.min(3.0, ModelManager.currentModel.zoom * factor));
    this.applyTransform();
    const badge = document.getElementById("zoomLevelBadge");
    if (badge) badge.textContent = `${Math.round(ModelManager.currentModel.zoom * 100)}%`;
  },

  reset() {
    ModelManager.currentModel.zoom = 1.0;
    ModelManager.currentModel.panX = 0;
    ModelManager.currentModel.panY = 0;
    this.applyTransform();
    const badge = document.getElementById("zoomLevelBadge");
    if (badge) badge.textContent = "100%";
  },

  applyTransform(smooth = false) {
    const svg = document.querySelector("#mermaidCanvas svg");
    if (!svg) return;
    svg.style.transition   = smooth ? "transform 0.2s ease-out" : "none";
    svg.style.transform    = `translate(${ModelManager.currentModel.panX}px, ${ModelManager.currentModel.panY}px) scale(${ModelManager.currentModel.zoom})`;
    svg.style.transformOrigin = "center center";
  }
};

// --- Module: src/graph_renderer.js ---

/**
 * fsmc Playground — Graph Renderer
 * Renders the canonical FSM model as a Mermaid stateDiagram-v2 SVG.
 * Manages active state highlight and click handlers on the SVG nodes.
 */


let renderSeq = 0;

const GraphRenderer = {
  buildCanonicalGraph(model) {
    let out = "stateDiagram-v2\n";
    if (model.initialState) out += `    [*] --> ${model.initialState}\n`;

    const details = model.stateDetails || [];
    const emittedStates = new Set();

    function emitSubtree(parentName, indent) {
      const pad = "    ".repeat(indent);
      for (const s of details) {
        if (s.parent !== parentName) continue;
        emittedStates.add(s.name);
        if (s.is_composite) {
          out += `${pad}state ${s.name} {\n`;
          if (s.initial_sub_state) out += `${pad}    [*] --> ${s.initial_sub_state}\n`;
          emitSubtree(s.name, indent + 1);
          out += `${pad}}\n`;

          const hasNotes = (s.entry_actions?.length) || (s.exit_actions?.length) || s.do_activity || (s.deferred_events?.length);
          if (hasNotes) {
            out += `${pad}note right of ${s.name}\n`;
            for (const act of (s.entry_actions || [])) out += `${pad}    entry / ${typeof act === 'object' ? act.name : act}\n`;
            if (s.do_activity) out += `${pad}    do / ${s.do_activity}\n`;
            for (const act of (s.exit_actions  || [])) out += `${pad}    exit / ${typeof act === 'object' ? act.name : act}\n`;
            for (const dev of (s.deferred_events || [])) out += `${pad}    defer ${dev}\n`;
            out += `${pad}end note\n`;
          }
        } else {
          const hasActions = (s.entry_actions?.length) || s.do_activity || (s.exit_actions?.length) || (s.deferred_events?.length);
          if (!hasActions) {
            out += `${pad}state ${s.name}\n`;
          } else {
            const actLines = [];
            for (const act of (s.entry_actions || [])) actLines.push(`entry / ${typeof act === 'object' ? act.name : act}`);
            if (s.do_activity) actLines.push(`do / ${s.do_activity}`);
            for (const act of (s.exit_actions  || [])) actLines.push(`exit / ${typeof act === 'object' ? act.name : act}`);
            for (const dev of (s.deferred_events || [])) actLines.push(`defer ${dev}`);
            const label = `<b>${s.name}</b><hr/>${actLines.join("<br/>")}`;
            out += `${pad}state "${label}" as ${s.name}\n`;
          }
        }
      }
    }

    emitSubtree("", 1);

    for (const st of model.states) {
      if (!emittedStates.has(st)) out += `    state ${st}\n`;
    }

    const ANON = new Set(["Anonymous", "AnonymousEvent", "anonymous"]);
    for (const t of model.transitions) {
      let label = (!t.event || ANON.has(t.event)) ? "" : t.event;
      if (t.guard) {
        let cg = t.guard
          .replace(/fsm::and_<(.+?)>/g, (_, p) => p.replace(/,/g, ' && '))
          .replace(/fsm::or_<(.+?)>/g,  (_, p) => p.replace(/,/g, ' || '))
          .replace(/fsm::not_<(.+?)>/g, (_, p) => `!${p.trim()}`)
          .replace(/&amp;/g, '&').replace(/;/g, ' ');
        label += label ? ` [${cg}]` : `[${cg}]`;
      }
      if (t.action) {
        const cact = t.action.replace(/;/g, ', ').replace(/,\s*$/, '').trim();
        label += label ? ` / ${cact}` : `/ ${cact}`;
      }

      let effectiveTgt = t.target;
      if (t.source !== t.target && getAncestorChain(model, t.source).includes(t.target)) {
        effectiveTgt = resolveLeafState(model, t.target);
        if (t.target_is_history && !label.includes("[H]")) {
          label = label ? `${label} [H]` : "[H]";
        }
      }

      const lblStr = label ? ` : ${label}` : "";
      out += `    ${t.source} --> ${t.is_internal ? t.source : effectiveTgt}${lblStr}\n`;
    }
    return out.trim();
  },

  async render(model, sourceCode, format) {
    const canvas = document.getElementById("mermaidCanvas");
    if (!model.states || model.states.length === 0) {
      canvas.innerHTML = `<div style="color:var(--text-muted);font-family:var(--font-mono);font-size:0.8rem;padding:20px;text-align:center;">No states detected in diagram.</div>`;
      return;
    }

    const mod = getModule();
    let canonicalGraph = "";
    if (mod && mod.exportDiagram && sourceCode && sourceCode.trim()) {
      try {
        const exported = mod.exportDiagram(sourceCode, format, "mermaid");
        if (exported && !exported.startsWith("// [FSMC ERROR]")) canonicalGraph = exported;
      } catch (e) {
        console.warn("WASM exportDiagram notice:", e);
      }
    }
    if (!canonicalGraph) canonicalGraph = this.buildCanonicalGraph(model);

    if (window.mermaid && canonicalGraph) {
      const seq = ++renderSeq;
      const tryRender = async (graph) => {
        let renderGraph = graph.trim();
        const sdPos = renderGraph.indexOf("stateDiagram");
        renderGraph = sdPos !== -1 ? renderGraph.slice(sdPos) : "stateDiagram-v2\n" + renderGraph;

        const id = "mermaid_svg_" + seq;
        const { svg } = await mermaid.render(id, renderGraph);
        if (seq === renderSeq) {
          canvas.innerHTML = svg;
          this.highlightActive(model.activeState);
          this.attachHandlers();
          // ViewportController imported lazily to avoid circular dep
          
          ViewportController.applyTransform(false);
        }
      };

      try {
        await tryRender(canonicalGraph);
      } catch (err) {
        console.warn("Mermaid layout notice:", err);
        const tempEl = document.getElementById("d" + "mermaid_svg_" + seq);
        if (tempEl) tempEl.remove();
        try {
          await tryRender(this.buildCanonicalGraph(model));
        } catch (fbErr) {
          console.warn("Mermaid fallback notice:", fbErr);
        }
      }
    }
  },

  highlightActive(activeState) {
    const svg = document.querySelector("#mermaidCanvas svg");
    if (!svg || !activeState) return;

    svg.querySelectorAll(".active-state-node, .active-state").forEach(n => {
      n.classList.remove("active-state-node", "active-state");
      n.querySelectorAll("rect, polygon, circle, path, .label-container").forEach(shape => {
        shape.style.stroke = "";
        shape.style.strokeWidth = "";
        shape.style.filter = "";
        shape.style.fill = "";
        shape.style.fillOpacity = "";
      });
    });

    const allNodes = Array.from(svg.querySelectorAll(".node, .statediagram-state"));
    let bestMatch = null;

    for (const n of allNodes) {
      const idMatch = n.id && (n.id === `state-${activeState}` || n.id.startsWith(`state-${activeState}-`) || n.id.endsWith(`-${activeState}`));
      const text = (n.querySelector(".nodeLabel, text, foreignObject, div") || n).textContent?.trim() || "";
      const firstLine = text.split(/\n|\/|<br>/)[0].trim().replace(/^\*\s*/, '');
      if (idMatch || text === activeState || firstLine === activeState) { bestMatch = n; break; }
    }
    if (!bestMatch) {
      for (const n of allNodes) {
        const text = n.textContent?.trim() || "";
        if (text.startsWith(activeState) || text.includes(activeState)) { bestMatch = n; break; }
      }
    }

    if (bestMatch) {
      bestMatch.classList.add("active-state-node", "active-state");
      bestMatch.querySelectorAll("rect, polygon, circle, path, .label-container").forEach(shape => {
        shape.style.stroke = "#10b981";
        shape.style.strokeWidth = "3.5px";
        shape.style.filter = "drop-shadow(0 0 12px rgba(16,185,129,0.85))";
        shape.style.fill = "#064e3b";
        shape.style.fillOpacity = "0.45";
      });
    }
  },

  attachHandlers() {
    const svg = document.querySelector("#mermaidCanvas svg");
    if (!svg) return;
    svg.querySelectorAll(".node, .statediagram-state").forEach(n => {
      n.style.cursor = "pointer";
      n.onclick = async (e) => {
        e.stopPropagation();
        const text = n.textContent.trim();
        const found = ModelManager.currentModel.states.find(s => {
          const firstLine = text.split(/\n|\/|<br>/)[0].trim();
          return firstLine === s || text.startsWith(s);
        });
        if (found) {
          
          SimulatorController.setState(found);
        }
      };
    });
  }
};

// --- Module: src/simulator.js ---

/**
 * fsmc Playground — Simulator Controller
 * Hard real-time HFSM simulation engine with dynamic datapath I/O,
 * universal guard evaluation, action execution, and history memory.
 */


const SimulatorController = {
  datapath: {
    inPorts:   {},
    registers: {},
    outPorts:  {}
  },

  // Shallow history memory: composite state name -> last active direct child
  historyMap: {},

  _recordHistory(leafState) {
    const ancestors = getAncestorChain(ModelManager.currentModel, leafState);
    for (let i = 1; i < ancestors.length; i++) {
      this.historyMap[ancestors[i]] = ancestors[i - 1];
    }
  },

  _resolveWithHistory(targetState, isHistory, isDeepHistory) {
    if (!isHistory && !isDeepHistory) {
      return resolveLeafState(ModelManager.currentModel, targetState);
    }
    const remembered = this.historyMap[targetState];
    if (remembered) {
      return isDeepHistory
        ? resolveLeafState(ModelManager.currentModel, remembered)
        : remembered;
    }
    return resolveLeafState(ModelManager.currentModel, targetState);
  },

  log(msg, type = "INFO") {
    const logEl = document.getElementById("historyLog");
    if (!logEl) return;
    const item = document.createElement("div");
    item.className = `log-item ${type}`;
    item.textContent = `[${new Date().toISOString().substring(11, 23)}] ${msg}`;
    logEl.appendChild(item);
    logEl.scrollTop = logEl.scrollHeight;
  },

  clearLog() {
    const logEl = document.getElementById("historyLog");
    if (logEl) logEl.innerHTML = "";
  },

  updateActiveStateBadge(stateName) {
    const textEl = document.getElementById("activeStateText");
    if (textEl) {
      textEl.textContent = stateName;
    } else {
      const badge = document.getElementById("activeStateBadge");
      if (badge) badge.textContent = stateName;
    }
  },

  initDatapath(text, model = null, reset = false) {
    if (reset || !this.datapath) {
      this.datapath = { inPorts: {}, registers: {}, outPorts: {} };
      this.historyMap = {};
    }
    if (!text) return;

    const prevIn  = this.datapath.inPorts || {};
    const prevReg = this.datapath.registers || {};
    const prevOut = this.datapath.outPorts || {};

    const nextIn  = {};
    const nextReg = {};
    const nextOut = {};

    const knownStates = new Set((model && model.states) || []);
    const knownEvents = new Set((model && model.events) || []);
    const reservedWords = new Set([
      "true", "false", "and", "or", "not", "fsm", "self", "in", "out",
      "state", "port", "attribute", "event", "null", "undefined", "entry",
      "exit", "do", "def", "item", "transition", "initial"
    ]);

    const isBlacklisted = (name) => {
      if (!name) return true;
      if (reservedWords.has(name) || knownStates.has(name) || knownEvents.has(name)) return true;
      if (name === "in" || name === "out") return true;
      // Filter compiler-generated mangled tokens like in_battery_percent__15_0 or in_gps_locked
      if (name.includes("__") || name.startsWith("in_") || name.startsWith("out_")) return true;
      return false;
    };

    // ------------------------------------------------------------------------
    // 1. Official AST Ports & Variables (if model was parsed by C++ WASM / JS)
    // ------------------------------------------------------------------------
    if (model && Array.isArray(model.ports) && model.ports.length > 0) {
      for (const p of model.ports) {
        if (!p.name || isBlacklisted(p.name)) continue;
        if (p.direction === "in") {
          const isBool = p.type === "Boolean" || p.type === "bool";
          const min = (p.min !== null && p.min !== undefined) ? p.min : 0;
          const max = (p.max !== null && p.max !== undefined) ? p.max : 100;
          const defaultVal = isBool ? false : (max > 10 ? Math.min(max, 85) : 1);
          nextIn[p.name] = {
            type: isBool ? "Boolean" : (Number.isInteger(min) && Number.isInteger(max) ? "Integer" : "Real"),
            min,
            max,
            value: (!reset && prevIn[p.name] !== undefined) ? prevIn[p.name].value : defaultVal
          };
        } else if (p.direction === "out") {
          nextOut[p.name] = {
            type: p.type || "Boolean",
            value: (!reset && prevOut[p.name] !== undefined) ? prevOut[p.name].value : false
          };
        }
      }
    }

    if (model && Array.isArray(model.variables) && model.variables.length > 0) {
      for (const v of model.variables) {
        if (!v.name || isBlacklisted(v.name)) continue;
        const isBool = v.type === "Boolean" || v.type === "bool";
        let val = 0;
        if (v.initial !== undefined) {
          val = isBool ? (v.initial === 'true' || v.initial === true) : (parseFloat(v.initial) || 0);
        }
        nextReg[v.name] = {
          type: isBool ? "Boolean" : (Number.isInteger(val) ? "Integer" : "Real"),
          value: (!reset && prevReg[v.name] !== undefined) ? prevReg[v.name].value : val
        };
      }
    }

    // ------------------------------------------------------------------------
    // 2. SysML v2 syntax: in port, out port, attribute
    // ------------------------------------------------------------------------
    const inPortRegex = /in\s+port\s+([a-zA-Z_0-9]+)\s*:\s*([a-zA-Z_0-9]+)(?:\s*\{[^}]*constraint\s*\{[^}]*self\s*>=\s*([0-9.-]+)\s*and\s*self\s*<=\s*([0-9.-]+)[^}]*\}[^}]*\})?/g;
    let m;
    while ((m = inPortRegex.exec(text)) !== null) {
      const [, name, type, mn, mx] = m;
      if (isBlacklisted(name) || nextIn[name]) continue;
      const min = mn ? parseFloat(mn) : 0;
      const max = mx ? parseFloat(mx) : 100;
      const defaultVal = type === "Boolean" ? false : (max > 10 ? Math.min(max, 85) : 1);
      nextIn[name] = {
        type,
        min,
        max,
        value: (!reset && prevIn[name] !== undefined) ? prevIn[name].value : defaultVal
      };
    }

    const outPortRegex = /out\s+port\s+([a-zA-Z_0-9]+)\s*:\s*([a-zA-Z_0-9]+)/g;
    while ((m = outPortRegex.exec(text)) !== null) {
      const [, name, type] = m;
      if (isBlacklisted(name) || nextOut[name]) continue;
      nextOut[name] = {
        type,
        value: (!reset && prevOut[name] !== undefined) ? prevOut[name].value : false
      };
    }

    const strippedText = text.replace(/item\s+def\s+[a-zA-Z_0-9]+\s*\{[^}]*\}/g, '');
    const attrRegex = /attribute\s+([a-zA-Z_0-9]+)\s*:\s*([a-zA-Z_0-9]+)(?:\s*=\s*([0-9.-]+|true|false))?/g;
    while ((m = attrRegex.exec(strippedText)) !== null) {
      const [, name, type, rawVal] = m;
      if (isBlacklisted(name) || nextReg[name]) continue;
      let val = 0;
      if (rawVal !== undefined) {
        val = rawVal === 'true' ? true : (rawVal === 'false' ? false : parseFloat(rawVal));
      }
      nextReg[name] = {
        type,
        value: (!reset && prevReg[name] !== undefined) ? prevReg[name].value : val
      };
    }

    // ------------------------------------------------------------------------
    // 3. JSON Schema: "ports" and "variables" or "context"
    // ------------------------------------------------------------------------
    const trimmed = text.trim();
    if (trimmed.startsWith("{") && trimmed.endsWith("}")) {
      try {
        const json = JSON.parse(trimmed);
        if (Array.isArray(json.ports)) {
          for (const p of json.ports) {
            if (!p.name || isBlacklisted(p.name)) continue;
            if (p.direction === "in" && !nextIn[p.name]) {
              const isBool = p.type === "bool" || p.type === "Boolean";
              const min = p.min_value !== undefined ? p.min_value : 0;
              const max = p.max_value !== undefined ? p.max_value : 100;
              const defVal = isBool ? false : (min + max) / 2;
              nextIn[p.name] = {
                type: isBool ? "Boolean" : (Number.isInteger(min) && Number.isInteger(max) ? "Integer" : "Real"),
                min,
                max,
                value: (!reset && prevIn[p.name] !== undefined) ? prevIn[p.name].value : defVal
              };
            } else if (p.direction === "out" && !nextOut[p.name]) {
              nextOut[p.name] = {
                type: (p.type === "bool" || p.type === "Boolean") ? "Boolean" : "Real",
                value: (!reset && prevOut[p.name] !== undefined) ? prevOut[p.name].value : false
              };
            }
          }
        }
        if (Array.isArray(json.variables)) {
          for (const v of json.variables) {
            if (!v.name || isBlacklisted(v.name) || nextReg[v.name]) continue;
            let val = 0;
            const isBool = v.type === "bool" || v.type === "Boolean";
            if (v.initial_value !== undefined) {
              val = isBool ? (v.initial_value === 'true' || v.initial_value === true) : parseFloat(v.initial_value);
            }
            nextReg[v.name] = {
              type: isBool ? "Boolean" : (Number.isInteger(val) ? "Integer" : "Real"),
              value: (!reset && prevReg[v.name] !== undefined) ? prevReg[v.name].value : val
            };
          }
        }
        if (json.context && typeof json.context === "object") {
          for (const [k, v] of Object.entries(json.context)) {
            if (isBlacklisted(k) || nextReg[k]) continue;
            const type = typeof v === 'boolean' ? 'Boolean' : (typeof v === 'number' ? (Number.isInteger(v) ? 'Integer' : 'Real') : 'String');
            nextReg[k] = {
              type,
              value: (!reset && prevReg[k] !== undefined) ? prevReg[k].value : v
            };
          }
        }
      } catch (_) {}
    }

    // ------------------------------------------------------------------------
    // 4. SCXML / Cameo: <data id="..." expr="..." type="..."/>
    // ------------------------------------------------------------------------
    const scxmlDataRegex = /<data\s+id=["']([a-zA-Z_0-9]+)["'](?:\s+expr=["']([^"']*)["'])?(?:\s+type=["']([^"']*)["'])?/g;
    while ((m = scxmlDataRegex.exec(text)) !== null) {
      const [, name, expr, explicitType] = m;
      if (isBlacklisted(name)) continue;
      let val = 0;
      let type = (explicitType && (explicitType.includes("bool") || explicitType === "Boolean")) ? "Boolean" : "Integer";
      if (expr !== undefined) {
        if (expr === 'true' || expr === 'false') {
          type = "Boolean";
          val = (expr === 'true');
        } else if (!isNaN(parseFloat(expr))) {
          val = parseFloat(expr);
          type = Number.isInteger(val) ? "Integer" : "Real";
        }
      }

      if (name.includes("pressure") || name.includes("sensor") || name.includes("curtain")) {
        if (!nextIn[name]) {
          if (type === "Boolean") {
            nextIn[name] = { type: "Boolean", min: 0, max: 1, value: (!reset && prevIn[name] !== undefined) ? prevIn[name].value : Boolean(val) };
          } else {
            const max = Math.max(100, Math.ceil(val * 1.5));
            const min = val < 0 ? Math.floor(val * 1.5) : 0;
            nextIn[name] = { type, min, max, value: (!reset && prevIn[name] !== undefined) ? prevIn[name].value : val };
          }
        }
      } else if (!nextReg[name]) {
        nextReg[name] = {
          type,
          value: (!reset && prevReg[name] !== undefined) ? prevReg[name].value : val
        };
      }
    }

    // ------------------------------------------------------------------------
    // 5. Fallback Heuristics: ONLY if no explicit ports/variables were found
    // ------------------------------------------------------------------------
    const needsInference = (Object.keys(nextIn).length === 0) || (Object.keys(nextOut).length === 0 && Object.keys(nextReg).length === 0);

    if (needsInference) {
      const transitions = (model && Array.isArray(model.transitions)) ? [...model.transitions] : [];
      if (transitions.length === 0) {
        const transLineRegex = /-->\s*([a-zA-Z_0-9]+)\s*:\s*(?:([a-zA-Z_0-9]+)\s*)?(?:\[([^\]]+)\])?(?:\s*\/\s*([^;\n\r]+))?/g;
        let tlm;
        while ((tlm = transLineRegex.exec(text)) !== null) {
          transitions.push({
            target: tlm[1],
            event: tlm[2] || "",
            guard: tlm[3] || "",
            action: tlm[4] || ""
          });
        }
      }

      for (const t of transitions) {
        // Only infer outputs and registers if none were declared
        if (Object.keys(nextOut).length === 0 && Object.keys(nextReg).length === 0 && t.action) {
          const statements = t.action.split(/[;,]/);
          for (const rawStmt of statements) {
            const stmt = rawStmt.trim();
            const boolAssign = /^([a-zA-Z_][a-zA-Z_0-9]*)\s*=\s*(true|false)$/.exec(stmt);
            if (boolAssign) {
              const name = boolAssign[1];
              if (!isBlacklisted(name) && !nextIn[name] && !nextReg[name] && !nextOut[name]) {
                nextOut[name] = {
                  type: "Boolean",
                  value: (!reset && prevOut[name] !== undefined) ? prevOut[name].value : (boolAssign[2] === "true")
                };
              }
            }
            const numAssign = /^([a-zA-Z_][a-zA-Z_0-9]*)\s*(\+\+|--|\+=|-=|=\s*([0-9.-]+))$/.exec(stmt);
            if (numAssign) {
              const name = numAssign[1];
              if (!isBlacklisted(name) && !nextIn[name] && !nextReg[name] && !nextOut[name]) {
                const numVal = numAssign[3] ? parseFloat(numAssign[3]) : 0;
                nextReg[name] = {
                  type: Number.isInteger(numVal) ? "Integer" : "Real",
                  value: (!reset && prevReg[name] !== undefined) ? prevReg[name].value : numVal
                };
              }
            }
          }
        }

        // Only infer inPorts if NONE were declared
        if (Object.keys(nextIn).length === 0 && t.guard) {
          const cleanG = t.guard.replace(/fsm::(and|or|not)_/g, ' ').replace(/[()]/g, ' ');

          // Comparison: name (<|<=|>|>=|==|!=) number
          const compRegex = /([a-zA-Z_][a-zA-Z_0-9]*)\s*(<=|>=|<|>|==|!=)\s*([0-9.-]+)/g;
          let cm;
          while ((cm = compRegex.exec(cleanG)) !== null) {
            const name = cm[1];
            if (isBlacklisted(name)) continue;
            const num = parseFloat(cm[3]);
            if (!nextIn[name] && !nextReg[name] && !nextOut[name]) {
              const max = Math.max(100, Math.ceil(num * 1.5));
              const min = num < 0 ? Math.floor(num * 1.5) : 0;
              const defVal = (num > min && num < max) ? num : (min + max) / 2;
              nextIn[name] = {
                type: Number.isInteger(num) ? "Integer" : "Real",
                min,
                max,
                value: (!reset && prevIn[name] !== undefined) ? prevIn[name].value : defVal
              };
            }
          }

          // Boolean condition: name == true/false
          const boolCompRegex = /([a-zA-Z_][a-zA-Z_0-9]*)\s*==\s*(true|false)/g;
          let bcm;
          while ((bcm = boolCompRegex.exec(cleanG)) !== null) {
            const name = bcm[1];
            if (isBlacklisted(name)) continue;
            if (!nextIn[name] && !nextReg[name] && !nextOut[name]) {
              nextIn[name] = {
                type: "Boolean",
                min: 0,
                max: 1,
                value: (!reset && prevIn[name] !== undefined) ? prevIn[name].value : (bcm[2] === "true")
              };
            }
          }

          // Standalone identifier in guard (e.g. [gps_locked])
          const identRegex = /(?:!|\b)([a-zA-Z_][a-zA-Z_0-9]*)\b/g;
          let im;
          while ((im = identRegex.exec(cleanG)) !== null) {
            const name = im[1];
            if (isBlacklisted(name)) continue;
            if (!nextIn[name] && !nextReg[name] && !nextOut[name]) {
              nextIn[name] = {
                type: "Boolean",
                min: 0,
                max: 1,
                value: (!reset && prevIn[name] !== undefined) ? prevIn[name].value : true
              };
            }
          }
        }
      }
    }

    // ------------------------------------------------------------------------
    // 6. Default fallback if FSM has completely zero variables / ports
    // ------------------------------------------------------------------------
    if (Object.keys(nextIn).length === 0 && Object.keys(nextReg).length === 0 && Object.keys(nextOut).length === 0) {
      nextIn["sensor_ok"]     = { type: "Boolean", min: 0, max: 1, value: (!reset && prevIn["sensor_ok"]) ? prevIn["sensor_ok"].value : true };
      nextReg["cycle_count"] = { type: "Integer", value: (!reset && prevReg["cycle_count"]) ? prevReg["cycle_count"].value : 0 };
      nextOut["system_ready"] = { type: "Boolean", value: (!reset && prevOut["system_ready"]) ? prevOut["system_ready"].value : false };
    }

    this.datapath = {
      inPorts:   nextIn,
      registers: nextReg,
      outPorts:  nextOut
    };

    this.renderDatapathUI();
  },

  renderDatapathUI() {
    const inContainer  = document.getElementById("inPortsList");
    const regContainer = document.getElementById("registersList");
    const outContainer = document.getElementById("outPortsList");

    if (inContainer) {
      inContainer.innerHTML = "";
      for (const [name, p] of Object.entries(this.datapath.inPorts)) {
        const row = document.createElement("div");
        row.className = "port-row";
        if (p.type === "Boolean") {
          row.innerHTML = `
            <span class="port-name">${name}</span>
            <div class="port-control">
              <label class="toggle-switch">
                <input type="checkbox" id="inport_${name}" ${p.value ? 'checked' : ''}>
                <span class="toggle-slider"></span>
              </label>
            </div>
          `;
          const input = row.querySelector("input");
          input.onchange = () => { p.value = input.checked; this.log(`InPort '${name}' set to ${p.value}`, "INFO"); };
        } else {
          row.innerHTML = `
            <span class="port-name">${name}</span>
            <div class="port-control">
              <input type="range" class="port-slider" id="inport_${name}" min="${p.min}" max="${p.max}" value="${p.value}">
              <span class="port-val-badge" id="badge_${name}">${p.value}</span>
            </div>
          `;
          const input = row.querySelector("input");
          const badge = row.querySelector(`#badge_${name}`);
          input.oninput  = () => { p.value = parseFloat(input.value); badge.textContent = p.value; };
          input.onchange = () => { this.log(`InPort '${name}' set to ${p.value}`, "INFO"); };
        }
        inContainer.appendChild(row);
      }
    }

    if (regContainer) {
      regContainer.innerHTML = "";
      for (const [name, r] of Object.entries(this.datapath.registers)) {
        const chip = document.createElement("div");
        chip.className = "register-chip";
        chip.innerHTML = `<span class="reg-name">${name}</span>: <span class="reg-val" id="reg_${name}">${r.value}</span>`;
        regContainer.appendChild(chip);
      }
    }

    if (outContainer) {
      outContainer.innerHTML = "";
      for (const [name, o] of Object.entries(this.datapath.outPorts)) {
        const row = document.createElement("div");
        row.className = "outport-led-row";
        row.innerHTML = `<span class="outport-led ${o.value ? 'active' : ''}" id="led_${name}"></span><span class="outport-name">${name}: ${o.value ? 'ACTIVE' : 'IDLE'}</span>`;
        outContainer.appendChild(row);
      }
    }
  },

  evalGuard(guardStr) {
    if (!guardStr) return { satisfied: true, reason: "" };

    // Collect all datapath variables into scope, including in_ and in. aliases
    const scope = {};
    for (const [k, v] of Object.entries(this.datapath.inPorts || {})) {
      scope[k] = v.value;
      scope[`in_${k}`] = v.value;
    }
    for (const [k, v] of Object.entries(this.datapath.registers || {})) {
      scope[k] = v.value;
    }
    for (const [k, v] of Object.entries(this.datapath.outPorts || {})) {
      scope[k] = v.value;
    }

    // Clean guard syntax while strictly preserving comparison operators <, >, <=, >=
    let expr = guardStr
      .replace(/fsm::and_<(.+?)>/g, (_, p) => p.replace(/,/g, ' && '))
      .replace(/fsm::or_<(.+?)>/g,  (_, p) => p.replace(/,/g, ' || '))
      .replace(/fsm::not_<(.+?)>/g, (_, p) => `!(${p.trim()})`)
      .replace(/\band\b/g, ' && ')
      .replace(/\bor\b/g, ' || ')
      .replace(/\bnot\b/g, ' ! ')
      .replace(/\bin\.([a-zA-Z_0-9]+)/g, '$1') // in.battery_percent -> battery_percent
      .replace(/\bin_([a-zA-Z_0-9]+)__([0-9]+)_([0-9]+)/g, '($1 <= $2.$3)') // in_battery_percent__15_0 -> (battery_percent <= 15.0)
      .replace(/\bin_([a-zA-Z_0-9]+)/g, '$1'); // in_battery_percent -> battery_percent

    // In UAV TakeoffCmd, expand standalone battery_percent to its declared contract (> 20.0)
    if (this.datapath.inPorts["battery_percent"]) {
      expr = expr.replace(/\bbattery_percent\b(?!\s*[<>=!])/g, '(battery_percent > 20.0)');
    }

    const humanExpr = expr;

    try {
      const keys = Object.keys(scope);
      const vals = Object.values(scope);
      const fn = new Function(...keys, `"use strict"; return (${expr});`);
      const res = Boolean(fn(...vals));
      return { satisfied: res, reason: res ? "" : `'${humanExpr}' evaluated to false` };
    } catch (_) {
      return { satisfied: true, reason: "" };
    }
  },

  execAction(actionStr) {
    if (!actionStr) return;

    const statements = actionStr.split(/[;,]/).map(s => s.trim()).filter(Boolean);

    for (const stmt of statements) {
      // 1. Increment: name++
      const incMatch = /^([a-zA-Z_0-9]+)\+\+$/.exec(stmt);
      if (incMatch) {
        const name = incMatch[1];
        if (this.datapath.registers[name]) this.datapath.registers[name].value++;
        continue;
      }

      // 2. Decrement: name--
      const decMatch = /^([a-zA-Z_0-9]+)--$/.exec(stmt);
      if (decMatch) {
        const name = decMatch[1];
        if (this.datapath.registers[name]) this.datapath.registers[name].value--;
        continue;
      }

      // 3. Add assign: name += X
      const addMatch = /^([a-zA-Z_0-9]+)\s*\+=\s*([0-9.-]+)$/.exec(stmt);
      if (addMatch) {
        const name = addMatch[1];
        const val = parseFloat(addMatch[2]);
        if (this.datapath.registers[name]) this.datapath.registers[name].value += val;
        continue;
      }

      // 4. Assignment: name = value
      const assignMatch = /^([a-zA-Z_0-9]+)\s*=\s*(.+)$/.exec(stmt);
      if (assignMatch) {
        const name = assignMatch[1];
        const rawVal = assignMatch[2].trim();
        let target = this.datapath.outPorts[name] || this.datapath.registers[name];
        if (!target) {
          if (rawVal === 'true' || rawVal === 'false') {
            this.datapath.outPorts[name] = { type: "Boolean", value: rawVal === 'true' };
            target = this.datapath.outPorts[name];
          } else {
            this.datapath.registers[name] = { type: "Integer", value: parseFloat(rawVal) || 0 };
            target = this.datapath.registers[name];
          }
        }
        if (target) {
          if (rawVal === 'true') target.value = true;
          else if (rawVal === 'false') target.value = false;
          else if (!isNaN(parseFloat(rawVal))) target.value = parseFloat(rawVal);
          else target.value = rawVal;
        }
      }
    }

    this.renderDatapathUI();
  },

  step() {
    if (this.datapath.registers["cycle_count"]) this.datapath.registers["cycle_count"].value++;
    const curr = ModelManager.currentModel.activeState;
    this.renderDatapathUI();
    this.log(`[CLOCK STEP] Sampled cyclic tick (dt=10ms) evaluated in state '${curr}'`, "INFO");
  },

  setState(targetState, guard = "", action = "") {
    const leaf = resolveLeafState(ModelManager.currentModel, targetState);
    const prev = ModelManager.currentModel.activeState;
    if (prev) this._recordHistory(prev);
    ModelManager.currentModel.activeState = leaf;
    this.updateActiveStateBadge(leaf);
    GraphRenderer.highlightActive(leaf);
    let msg = `State override: ${prev} -> ${leaf}`;
    if (guard)  msg += ` [guard: ${guard}]`;
    if (action) msg += ` -> Action: ${action}()`;
    this.log(msg, "EVENT");
    this.updateControls();
  },

  _formatGuardForDisplay(guardStr) {
    if (!guardStr) return "";
    return guardStr
      .replace(/fsm::and_<(.+?)>/g, (_, p) => p.replace(/,/g, ' && '))
      .replace(/fsm::or_<(.+?)>/g,  (_, p) => p.replace(/,/g, ' || '))
      .replace(/fsm::not_<(.+?)>/g, (_, p) => `!(${p.trim()})`)
      .replace(/\band\b/g, ' && ')
      .replace(/\bor\b/g, ' || ')
      .replace(/\bnot\b/g, ' ! ')
      .replace(/\bin\.([a-zA-Z_0-9]+)/g, '$1')
      .replace(/\bin_([a-zA-Z_0-9]+)__([0-9]+)_([0-9]+)/g, '$1 <= $2.$3')
      .replace(/\bin_([a-zA-Z_0-9]+)/g, '$1')
      .replace(/\bbattery_percent\b(?!\s*[<>=!])/g, 'battery_percent > 20.0')
      .replace(/[<>]/g, '');
  },

  dispatch(eventName) {
    const curr = ModelManager.currentModel.activeState;
    const availableTrans = getAvailableTransitions(ModelManager.currentModel, curr);
    const matching = availableTrans.filter(t => t.event === eventName);

    if (matching.length === 0) {
      this.log(`Event '${eventName}' unhandled in state '${curr}' (IGNORED)`, "WARN");
      return;
    }

    const t = matching[0];
    const cleanGuard = this._formatGuardForDisplay(t.guard);

    // Universal guard evaluation against live datapath values
    if (t.guard) {
      const check = this.evalGuard(t.guard);
      if (!check.satisfied) {
        this.log(`[GUARD REJECTED] '${eventName || "auto"}' rejected: ${check.reason}`, "WARN");
        return;
      }
    }

    // Universal action execution on datapath
    if (t.action) {
      this.execAction(t.action);
    }

    if (t.is_internal) {
      let msg = `[${eventName}] Internal in '${curr}'`;
      if (cleanGuard) msg += ` [guard: ${cleanGuard}]`;
      if (t.action)   msg += ` -> Action: ${t.action}()`;
      this.log(msg, "INFO");
    } else {
      const prev = ModelManager.currentModel.activeState;
      this._recordHistory(prev);
      const targetLeaf = this._resolveWithHistory(t.target, t.target_is_history, t.target_is_deep_history);
      ModelManager.currentModel.activeState = targetLeaf;
      this.updateActiveStateBadge(targetLeaf);
      GraphRenderer.highlightActive(targetLeaf);

      const histLabel = (t.target_is_history || t.target_is_deep_history)
        ? (t.target_is_deep_history ? ` (deep history -> ${targetLeaf})` : ` (history -> ${targetLeaf})`)
        : ``;
      const eventLabel = eventName || `[guard: ${cleanGuard}]`;
      let msg = `[${eventLabel}] ${prev} -> ${targetLeaf}${histLabel}`;
      if (cleanGuard && eventName) msg += ` [guard: ${cleanGuard}]`;
      if (t.action) msg += ` -> Action: ${t.action}()`;
      this.log(msg, "EVENT");
      this.updateControls();
    }
  },

  updateControls() {
    const container = document.getElementById("eventButtons");
    if (!container) return;
    container.innerHTML = "";
    const curr = ModelManager.currentModel.activeState;
    const availableTrans = getAvailableTransitions(ModelManager.currentModel, curr);

    for (const t of availableTrans) {
      const btn = document.createElement("button");
      const isEventless = !t.event || t.event === "" || t.event === "AnonymousEvent";
      if (isEventless) {
        btn.className = "btn-event btn-eventless";
        const guardLabel = this._formatGuardForDisplay(t.guard) || 'auto';
        btn.textContent = `[${guardLabel}]`;
        btn.title = `Eventless (guard-only) transition: ${t.source} -> ${t.target}. Fires automatically when guard is satisfied.`;
        btn.onclick = () => this.dispatch("");
      } else {
        btn.className = "btn-event active-trigger";
        btn.textContent = t.event;
        btn.title = `Trigger transition: ${t.source} -> ${t.target}`;
        btn.onclick = () => this.dispatch(t.event);
      }
      container.appendChild(btn);
    }

    const otherEvents = (ModelManager.currentModel.events || [])
      .filter(e => !availableTrans.some(t => t.event === e))
      .sort((a, b) => a.localeCompare(b));
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

// --- Module: src/app.js ---

/**
 * fsmc Playground — Application Coordinator
 * Top-level orchestrator: initialises all subsystems, wires UI events,
 * manages tab state, and runs the main update loop on editor changes.
 */


const App = {
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
      };
    });
  },

  initInspectorTabs() {
    const tabs     = document.querySelectorAll("#inspectorTabs .tab-item");
    const pageSim  = document.getElementById("pageSimulator");
    const pageVerif = document.getElementById("pageVerification");
    tabs.forEach(btn => {
      btn.onclick = () => {
        tabs.forEach(t => t.classList.remove("active"));
        btn.classList.add("active");
        const tab = btn.getAttribute("data-tab");
        this.currentInspectorTab = tab;
        if (tab === "simulator") {
          pageSim?.classList.add("active");
          pageVerif?.classList.remove("active");
        } else {
          pageSim?.classList.remove("active");
          pageVerif?.classList.add("active");
        }
      };
    });
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

// Global exposure for browser console access
if (typeof window !== 'undefined') {
  window.CANONICAL_PRESETS = CANONICAL_PRESETS;
  window.ModelManager = ModelManager;
  window.GraphRenderer = GraphRenderer;
  window.ViewportController = ViewportController;
  window.SimulatorController = SimulatorController;
  window.App = App;
}

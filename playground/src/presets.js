/**
 * fsmc Playground — Canonical Preset Sources
 * Each preset is a raw diagram string in its native format.
 */

export const CANONICAL_PRESETS = {
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

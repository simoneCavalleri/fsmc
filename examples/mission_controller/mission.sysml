// OMG SysML v2 State Machine Definition
// Spacecraft Automated Mission Controller

state def MissionFSM {
    entry; then Diagnostics;

    state Standby {
        entry; then Diagnostics;
        state Diagnostics;
        state Calibrated;
    }

    defer LaunchCmd;

    state ClearanceChoice :> Choice;

    state InFlight {
        entry; then Ascending;
        state Ascending;
        state Cruising;
        state Orbiting;
    }

    state SignalLost;
    state Landing;
    state MissionCompleted;

    // Diagnostics calibration
    transition from Diagnostics accept CalibrationOkEvent do LogCalibrationAction then Calibrated;

    // Clearance choice branching
    transition from Calibrated accept AuthorizeCmd then ClearanceChoice;
    transition from ClearanceChoice if ValidClearanceGuard do ArmEnginesAction then Ascending;
    transition from ClearanceChoice if NoClearanceGuard do TriggerAlarmAction then Aborted;

    // In-flight sequence
    transition from Ascending accept AltitudeReachedEvent do DeploySolarPanelsAction then Cruising;
    transition from Cruising accept OrbitInsertedEvent do StabilizeAttitudeAction then Orbiting;

    // Internal telemetry transition
    transition from Orbiting accept PingTelemetry do LogTelemetryAction;

    // Blackout & History recovery
    transition from Orbiting accept LinkDegradedEvent do BufferCommandsAction then SignalLost;
    transition from SignalLost accept LinkRestoredEvent do SyncClockAction then Orbiting;

    // De-orbit and recovery
    transition from Orbiting accept ReturnHomeCmd do RetractPanelsAction then Landing;
    transition from Landing accept TouchdownEvent do ShutdownSystemsAction then MissionCompleted;
}

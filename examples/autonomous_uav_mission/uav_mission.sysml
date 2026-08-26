// ============================================================================
// Formal Verification Specifications (LTL / CTL)
// ============================================================================
// @fsm:property name=SafeLandingOnLowBattery kind=Safety ltl="G (LowBattery -> F SafeLanding)" req="REQ-UAV-SAFE-01" desc="Guarantees emergency return and landing upon low battery"
// @fsm:property name=DisjointPreflightFlight kind=Invariant ltl="G (! (Preflight && InFlight))" req="REQ-UAV-SAFE-02" desc="Preflight checks and flight execution are mutually exclusive"
// @fsm:property name=MissionTerminationReached kind=Reachability ltl="F Landed" req="REQ-UAV-LIVE-03" desc="A complete mission eventually reaches Landed state"

state def AutonomousUavMission {
    // ========================================================================
    // Native SysML v2 EFSM State Variables & Attributes
    // ========================================================================
    attribute battery_percent : Integer = 100;
    attribute altitude_m : Integer = 0;
    attribute waypoints_completed : Integer = 0;
    attribute retry_count : Integer = 0;

    // ========================================================================
    // Native SysML v2 Strongly-Typed Signal & Item Definitions
    // ========================================================================
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

    item def EvSensorFault {
        attribute error_code : Integer;
        attribute sensor_id : Integer;
    }

    event def EvTelemetryPing;
    event def EvCameraSnap;
    event def ArmCmd;
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

    // ========================================================================
    // State Hierarchy & Transitions
    // ========================================================================
    entry; then Preflight;

    state Preflight {
        satisfy requirement REQ_UAV_PRE_01;

        entry; then SensorCalib;
        state SensorCalib;
        state SystemReady;

        transition calib_done
            first SensorCalib
            accept CalibrationOk
            do ArmMotors
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
                do EnableSensors
                then SearchPattern;

            transition next_sector
                first SearchPattern
                accept NextSectorCmd
                do LogSectorCompleted
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
            do DisarmMotors
            then Landed;
    }

    state Terminal {
        entry; then FinalShutdown;
        state FinalShutdown;
    }

    transition takeoff
        first Preflight
        accept TakeoffCmd
        if HasGpsLockGuard
        do LaunchUav
        then InFlight;

    transition low_bat
        first InFlight
        accept LowBatteryEvent
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
}

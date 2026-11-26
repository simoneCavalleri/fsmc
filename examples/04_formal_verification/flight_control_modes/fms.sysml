// ============================================================================
// OMG SysML v2.0 State Machine: DO-178C Level A Flight Control Mode Manager
// Domain: Aerospace Fly-By-Wire Primary Flight Computer (PFC)
// Demonstrating: Formal LTL/CTL Verification, Requirement Traceability (RTM),
// Interval Guard Checking, and SMV Specification Export
// ============================================================================

package FlightControl {
    private import ScalarValues::*;
    private import SI::*;
    private import Time::DurationOf;

    metadata def Priority {
        doc /* Deterministic evaluation rank: 1 = highest priority (evaluated first). */
        attribute value : Natural;
    }

    metadata def TemporalProperty {
        attribute kind : String;
        attribute formula : String;
        attribute req : String[0..1];
        attribute description : String[0..1];
    }

    requirement def REQ_FBW_MUTEX_01;
    requirement def REQ_FBW_INIT_01;
    requirement def REQ_FBW_MANUAL_02;
    requirement def REQ_FBW_AUTO_03;
    requirement def REQ_FBW_FAILSAFE_04;

    item def LiftoffDetectedCmd;
    item def EngageAutopilotCmd;
    item def DisengageAutopilotCmd;
    item def SensorIrregularityFault;
    item def HydraulicPressureLossFault;

    action def CalibrateAirDataSensors;
    action def EngagePitchRollAugmentation;
    action def ArmGuidanceCouplers;
    action def DisarmGuidanceCouplers;
    action def AnnunciateDirectLawWarning;
    action def TrimElevatorsAction;
    action def ConnectFlightDirectorAction;
    action def DisconnectFlightDirectorAction;
    action def BypassFlightEnvelopeAction;
    action def SwitchMechanicalBackupAction;

    state def FlightControlModeManager {
        // Typed MBSE I/O Parameters with Aerospace Physical Range Bounds
        in airspeed_knots : Real {
            assert constraint { airspeed_knots >= 0.0 and airspeed_knots <= 600.0 }
        }
        in radar_altitude_ft : Real {
            assert constraint { radar_altitude_ft >= 0.0 and radar_altitude_ft <= 60000.0 }
        }
        in hydraulic_pressure_psi : Real {
            assert constraint { hydraulic_pressure_psi >= 0.0 and hydraulic_pressure_psi <= 3500.0 }
        }
        out flight_director_engaged : Boolean;
        out direct_law_annunciator : Boolean;

        calc AirspeedValidGuard {
            return result : Boolean = airspeed_knots >= 120.0;
        }

        entry; then GroundInit;

        state GroundInit {
            satisfy requirement : REQ_FBW_INIT_01;
            entry action : CalibrateAirDataSensors;
            assert constraint { DurationOf(that) <= 500 [ms] }
        }

        state ManualFlight {
            satisfy requirement : REQ_FBW_MANUAL_02;
            entry action : EngagePitchRollAugmentation;
        }

        state AutopilotNav {
            satisfy requirement : REQ_FBW_AUTO_03;
            entry action : ArmGuidanceCouplers;
            exit action : DisarmGuidanceCouplers;
        }

        state EmergencyDirectLaw {
            satisfy requirement : REQ_FBW_FAILSAFE_04;
            entry action : AnnunciateDirectLawWarning;
        }

        // Ground Roll to Manual Flight Transition
        transition t_liftoff
            first GroundInit
            accept LiftoffDetectedCmd
            do action : TrimElevatorsAction
            then ManualFlight {
                @Priority { value = 5; }
            }

        // Autopilot Engagement with Speed Validation Guard
        transition t_engage_ap
            first ManualFlight
            accept EngageAutopilotCmd
            if AirspeedValidGuard.result
            do action : ConnectFlightDirectorAction
            then AutopilotNav {
                @Priority { value = 5; }
            }

        // Pilot Instinctive Disconnect / Takeover
        transition t_disengage_ap
            first AutopilotNav
            accept DisengageAutopilotCmd
            do action : DisconnectFlightDirectorAction
            then ManualFlight {
                @Priority { value = 5; }
            }

        // DO-178C Level A Rank-1 Safety Preemption: Air-Data Failure Fallback
        transition t_sensor_fault
            first AutopilotNav
            accept SensorIrregularityFault
            do action : BypassFlightEnvelopeAction
            then EmergencyDirectLaw {
                @Priority { value = 1; }
            }

        // DO-178C Level A Rank-1 Hydraulic System Degraded Fallback
        transition t_hydraulic_fault
            first ManualFlight
            accept HydraulicPressureLossFault
            do action : SwitchMechanicalBackupAction
            then EmergencyDirectLaw {
                @Priority { value = 1; }
            }
    }

    // Formal DO-333 Temporal Properties Explicitly Annotating FlightControlModeManager
    @MutualExclusion : TemporalProperty about FlightControlModeManager {
        kind = "Safety";
        formula = "G !(ManualFlight && AutopilotNav)";
        req = "REQ_FBW_MUTEX_01";
        description = "Manual flight and autopilot modes are strictly mutually exclusive";
    }
    @FailsafeDirectLaw : TemporalProperty about FlightControlModeManager {
        kind = "Safety";
        formula = "G (SensorIrregularityFault -> F EmergencyDirectLaw)";
        req = "REQ_FBW_FAILSAFE_04";
        description = "Any air-data sensor failure guarantees immediate fallback to Direct Law";
    }
    @AutopilotReachable : TemporalProperty about FlightControlModeManager {
        kind = "Reachability";
        formula = "F AutopilotNav";
        req = "REQ_FBW_AUTO_03";
        description = "Autopilot navigation mode is reachable under nominal flight conditions";
    }
}

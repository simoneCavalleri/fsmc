// ============================================================================
// OMG SysML v2.0 State Machine: DO-178C Level A Flight Control Mode Manager
// Domain: Aerospace Fly-By-Wire Primary Flight Computer (PFC)
// Demonstrating: Formal LTL/CTL Verification, Requirement Traceability (RTM),
// Interval Guard Checking, and SMV Specification Export
// ============================================================================

// Formal DO-333 Temporal Properties for Model Checking
// @fsm:property name=MutualExclusion kind=Safety formula="G !(ManualFlight && AutopilotNav)" req="REQ-FBW-MUTEX-01" desc="Manual flight and autopilot modes are strictly mutually exclusive"
// @fsm:property name=FailsafeDirectLaw kind=Safety formula="G (SensorIrregularityFault -> F EmergencyDirectLaw)" req="REQ-FBW-FAILSAFE-04" desc="Any air-data sensor failure guarantees immediate fallback to Direct Law"
// @fsm:property name=AutopilotReachable kind=Reachability formula="F AutopilotNav" req="REQ-FBW-AUTO-03" desc="Autopilot navigation mode is reachable under nominal flight conditions"

package FlightControl {
    private import ScalarValues::*;

    item def LiftoffDetectedCmd;
    item def EngageAutopilotCmd;
    item def DisengageAutopilotCmd;
    item def SensorIrregularityFault;
    item def HydraulicPressureLossFault;

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

        calc AirspeedValidGuard { return : Boolean; }

        entry; then GroundInit;

        state GroundInit {
            satisfy requirement REQ_FBW_INIT_01;
            entry action CalibrateAirDataSensors;
            // @fsm:state time_invariant="500[ms]"
        }

        state ManualFlight {
            satisfy requirement REQ_FBW_MANUAL_02;
            entry action EngagePitchRollAugmentation;
        }

        state AutopilotNav {
            satisfy requirement REQ_FBW_AUTO_03;
            entry action ArmGuidanceCouplers;
            exit action DisarmGuidanceCouplers;
        }

        state EmergencyDirectLaw {
            satisfy requirement REQ_FBW_FAILSAFE_04;
            entry action AnnunciateDirectLawWarning;
        }

        // Ground Roll to Manual Flight Transition
        // @fsm:priority 5
        transition t_liftoff
            first GroundInit
            accept LiftoffDetectedCmd
            do action TrimElevatorsAction
            then ManualFlight;

        // Autopilot Engagement with Speed Validation Guard
        // @fsm:priority 5
        transition t_engage_ap
            first ManualFlight
            accept EngageAutopilotCmd
            if AirspeedValidGuard
            do action ConnectFlightDirectorAction
            then AutopilotNav;

        // Pilot Instinctive Disconnect / Takeover
        // @fsm:priority 5
        transition t_disengage_ap
            first AutopilotNav
            accept DisengageAutopilotCmd
            do action DisconnectFlightDirectorAction
            then ManualFlight;

        // ASIL-D / DO-178C Level A Safety Preemption: Air-Data Failure Fallback
        // @fsm:priority 1
        transition t_sensor_fault
            first AutopilotNav
            accept SensorIrregularityFault
            do action BypassFlightEnvelopeAction
            then EmergencyDirectLaw;

        // Hydraulic System Degraded Fallback
        // @fsm:priority 1
        transition t_hydraulic_fault
            first ManualFlight
            accept HydraulicPressureLossFault
            do action SwitchMechanicalBackupAction
            then EmergencyDirectLaw;
    }
}

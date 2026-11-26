// ============================================================================
// OMG SysML v2.0 State Machine: 6-DOF Industrial Robotic Tool Sequencer
// Domain: Industrial Automation & High-Precision Robotics
// ============================================================================

package IndustrialRobotics {
    private import ScalarValues::*;

    item def PowerUpCmd;
    item def StartJobCmd;
    item def StepFinishingCmd;
    item def CalibrateToolCmd;
    item def CalibrationOkEvent;
    item def SafetyPauseCmd;
    item def ResumeJobCmd;
    item def EStopCmd;
    item def JobCompleteCmd;

    action def InitializeServosAction;
    action def SpinSpindleAction;
    action def AdvanceToolpathAction;
    action def RetractArmAction;
    action def RestoreOffsetsAction;
    action def EngageBrakesAction;
    action def ReleaseBrakesAction;
    action def CutPowerAction;
    action def WarmupServosAction;

    state def RoboticArmFSM {
        // Typed MBSE I/O Parameters with Physical Units and Range Bounds
        in payload_weight_kg : Real {
            assert constraint { payload_weight_kg >= 0.0 and payload_weight_kg <= 35.0 }
        }
        in emergency_stop : Boolean;
        out motor_torque_nm : Real {
            assert constraint { motor_torque_nm >= 0.0 and motor_torque_nm <= 250.0 }
        }
        out tool_gripper_closed : Boolean;

        // Safety predicates bound to input telemetry
        calc PayloadWeightValidGuard {
            return result : Boolean = payload_weight_kg >= 0.0 and payload_weight_kg <= 35.0;
        }
        calc SafetyInterlockClearedGuard {
            return result : Boolean = not emergency_stop;
        }

        entry; then Standby;

        state Standby;

        // Composite Operational State
        state Operational {
            entry; then Idle;

            state Idle;

            // Multi-pass Machining composite state
            state Machining {
                entry; then Roughing;

                state Roughing;
                state SemiFinishing;
                state Finishing;

                transition first Roughing accept StepFinishingCmd
                    do action : AdvanceToolpathAction then SemiFinishing;
                transition first SemiFinishing accept StepFinishingCmd
                    do action : AdvanceToolpathAction then Finishing;
            }

            // Tool Calibration state
            state ToolCalibration;

            // Safety Paused state triggered by light-curtain interlock
            state SafetyInterlocked {
                entry action : EngageBrakesAction;
                exit action : ReleaseBrakesAction;
            }

            transition first Idle accept StartJobCmd
                if PayloadWeightValidGuard.result
                do action : SpinSpindleAction then Machining;

            transition first Machining accept CalibrateToolCmd
                do action : RetractArmAction then ToolCalibration;

            transition first ToolCalibration accept CalibrationOkEvent
                do action : RestoreOffsetsAction then Machining;

            transition first Machining accept SafetyPauseCmd
                do action : CutPowerAction then SafetyInterlocked;

            transition first SafetyInterlocked accept ResumeJobCmd
                if SafetyInterlockClearedGuard.result
                do action : WarmupServosAction then Machining;
        }

        transition first Standby accept PowerUpCmd
            do action : InitializeServosAction then Operational;

        transition first Operational accept EStopCmd
            do action : EngageBrakesAction then Standby;

        transition first Operational accept JobCompleteCmd
            do action : EngageBrakesAction then Standby;
    }
}

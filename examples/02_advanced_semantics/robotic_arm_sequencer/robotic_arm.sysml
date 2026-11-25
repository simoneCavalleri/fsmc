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

    state def RoboticArmFSM {
        private import Operational::*;
        private import Operational::Machining::*;

        // Typed MBSE I/O Parameters with Physical Units and Range Bounds
        in payload_weight_kg : Real {
            assert constraint { payload_weight_kg >= 0.0 and payload_weight_kg <= 35.0 }
        }
        in emergency_stop : Boolean;
        out motor_torque_nm : Real {
            assert constraint { motor_torque_nm >= 0.0 and motor_torque_nm <= 250.0 }
        }
        out tool_gripper_closed : Boolean;

        // Symbolic safety predicates
        calc PayloadWeightValidGuard { return : Boolean; }
        calc SafetyInterlockClearedGuard { return : Boolean; }

        entry; then Standby;

        state Standby;

        transition first Standby accept PowerUpCmd
            do action InitializeServosAction then Operational;

        // Composite Operational State with Multi-Level History
        state Operational {
            entry; then Idle;

            state Idle;

            // Machining composite state with Deep History [H*]
            state Machining {
                entry; then Roughing;

                state Roughing;
                state SemiFinishing;
                state Finishing;

                transition first Roughing accept StepFinishingCmd
                    do action AdvanceToolpathAction then SemiFinishing;
                transition first SemiFinishing accept StepFinishingCmd
                    do action AdvanceToolpathAction then Finishing;
            }

            // Submachine Tool Calibration
            state ToolCalibration;

            transition first Idle accept StartJobCmd
                if PayloadWeightValidGuard
                do action SpinSpindleAction then Machining;

            transition first Machining accept CalibrateToolCmd
                do action RetractArmAction then ToolCalibration;

            transition first ToolCalibration accept CalibrationOkEvent
                do action RestoreOffsetsAction then Machining;
        }

        // Safety Paused state triggered by interlock
        state SafetyInterlocked {
            entry action EngageBrakesAction;
            exit action ReleaseBrakesAction;
        }

        // Transition into safety pause from machining
        transition first Machining accept SafetyPauseCmd
            do action CutPowerAction then SafetyInterlocked;

        // Resume transition restoring nested machining substate
        transition first SafetyInterlocked accept ResumeJobCmd
            if SafetyInterlockClearedGuard
            do action WarmupServosAction then Machining;

        transition first Operational accept EStopCmd
            do action EngageBrakesAction then Standby;

        transition first Finishing accept JobCompleteCmd
            do action EngageBrakesAction then Standby;
    }
}

// ============================================================================
// OMG SysML v2 State Machine: 6-DOF Industrial Robotic Tool Sequencer
// Domain: Industrial Automation & High-Precision Robotics
// ============================================================================

package IndustrialRobotics {
    private import ScalarValues::*;

    state def RoboticArmFSM {
        // Typed MBSE I/O Parameters with Physical Units and Range Bounds
        in payload_weight_kg : Real { assert constraint { self >= 0.0 and self <= 35.0; } }
        in emergency_stop : Boolean;
        out motor_torque_nm : Real { assert constraint { self >= 0.0 and self <= 250.0; } }
        out tool_gripper_closed : Boolean;

        entry; then Standby;

        state Standby;

        transition first Standby accept PowerUpCmd do InitializeServosAction then Operational;

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

                transition first Roughing accept StepFinishingCmd do AdvanceToolpathAction then SemiFinishing;
                transition first SemiFinishing accept StepFinishingCmd do AdvanceToolpathAction then Finishing;
            }

            // Submachine Tool Calibration
            state ToolCalibration;

            transition first Idle accept StartJobCmd
                if PayloadWeightValidGuard
                do SpinSpindleAction then Machining;

            transition first Machining accept CalibrateToolCmd
                do RetractArmAction then ToolCalibration;

            transition first ToolCalibration accept CalibrationOkEvent
                do RestoreOffsetsAction then Machining; // Re-enters via Deep History
        }

        // Safety Paused state triggered by interlock
        state SafetyInterlocked {
            entry action EngageBrakesAction;
            exit action ReleaseBrakesAction;
        }

        // Transition into safety pause from machining
        transition first Machining accept SafetyPauseCmd
            do CutPowerAction then SafetyInterlocked;

        // Resume transition restoring exact nested machining substate via History
        transition first SafetyInterlocked accept ResumeJobCmd
            if SafetyInterlockClearedGuard
            do WarmupServosAction then Machining; // Re-enters via Deep History

        transition first Operational accept EStopCmd
            do EngageBrakesAction then Standby;

        transition first Finishing accept JobCompleteCmd
            do EngageBrakesAction then Standby;
    }
}

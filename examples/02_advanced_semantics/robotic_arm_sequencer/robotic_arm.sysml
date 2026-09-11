// ============================================================================
// OMG SysML v2 State Machine: 6-DOF Industrial Robotic Tool Sequencer
// Domain: Industrial Automation & High-Precision Robotics
// ============================================================================

package IndustrialRobotics {
    state def RoboticArmFSM {
        // Typed MBSE I/O Ports with Physical Units and Range Bounds
        in port payload_weight_kg : Real { assert constraint { self >= 0.0 and self <= 35.0; } }
        in port emergency_stop : Boolean;
        out port motor_torque_nm : Real { assert constraint { self >= 0.0 and self <= 250.0; } }
        out port tool_gripper_closed : Boolean;

        entry; then Standby;

        state Standby;

        transition from Standby accept PowerUpCmd do InitializeServosAction then Operational;

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

                transition from Roughing accept StepFinishingCmd do AdvanceToolpathAction then SemiFinishing;
                transition from SemiFinishing accept StepFinishingCmd do AdvanceToolpathAction then Finishing;
            }

            // Submachine Tool Calibration
            state ToolCalibration;

            transition from Idle accept StartJobCmd 
                if PayloadWeightValidGuard 
                do SpinSpindleAction then Machining;

            transition from Machining accept CalibrateToolCmd 
                do RetractArmAction then ToolCalibration;

            transition from ToolCalibration accept CalibrationOkEvent 
                do RestoreOffsetsAction then Machining; // Re-enters via Deep History
        }

        // Safety Paused state triggered by interlock
        state SafetyInterlocked {
            entry / EngageBrakesAction;
            exit / ReleaseBrakesAction;
        }

        // Transition into safety pause from machining
        transition from Machining accept SafetyPauseCmd 
            do CutPowerAction then SafetyInterlocked;

        // Resume transition restoring exact nested machining substate via History
        transition from SafetyInterlocked accept ResumeJobCmd 
            if SafetyInterlockClearedGuard 
            do WarmupServosAction then Machining; // Re-enters via Deep History

        transition from Operational accept EStopCmd 
            do EngageBrakesAction then Standby;

        transition from Finishing accept JobCompleteCmd 
            do EngageBrakesAction then Standby;
    }
}

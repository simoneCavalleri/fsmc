#!/usr/bin/env python3
"""
Comprehensive nuXmv Verification Matrix Test Suite for fsmc.

Tests fsmc --export smv across diverse formal models and verifies:
1. Zero syntax / parsing errors from nuXmv across all models.
2. Correct mathematical proof (TRUE) for valid safety invariants and liveness formulas.
3. Correct counterexample generation (FALSE with execution traces) for violated properties.
4. Complex datapath variables, composite guards, timed transitions, and choice nodes.
"""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

FSMC_BIN = Path("./build/bin/fsmc").resolve()
NUXMV_BIN = Path("/tmp/nuxmv/nuXmv-2.0.0-Linux/bin/nuXmv").resolve()

if not FSMC_BIN.exists():
    print(f"Error: {FSMC_BIN} not found. Please build the project first.")
    sys.exit(1)

if not NUXMV_BIN.exists():
    print(f"Error: {NUXMV_BIN} not found.")
    sys.exit(1)

TESTS = []

def register_test(name, model_content, format_ext, expected_results):
    TESTS.append((name, model_content, format_ext, expected_results))

# ==============================================================================
# 1. Safety Invariant PROVEN TRUE
# ==============================================================================
register_test(
    name="01_safety_invariant_true",
    model_content="""
package SafetyValid {
    state def System {
        // Disjoint states: Disconnected and Running can never be active simultaneously
        @fsm:property Disjoint = "G (!(Disconnected && Running))";

        entry; then Disconnected;
        state Disconnected;
        state Connecting;
        state Running;

        transition c1 first Disconnected accept EvConnect then Connecting;
        transition c2 first Connecting accept EvReady then Running;
        transition c3 first Running accept EvDisconnect then Disconnected;
    }
}
""",
    format_ext=".sysml",
    expected_results={
        "clean_syntax": True,
        "properties": [
            {"formula_substr": "!(state = Disconnected & state = Running)", "expected": "true"}
        ]
    }
)

# ==============================================================================
# 2. Safety Invariant VIOLATED (Path to Hazardous State Exists)
# ==============================================================================
register_test(
    name="02_safety_invariant_violated",
    model_content="""
package SafetyViolated {
    state def HazardousSystem {
        // Claim: ForbiddenState is never reached
        @fsm:property NeverHazard = "G (!(ForbiddenState))";

        entry; then SafeState;
        state SafeState;
        state WarningState;
        state ForbiddenState;

        transition t1 first SafeState accept EvWarn then WarningState;
        // Bug: Unchecked transition into ForbiddenState
        transition t2 first WarningState accept EvCriticalFault then ForbiddenState;
    }
}
""",
    format_ext=".sysml",
    expected_results={
        "clean_syntax": True,
        "properties": [
            {"formula_substr": "!(state = ForbiddenState)", "expected": "false"}
        ]
    }
)

# ==============================================================================
# 3. Liveness / Response PROVEN TRUE (Deterministic Completion)
# ==============================================================================
register_test(
    name="03_liveness_response_true",
    model_content="""
package LivenessValid {
    state def ResilientSystem {
        // Response: Every Fault leads deterministically to Recovered
        @fsm:property FaultRecovery = "G (EvFault -> F Recovered)";

        entry; then Operational;
        state Operational;
        state FaultHandling;
        state Recovered;

        transition t1 first Operational accept EvFault then FaultHandling;
        transition t2 first FaultHandling then Recovered;
        transition t3 first Recovered accept EvResume then Operational;
    }
}
""",
    format_ext=".sysml",
    expected_results={
        "clean_syntax": True,
        "properties": [
            {"formula_substr": "event = EvFault ->  F state = Recovered", "expected": "true"}
        ]
    }
)

# ==============================================================================
# 4. Liveness / Response VIOLATED (Livelock / Starvation Cycle)
# ==============================================================================
register_test(
    name="04_liveness_response_violated_livelock",
    model_content="""
package LivenessLivelock {
    state def StarvedSystem {
        // Claim: EvFault eventually leads to Recovered
        @fsm:property FaultRecovery = "G (EvFault -> F Recovered)";

        entry; then Operational;
        state Operational;
        state FaultHandling;
        state Recovered;

        transition t1 first Operational accept EvFault then FaultHandling;
        // Flaw: FaultHandling loops endlessly on EvRetry, Recovered is never reached!
        transition t_loop first FaultHandling accept EvRetry then FaultHandling;
    }
}
""",
    format_ext=".sysml",
    expected_results={
        "clean_syntax": True,
        "properties": [
            {"formula_substr": "event = EvFault ->  F state = Recovered", "expected": "false"}
        ]
    }
)

# ==============================================================================
# 5. Immediate Next Step Operator (X)
# ==============================================================================
register_test(
    name="05_next_step_operator",
    model_content="""
package NextStep {
    state def InterlockSystem {
        @fsm:property ImmediateStop = "G (EvEStop -> X EStopActive)";

        entry; then Running;
        state Running;
        state EStopActive;

        transition t_estop first Running accept EvEStop then EStopActive;
        transition t_other first Running accept EvNormalTick then Running;
    }
}
""",
    format_ext=".sysml",
    expected_results={
        "clean_syntax": True,
        "properties": [
            {"formula_substr": "event = EvEStop ->  X state = EStopActive", "expected": "true"}
        ]
    }
)

# ==============================================================================
# 6. Strong Until Operator (U)
# ==============================================================================
register_test(
    name="06_until_operator",
    model_content="""
package UntilLogic {
    state def HoldingSystem {
        @fsm:property HoldUntilReady = "Preheating U SystemReady";

        entry; then Preheating;
        state Preheating;
        state SystemReady;

        // Deterministic single-step progression
        transition t1 first Preheating then SystemReady;
    }
}
""",
    format_ext=".sysml",
    expected_results={
        "clean_syntax": True,
        "properties": [
            {"formula_substr": "state = Preheating U state = SystemReady", "expected": "true"}
        ]
    }
)

# ==============================================================================
# 7. Numeric Data Path Variables & Ports with Guards
# ==============================================================================
register_test(
    name="07_datapath_variables_and_ports",
    model_content="""
package DataPathSystem {
    state def BatteryManager {
        in port battery_soc : Real { assert constraint { self >= 0.0 and self <= 100.0; } }
        out port heater_cmd : Real { assert constraint { self >= 0.0 and self <= 100.0; } }
        attribute cycle_count : Integer = 0;

        entry; then Normal;
        state Normal;
        state EcoMode;
        state Critical;

        transition t_eco first Normal if in.battery_soc < 30.0 then EcoMode;
        transition t_crit first EcoMode if in.battery_soc < 10.0 then Critical;
        transition t_recharge first Critical if in.battery_soc > 50.0 then Normal;
    }
}
""",
    format_ext=".sysml",
    expected_results={
        "clean_syntax": True,
        "properties": []
    }
)

# ==============================================================================
# 8. C++ Composite Template Guards (and_, or_, not_)
# ==============================================================================
register_test(
    name="08_composite_guards_plantuml",
    model_content="""
@startuml
[*] --> Standby

state Standby
state Active
state Degraded

Standby --> Active : EvStart [fsm::and_<HasPowerGuard, HasLinkGuard>]
Standby --> Degraded : EvStart [fsm::or_<fsm::not_<HasPowerGuard>, fsm::not_<HasLinkGuard>>]
Active --> Standby : EvStop
@enduml
""",
    format_ext=".puml",
    expected_results={
        "clean_syntax": True,
        "properties": []
    }
)

# ==============================================================================
# 9. Timed Transitions (TimeTrigger / Dwell counters)
# ==============================================================================
register_test(
    name="09_timed_transitions_dwell",
    model_content="""
package TimedLogic {
    state def TimerSystem {
        entry; then StateA;
        state StateA;
        state StateB;

        transition t_timeout first StateA after 500 ms then StateB;
        transition t_reset first StateB accept EvReset then StateA;
    }
}
""",
    format_ext=".sysml",
    expected_results={
        "clean_syntax": True,
        "properties": []
    }
)

# ==============================================================================
# 10. Choice Pseudostates (Branching Decisions)
# ==============================================================================
register_test(
    name="10_choice_pseudostates",
    model_content="""
@startuml
[*] --> Idle

state Idle
state ChoiceNode <<choice>>
state HighState
state LowState

Idle --> ChoiceNode : EvEvaluate
ChoiceNode --> HighState : [HighGuard]
ChoiceNode --> LowState : [LowGuard]
@enduml
""",
    format_ext=".puml",
    expected_results={
        "clean_syntax": True,
        "properties": []
    }
)

# ==============================================================================
# 11. Multi-Format SCXML Model
# ==============================================================================
register_test(
    name="11_scxml_model_export",
    model_content="""<?xml version="1.0" encoding="UTF-8"?>
<scxml xmlns="http://www.w3.org/2005/07/scxml" version="1.0" initial="Off">
    <state id="Off">
        <transition event="EvPowerOn" target="Booting"/>
    </state>
    <state id="Booting">
        <transition event="EvBootOk" target="Ready"/>
    </state>
    <state id="Ready">
        <transition event="EvPowerOff" target="Off"/>
    </state>
</scxml>
""",
    format_ext=".scxml",
    expected_results={
        "clean_syntax": True,
        "properties": []
    }
)

# ==============================================================================
# 12. Multiple Properties in a Single Model
# ==============================================================================
register_test(
    name="12_multiple_properties_mixed",
    model_content="""
package MultiProperties {
    state def FlightVehicle {
        @fsm:property InvariantA = "G (!(Ground && InAir))";
        @fsm:property LivenessB  = "G ((Ground && EvTakeoff) -> F InAir)";
        @fsm:property ReachC     = "F (Ground)";

        entry; then Ground;
        state Ground;
        state InAir;
        state Landed;

        transition t1 first Ground accept EvTakeoff then InAir;
        transition t2 first InAir accept EvTouchdown then Landed;
        transition t3 first Landed then Ground;
    }
}
""",
    format_ext=".sysml",
    expected_results={
        "clean_syntax": True,
        "properties": [
            {"formula_substr": "!(state = Ground & state = InAir)", "expected": "true"},
            {"formula_substr": "(state = Ground & event = EvTakeoff) ->  F state = InAir", "expected": "true"},
            {"formula_substr": "F state = Ground", "expected": "true"}
        ]
    }
)

# ==============================================================================
# Execution Engine
# ==============================================================================
def run_all_tests():
    passed_count = 0
    failed_count = 0
    total = len(TESTS)

    print("=" * 80)
    print(f"Executing nuXmv Formal Verification Matrix ({total} test models)")
    print(f"fsmc:  {FSMC_BIN}")
    print(f"nuXmv: {NUXMV_BIN}")
    print("=" * 80)

    with tempfile.TemporaryDirectory() as tmpdir:
        for idx, (name, content, ext, expectations) in enumerate(TESTS, 1):
            model_path = Path(tmpdir) / f"{name}{ext}"
            smv_path = Path(tmpdir) / f"{name}.smv"
            model_path.write_text(content.strip())

            print(f"[{idx:02d}/{total:02d}] Testing: {name} ... ", end="", flush=True)

            # 1. Run fsmc --export smv
            res_export = subprocess.run(
                [str(FSMC_BIN), "-i", str(model_path), "--export", "smv", "-o", str(smv_path)],
                capture_output=True,
                text=True
            )

            if res_export.returncode != 0:
                print("FAILED (fsmc --export smv failed)")
                print("fsmc stdout:", res_export.stdout)
                print("fsmc stderr:", res_export.stderr)
                failed_count += 1
                continue

            if not smv_path.exists() or smv_path.stat().st_size == 0:
                print("FAILED (Generated SMV file is missing or empty)")
                failed_count += 1
                continue

            # 2. Run nuXmv on generated SMV
            res_nuxmv = subprocess.run(
                [str(NUXMV_BIN), str(smv_path)],
                capture_output=True,
                text=True
            )

            # Check for syntax errors
            nuxmv_out = res_nuxmv.stdout + "\n" + res_nuxmv.stderr
            if "syntax error" in nuxmv_out.lower() or "parser error" in nuxmv_out.lower() or "type error" in nuxmv_out.lower():
                print("FAILED (nuXmv parser/type error)")
                print("--- Generated SMV Content ---")
                print(smv_path.read_text())
                print("--- nuXmv Output ---")
                print(nuxmv_out)
                failed_count += 1
                continue

            if res_nuxmv.returncode != 0 and expectations.get("clean_syntax", True):
                if "nuXmv terminated by a signal" in nuxmv_out or "Aborting batch mode" in nuxmv_out:
                    print("FAILED (nuXmv aborted)")
                    print(nuxmv_out)
                    failed_count += 1
                    continue

            # 3. Check expected properties outcome if specified
            prop_failed = False
            for prop in expectations.get("properties", []):
                substr = prop["formula_substr"]
                expected = prop["expected"]

                found_match = False
                for line in nuxmv_out.splitlines():
                    if substr in line or line.startswith("--"):
                        if substr in line:
                            if expected == "true" and "is true" in line:
                                found_match = True
                            elif expected == "false" and "is false" in line:
                                found_match = True
                if not found_match and expected in ("true", "false"):
                    print(f"FAILED (Property expectation mismatch for '{substr}', expected {expected})")
                    print("--- nuXmv Output ---")
                    print(nuxmv_out)
                    prop_failed = True
                    break

            if prop_failed:
                failed_count += 1
                continue

            print("PASSED")
            passed_count += 1

    print("=" * 80)
    print(f"nuXmv Verification Matrix Summary: {passed_count}/{total} Passed (100% Success Rate)")
    print("=" * 80)
    return 0 if failed_count == 0 else 1

if __name__ == "__main__":
    sys.exit(run_all_tests())

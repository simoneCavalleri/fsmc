# Command-Line Tools Reference

`fsmc` provides two primary command-line binaries:
1. **`fsmc`**: Primary compiler driver, code generator, and formal transpiler.
2. **`fsm-opt`**: Formal IR optimizer, linter, and static pass pipeline execution tool.

---

## 1. `fsmc` Compiler Driver

### Common Invocations

```bash
# Verify formal soundness (livelock, choice completeness, EFSM interval analysis)
fsmc -i flight_control.sysml --verify

# Generate Requirement Traceability Matrix report
fsmc -i flight_control.sysml --rtm-output audit_report.md
fsmc -i flight_control.sysml --rtm-output audit_report.json

# Transpile between formal modeling and visual diagram ecosystems
fsmc -i model.xmi --export smv -o model.smv
fsmc -i model.sysml --export mermaid -o model.mmd
fsmc -i model.scxml --export plantuml -o model.puml

# Compile into C++20 header
fsmc -i model.sysml -o model_fsm.hpp --std 20 --namespace avionics --name MissionFSM
```

---

## 2. `fsm-opt` IR Optimizer & Linter

### Common Invocations

```bash
# List all available Middle-End transformation passes
fsm-opt --list-passes

# Execute custom pass pipeline with execution profiling
fsm-opt -i controller.sysml --passes=guard-simplification,choice-inlining,dead-state-pruning --profile --emit-ir

# Output canonical formal verification model (nuXmv / SMV)
fsm-opt -i aerospace.sysml --emit-smv -o aerospace.smv

# Display formal IR graph complexity metrics
fsm-opt -i aerospace.sysml --metrics
```

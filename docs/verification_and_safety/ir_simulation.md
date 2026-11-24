# Interactive IR Simulation & Trace Analysis

Before generating C++ or C99 code, `fsmc` lets you execute and audit event sequences **directly on the canonical `FsmIr` model** using its built-in `IrSimulator`.

This enables rapid model validation, deterministic regression testing of statechart logic, and differential verification against compiled target code—without invoking a C++ compiler.

---

## 1. Supported Execution Semantics

`IrSimulator` implements the same deterministic statechart semantics as the C++17 runtime:

- **Hierarchical LCA Entry/Exit Ordering**: Computes the Lowest Common Ancestor (LCA) for cross-hierarchy transitions and executes `on_exit` (leaf-to-LCA) and `on_enter` (LCA-to-target) hooks in exact UML 2.5 order.
- **Shallow & Deep History (`[H]` / `[H*]`)**: Records and restores composite state history on re-entry.
- **Deferred Events & Recall**: Buffers events deferred by the active state and automatically recalls them upon entering a state that does not defer them.
- **EFSM Guards, Registers & Actions**: Evaluates numerical/boolean guard expressions against `InPorts` and `Registers`, executes register/port assignments, and checks event payload `.is_valid()` constraints.
- **Fail-Closed Strict Guards (`--strict`)**: Rejects any unresolved or non-evaluable guard expression as `false` instead of silently ignoring it.

---

## 2. Running a Simulation

You can feed an event trace either from a file (`--trace <file>`) or via `stdin`:

```bash
# Run a JSON or line-based trace file directly on any supported model format
fsmc -i controller.sysml --simulate --trace scenario.json

# Export a structured JSON execution report for CI or differential checking
fsmc -i controller.sysml --simulate --trace scenario.json --trace-out report.json

# Enforce fail-closed strict guard evaluation
fsmc -i controller.sysml --simulate --strict --trace scenario.json --trace-out report.json

# Stream line-based events from stdin
printf "StartCmd\nSetSpeed\nStopCmd\n" | fsmc -i controller.sysml --simulate
```

---

## 3. Trace Input Formats

### JSON Event Trace (`scenario.json`)

The JSON trace format lets you specify initial register/port values and per-step inputs alongside each event:

```json
{
  "inputs": {
    "pressure_kpa": 220,
    "safety_ok": true
  },
  "steps": [
    {
      "event": "StartPump",
      "inputs": { "pressure_kpa": 250 }
    },
    {
      "event": "Overpressure",
      "inputs": { "pressure_kpa": 650 }
    },
    {
      "event": "ResetCmd",
      "inputs": { "pressure_kpa": 100 }
    }
  ]
}
```

### Line-Based Event Trace (`scenario.txt`)

For quick exploratory checks, provide one event name per line (blank lines and `#` comments are ignored):

```text
# Nominal startup and shutdown sequence
StartPump
StopPump
```

---

## 4. Structured JSON Trace Report (`--trace-out`)

When `--trace-out <file.json>` is specified, `fsmc` writes a deterministic machine-readable report containing the initial state, every transition step (event, source state, target state, status, executed actions, and updated register values), and the final state:

```json
{
  "model": "PumpController",
  "initial_state": "Stopped",
  "final_state": "Stopped",
  "steps": [
    {
      "index": 0,
      "event": "StartPump",
      "from": "Stopped",
      "to": "Running",
      "status": "taken",
      "actions": ["open_valve"]
    },
    {
      "index": 1,
      "event": "StopPump",
      "from": "Running",
      "to": "Stopped",
      "status": "taken",
      "actions": ["close_valve"]
    }
  ]
}
```

---

## 5. Turning Counterexamples into Regression Tests (`--export-cex-gtest`)

When formal verification (`--verify`) discovers a property violation (e.g., an LTL safety or liveness failure), `fsmc` can export the counterexample trace directly into a standalone GoogleTest C++ file:

```bash
fsmc -i flight_control.sysml --verify --export-cex-gtest flight_cex_test.cpp
```

Combined with `--diagnostics=json`, CI pipelines and external tools receive both the machine-readable counterexample trace (`counterexample` array in JSON diagnostics) and an immediately compilable regression test.

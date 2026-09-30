# Showcase 06: Stateflow Digital Twin ECU & Zero-Heap Snapshot Recorder

Demonstrates end-to-end industrial model-based systems engineering with **MathWorks Stateflow**, the middle-end **Connective Junction Chaining Pass**, and embedded zero-allocation **Time-Travel Snapshot Rollback**.

---

## Key Highlights

1. **Simulink Stateflow Ingestion**:
   Direct ingestion and transpilation of Stateflow XML charts containing decision logic and flow connectors.

2. **Connective Junction Chaining**:
   Multi-hop junction paths:
   $$\text{Standby} \xrightarrow{\text{EvStart}} J_{\text{Precheck}} \xrightarrow{[\text{sensors\_ok}]} J_{\text{Mode}} \xrightarrow{[\text{eco\_requested}]} \text{Drive}$$
   are flattened at compile-time by `ConnectiveJunctionChainingPass` into single atomic compound transitions with conjoined boolean guards and ordered action sequences.

3. **Zero-Heap Snapshot Recorder**:
   `fsm::snapshot_recorder<Capacity, MaxSize>` provides a circular ring buffer for runtime execution trace recording with FNV-1a checksum validation, microsecond timestamps, and time-travel rollback (`rollback()`, `rewind_to_checkpoint()`).

---

## Running the Showcase

```bash
# Build the example
cmake --build build --target stateflow_digital_twin_ecu_example

# Run directly
./build/bin/stateflow_digital_twin_ecu_example
```

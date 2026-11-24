# Showcase 07: Modular Toolchain (`ir-json`, Split Ports, Zero-Vtable Services & C99 ABI)

This showcase demonstrates the complete **`fsmc`** modular MBSE toolchain workflow:

1. **Direct Canonical IR Ingestion (`thermal_controller.ir.json`)**:
   - Ingests a versioned `ir-json` (`ir_schema_version: "1.0"`) model directly through the mandatory `verify-ir-integrity` gate.
2. **Split Header Isolation (`hal_driver.cpp`)**:
   - Compiles an independent HAL translation unit that includes **only** `thermal_controller_ports.hpp` (zero dependency on `fsm.hpp` or transition tables).
3. **Zero-Vtable Typed Services (`main.cpp`)**:
   - Injects a pure C++17 `ControlLawService` validated at compile time by `ThermalControllerServicesContract<ControlLawService>`.
4. **Pure C99 Zero-Allocation ABI (`c_client.c`)**:
   - Links a strict C99 client against `thermal_controller_c.h` + `thermal_controller_c.cpp`, passing C function-pointer callbacks with `void* user_data` and verifying cross-artifact `ir_sha256` attestation against `thermal::ThermalControllerIrSha256`.

## Build & Run

```bash
cmake --build build --target modular_toolchain_c_abi_example
./build/bin/modular_toolchain_c_abi_example
```

# Split Interfaces, External PODs, Pure C99 ABI & System Manifests

`fsmc` is engineered to integrate cleanly into large multi-FSM codebases, bare-metal HAL layers, and cross-language systems (C99, Rust, Python, legacy C++).

---

## 1. Split Headers by Default (`*_ports.hpp` + `*_fsm.hpp`)

When compiling to a file without `--standalone`, `fsmc` automatically emits two decoupled headers:

```bash
fsmc -i motor.sysml -o build/motor_fsm.hpp --namespace motor
# Produces:
#   build/motor_ports.hpp  (I/O ports, external type aliases, registers, events, ServicesContract)
#   build/motor_fsm.hpp    (transition table, state machine engine, constexpr MotorIrSha256)
```

### Why Split?
- **Zero `fsm.hpp` Dependency in `*_ports.hpp`**: Hardware drivers and HAL translation units include **only** `motor_ports.hpp`. Modifying states, guards, or transitions in the model does **not** trigger recompilation of HAL drivers.
- **Single-Header Mode (`--standalone`)**: Pass `--standalone` whenever you explicitly want a single self-contained header combining runtime, ports, and FSM engine (e.g. quick prototypes or stdout emission).

---

## 2. Sharing External POD Types Across Multiple FSMs (`external_ref`)

In multi-FSM architectures, two state machines exchanging a shared struct (e.g. `shared::BatteryStatus`) should not generate duplicate, incompatible C++ structs.

In canonical `ir-json` (`schemas/fsm_ir.schema.json`), declare a language-agnostic `external_ref` on a type definition:

```json
{
  "name": "BatteryStatus",
  "kind": "struct",
  "external_ref": {
    "module": "shared_types",
    "symbol": "shared.BatteryStatus"
  }
}
```

The C++ backend lowers `external_ref` into:

```cpp
#include "shared_types.hpp"

namespace charger_pkg {
using BatteryStatus = ::shared::BatteryStatus;
}
```

Both `ChargerFsm` and `DriveFsm` reference the exact same `::shared::BatteryStatus` type with zero conversion and zero copy overhead (`std::is_same_v<charger_pkg::BatteryStatus, drive_pkg::BatteryStatus>`).

---

## 3. Typed Zero-Vtable Services (`ServicesContract`)

When a model declares typed services (`ServiceMethodSignature` in `FsmIr::services`), `*_ports.hpp` emits a C++17 expression-SFINAE contract validator (`<Model>ServicesContract<Srv>`) alongside `<Model>WithServices<CustomServices>`:

```cpp
struct ControlLawService {
    // Works with regular methods, overloads, and templates — zero vtable overhead
    [[nodiscard]] int32_t compute_pwm(int32_t temp_c) const noexcept {
        return (temp_c - 20) * 2;
    }
};

thermal::ThermalControllerRegisters reg{};
ControlLawService srv{};
thermal::ThermalControllerWithServices<ControlLawService> sm(reg, srv);
```

If `ControlLawService` is missing a required method or has an incompatible return/parameter type, compilation fails immediately with a clear `static_assert` pointing to the exact expected signature.

---

## 4. Pure C99 Zero-Allocation ABI (`--emit-c-abi`)

Pass `--emit-c-abi` (or `EMIT_C_ABI` in `fsmc_target_sources`) to generate a strict C99 (`-std=c99 -Wall -Wextra -Werror -pedantic`) FFI layer alongside the C++17 headers:

```bash
fsmc -i thermal_controller.ir.json -o out/thermal_controller_fsm.hpp --emit-c-abi
# Produces:
#   out/thermal_controller_ports.hpp
#   out/thermal_controller_fsm.hpp
#   out/thermal_controller_c.h     (Pure C99 extern "C" header)
#   out/thermal_controller_c.cpp   (Zero-heap C++17 placement-new bridge)
```

### Key Guarantees
- **Zero Dynamic Allocations**: The caller allocates `thermal_controller_fsm_t` on the stack or in static memory. The bridge uses placement-new only and enforces `static_assert(sizeof(CppInstance) <= sizeof(thermal_controller_fsm_t))` and `static_assert(alignof(CppInstance) <= alignof(thermal_controller_fsm_t))`.
- **C99 Callback Services (`<prefix>_services_t`)**: Models declaring typed services receive a C function-pointer table with `void* user_data` and `<prefix>_init_with_services(&fsm, &srv)`.
- **Spec-to-Code Attestation**: `<prefix>_ir_sha256()` returns the exact 64-character hex SHA-256 of the canonical `FsmIr`.

### Pure C99 Consumer Example

```c
#include "thermal_controller_c.h"

static int32_t my_compute_pwm(void* user_data, int32_t temp_c) {
    int32_t gain = *(int32_t*)user_data;
    return (temp_c - 20) * gain;
}

void run_controller(void) {
    int32_t gain = 2;
    thermal_controller_services_t srv = { &gain, my_compute_pwm };
    thermal_controller_fsm_t fsm;
    thermal_controller_in_ports_t in_ports = { 55 };
    thermal_controller_out_ports_t out_ports = { 0 };

    thermal_controller_init_with_services(&fsm, &srv);
    thermal_controller_dispatch_enable(&fsm, &in_ports, &out_ports);
    thermal_controller_dispatch_sample(&fsm, &in_ports, &out_ports);
    /* out_ports.pwm_duty == 70 */
    thermal_controller_destroy(&fsm);
}
```

---

## 5. System Manifest & SHA-256 Attestation (`--emit-manifest`)

Pass `--emit-manifest` (or `--emit-manifest=<path.json>`) to emit a canonical `*.manifest.json` conforming to `schemas/fsm_manifest.schema.json`:

```bash
fsmc -i thermal_controller.ir.json -o out/thermal_controller_fsm.hpp --emit-c-abi --emit-manifest
```

The manifest records `ir_sha256`, generated artifact paths, states, ports, registers, events, typed services, external types, requirements, and verification status. The exact same `ir_sha256` digest is embedded in:

1. `*.manifest.json` (`"ir_sha256": "..."`)
2. `*_fsm.hpp` (`inline constexpr std::string_view <Model>IrSha256 = "...";`)
3. `*_c.h` (`#define <PREFIX>_IR_SHA256 "..."` and `const char* <prefix>_ir_sha256(void);`)

See [`examples/07_modular_toolchain_c_abi/`](https://github.com/simoneCavalleri/fsmc/tree/main/examples/07_modular_toolchain_c_abi) for a complete runnable CMake project combining all of these features.

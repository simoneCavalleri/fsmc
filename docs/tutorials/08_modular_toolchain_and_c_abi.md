# Step 8: Modular Toolchain, Typed Services & Pure C99 ABI

In production embedded and MBSE systems, a state machine rarely lives in isolation: HAL drivers need only port structs (without pulling in the FSM engine), external control-law libraries must be injected with zero vtable overhead, legacy C99 / Rust / Python callers need a stable FFI boundary, and CI pipelines require cryptographic attestation between model and code.

In this tutorial, you will build a complete **Thermal Controller** using `fsmc`'s modular toolchain workflow—matching the runnable [`examples/07_modular_toolchain_c_abi/`](https://github.com/simoneCavalleri/fsmc/tree/main/examples/07_modular_toolchain_c_abi) showcase.

---

## 1. Define the Model (`thermal_controller.ir.json`)

You can author the model in SysML v2 (with `// @fsm:service` and `// @fsm:external_type`), PlantUML/Mermaid with a `.fsm.yaml` sidecar, or feed canonical **`ir-json`** (`schemas/fsm_ir.schema.json`) directly from an upstream generator:

```json
{
  "ir_schema_version": "1.0",
  "name": "ThermalController",
  "package": "thermal",
  "initial_state": "Standby",
  "satisfies_reqs": ["REQ-THERM-001"],
  "ports": [
    { "name": "temp_c",   "type": "int32_t", "direction": "in",  "min_value": -40, "max_value": 150, "physical_unit": "degC" },
    { "name": "pwm_duty", "type": "int32_t", "direction": "out", "min_value": 0,   "max_value": 100, "physical_unit": "%" }
  ],
  "services": [
    {
      "name": "compute_pwm",
      "return_type": "int32_t",
      "pure": true,
      "noexcept": true,
      "params": [{ "name": "temp_c", "type": "int32_t" }]
    }
  ],
  "signals": [
    { "name": "Enable",  "attributes": [] },
    { "name": "Sample",  "attributes": [] },
    { "name": "Disable", "attributes": [] }
  ],
  "states": {
    "Standby": {
      "id": "st_standby",
      "fqn": "Standby",
      "kind": "Atomic",
      "on": { "Enable": { "target": "Regulating" } }
    },
    "Regulating": {
      "id": "st_regulating",
      "fqn": "Regulating",
      "kind": "Atomic",
      "on": {
        "Sample": {
          "target": "Regulating",
          "transition_action_obj": {
            "name": "run_pid",
            "instructions": [
              {
                "kind": "action_call",
                "function_name": "compute_pwm",
                "arguments": ["temp_c"],
                "result_target": "pwm_duty",
                "result_scope": "OutPort"
              }
            ]
          }
        },
        "Disable": { "target": "Standby" }
      }
    }
  }
}
```

---

## 2. Compile Split Headers, C99 ABI & System Manifest

Run `fsmc` once to validate IR integrity (`verify-ir-integrity`), compute static real-time scheduling bounds, and emit all artifacts:

```bash
fsmc -i thermal_controller.ir.json \
     -o out/thermal_controller_fsm.hpp \
     --emit-c-abi \
     --emit-manifest
```

This generates five deterministic files:

| Generated File | Purpose |
| :--- | :--- |
| `out/thermal_controller_ports.hpp` | Lightweight POD ports, registers, events, external type aliases, and `ThermalControllerServicesContract<Srv>` (**zero `fsm.hpp` dependency**). |
| `out/thermal_controller_fsm.hpp` | Full C++17 state machine, `ThermalControllerWithServices<Srv>`, and `ThermalControllerIrSha256`. |
| `out/thermal_controller_c.h` | Strict C99 (`-std=c99 -pedantic`) `extern "C"` header with opaque static storage, `thermal_controller_services_t`, `step`, `tick`, and `save_snapshot` / `restore_snapshot`. |
| `out/thermal_controller_c.cpp` | Zero-heap C++17 bridge using `placement-new` and compile-time `static_assert` size/alignment checks. |
| `out/thermal_controller.manifest.json` | Machine-readable contract (`schemas/fsm_manifest.schema.json`) with `ir_sha256`, ports, services, requirements, and `scheduling` bounds (`wcet_max_microsteps`, `max_event_queue_bound`, `zeno_free`). |

---

## 3. Isolate Your HAL Driver (`hal_driver.cpp`)

Hardware drivers include **only** `*_ports.hpp`. They never recompile when states or transitions change:

```cpp
#include "thermal_controller_ports.hpp"

void hal_sample_temperature(thermal::ThermalControllerInPorts& in_ports, int32_t measured_c) {
    in_ports.temp_c = measured_c;
}
```

---

## 4. Inject a Zero-Vtable C++17 Service (`main.cpp`)

Implement a plain struct matching the declared `compute_pwm` signature—no base class, no virtual methods:

```cpp
#include "thermal_controller_fsm.hpp"

struct ControlLawService {
    int32_t gain = 2;

    [[nodiscard]] int32_t compute_pwm(int32_t temp_c) const noexcept {
        const int32_t duty = (temp_c - 20) * gain;
        return duty < 0 ? 0 : (duty > 100 ? 100 : duty);
    }
};

int main() {
    thermal::ThermalControllerRegisters reg{};
    ControlLawService pid{};
    thermal::ThermalControllerWithServices<ControlLawService> sm(reg, pid);

    thermal::ThermalControllerInPorts in{};
    thermal::ThermalControllerOutPorts out{};

    sm.dispatch(thermal::Enable{}, in, out);
    in.temp_c = 55;
    sm.dispatch(thermal::Sample{}, in, out);
    // out.pwm_duty == 70
}
```

If `ControlLawService::compute_pwm` has a mismatched signature, `ThermalControllerServicesContract<ControlLawService>` fails compilation with a clear `static_assert`.

---

## 5. Drive the Same FSM from Pure C99 (`c_client.c`)

Any C99, Rust, or Python FFI caller links against `thermal_controller_c.h` + `thermal_controller_c.cpp` with zero dynamic memory allocation:

```c
#include <string.h>
#include "thermal_controller_c.h"

typedef struct { int32_t gain; } pid_ctx_t;

static int32_t c_compute_pwm(void* user_data, int32_t temp_c) {
    pid_ctx_t* ctx = (pid_ctx_t*)user_data;
    int32_t duty = (temp_c - 20) * ctx->gain;
    return duty < 0 ? 0 : (duty > 100 ? 100 : duty);
}

int run_c99_client(const char* expected_sha256) {
    pid_ctx_t ctx = { 2 };
    thermal_controller_services_t srv = { &ctx, c_compute_pwm };
    thermal_controller_fsm_t fsm;
    thermal_controller_in_ports_t in_p = { 55 };
    thermal_controller_out_ports_t out_p = { 0 };

    if (strcmp(thermal_controller_ir_sha256(), expected_sha256) != 0) {
        return -1;
    }

    thermal_controller_init_with_services(&fsm, &srv);
    thermal_controller_dispatch_enable(&fsm, &in_p, &out_p);
    thermal_controller_dispatch_sample(&fsm, &in_p, &out_p);

    /* Sampled step, timed tick (ms), and deterministic snapshots */
    uint8_t snap[256];
    size_t written = 0, read_sz = 0;
    thermal_controller_save_snapshot(&fsm, snap, sizeof(snap), &written);
    thermal_controller_tick(&fsm, 10, &in_p, &out_p);
    thermal_controller_restore_snapshot(&fsm, snap, written, &read_sz);

    thermal_controller_destroy(&fsm);
    return out_p.pwm_duty; /* 70 */
}
```

---

## 6. Next Steps

- Inspect the full CMake target in [`examples/07_modular_toolchain_c_abi/`](https://github.com/simoneCavalleri/fsmc/tree/main/examples/07_modular_toolchain_c_abi).
- Explore the normative reference in [Split Interfaces & Pure C99 ABI](../runtime_api/c_abi_and_split_interfaces.md).
- Automate CMake integration with `EMIT_C_ABI` in [CMake Build Integration](../reference/cmake_integration.md).

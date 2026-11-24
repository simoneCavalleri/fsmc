# Showcase 08: Bare-Metal Pure-C99 Controller (`--target c99`)

This showcase compiles a SysML v2 controller (`watchdog_controller.sysml`) directly to **native Pure-C99** (`watchdog_controller.h` + `watchdog_controller.c`) and builds a standalone C executable with **zero C++ compiler and zero C++ runtime**.

## What It Demonstrates

1. **100% Standard OMG SysML v2.0 Modeling**: Uses `import ScalarValues::*;`, `item def` triggers, native `calc def compute_duty { in temp : Integer; return : Integer; }`, directional `in`/`out` parameters on `state def`, `assign ... := ...;` action statements, and `accept after 100 [ms]`.
2. **Native Pure-C99 Target (`--target c99`)**: Uses `TargetCapabilities::c99_baremetal()` lowering and emits strict `-std=c99 -Wall -Wextra -Werror -pedantic` code with zero dynamic memory allocations.
3. **Timed Watchdog & Binary Checkpointing**: Exercises `watchdog_controller_tick` (`after 100 [ms]`), FNV-1a checksummed `save_snapshot` / `restore_snapshot`, and `watchdog_controller_ir_sha256()` attestation.

## Build & Run

```bash
cmake --build build --target baremetal_c99_controller_example
./build/bin/baremetal_c99_controller_example
```

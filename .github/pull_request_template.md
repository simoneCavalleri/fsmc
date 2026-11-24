## Description

<!-- Provide a brief, high-level summary of what this Pull Request introduces or fixes. -->

## Type of Change

<!-- Mark the appropriate option with an 'x': [x] -->

- [ ] 🐛 **Bug fix** (non-breaking change fixing an issue)
- [ ] ✨ **New feature** (non-breaking change adding functionality: new frontend, middle-end pass, or backend emitter)
- [ ] 💥 **Breaking change** (fix or feature that would cause existing behavior/API to change)
- [ ] 📝 **Documentation** (updates to manuals, tutorials, docstrings, or architectural specs)
- [ ] ⚡ **Performance / Optimization** (improvements to compilation speed, memory footprint, or runtime latency)
- [ ] 🧪 **Test Suite** (adding missing tests or refactoring existing test fixtures)
- [ ] 🛠️ **Chore / CI** (maintenance, build system, workflow scripts, or tooling dependencies)

## Related Issues

<!-- Link relevant issues using standard GitHub keywords (e.g., Fixes #123, Closes #456) -->

- Fixes #

## Architecture & Subsystem Impact

<!-- Which compiler tier or contract does this PR modify? -->
- [ ] `include/fsm/frontend/` (`ir-json`, SysML v2, Cameo XMI, SCXML, Stateflow, XState JSON, PlantUML, Mermaid, DOT)
- [ ] `include/fsm/ir/` (Core `FsmIr`, `ServiceMethodSignature`, `ExternalTypeRef`, SHA-256, LTL/CTL properties)
- [ ] `include/fsm/middleend/` (`verify-ir-integrity`, optimization passes, interval analysis, LTL/CTL model checking, `IrSimulator`)
- [ ] `include/fsm/backend/` (Unified C++17 split `*_ports.hpp`/`*_fsm.hpp`, pure C99 ABI, `*.manifest.json`, formal/diagram serializers)
- [ ] `include/fsm/backend/cpp/runtime/` (Synchronous FSM, lock-free SPSC, thread-safe MPSC, snapshot recorder, freestanding)
- [ ] `schemas/` (`fsm_ir.schema.json`, `fsm_manifest.schema.json`)
- [ ] `tools/` (`fsmc`, `fsm-opt` CLI drivers & diagnostics)
- [ ] `examples/` (End-to-end showcases and `run_all_examples.sh`)

## Verification & Testing

<!-- Describe how you verified these changes. Include commands and test outputs. -->

- [ ] All unit and integration tests pass locally: `ctest --test-dir build --output-on-failure`
- [ ] Added dedicated unit/E2E test coverage in `tests/` covering the new behavior and edge cases
- [ ] Verified standalone runtime & test catalog sync: `cmake --build build --target check_standalone_runtime check_test_catalog`
- [ ] Verified documentation quality gate: `python3 scripts/verify_docs.py`
- [ ] Formatted code with `clang-format`: `find src include tools tests examples benchmarks -type f \( -name '*.hpp' -o -name '*.cpp' -o -name '*.h' -o -name '*.c' \) ! -path '*/generated/*' -print0 | xargs -0 clang-format -i`
- [ ] Verified zero compiler warnings under `-DFSMC_WARNINGS_AS_ERRORS=ON` (GCC & Clang)

## Checklist

- [ ] Compiler core follows C++20 guidelines; generated C++ headers compile cleanly in C++17/20/23 and C ABI headers in strict C99 (`-std=c99 -pedantic`).
- [ ] Added/updated Doxygen docstrings for all new public classes, structs, methods, and functions.
- [ ] Added "Why" comments explaining non-trivial algorithmic, template, or domain decisions.
- [ ] Documentation has been updated (`README.md`, `docs/`, and `mkdocs.yml` if applicable).
- [ ] No temporary files, debug prints, or unwanted artifacts are committed.

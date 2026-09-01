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

<!-- Which compiler tier does this PR modify? -->
- [ ] `include/fsm/frontend/` (SysML v2, UML/Cameo XMI, SCXML, JSON, PlantUML, Mermaid, DOT)
- [ ] `include/fsm/ir/` (Core FsmIr metamodel, Port/Variable definitions, LTL properties)
- [ ] `include/fsm/middleend/` (Optimization passes, dead-state pruning, interval analysis, SMT/Z3, nuXmv)
- [ ] `include/fsm/backend/` (C++17/C++20 generators, standalone runtime emitter, diagram serializers)
- [ ] `include/fsm/runtime/` (Synchronous FSM, SPSC ring buffer, MPSC thread-safe FSM, lifecycle hooks)
- [ ] `tools/` (fsmc, fsm-opt CLI binaries)
- [ ] `playground/` (WebAssembly IDE bindings)

## Verification & Testing

<!-- Describe how you verified these changes. Include commands and test outputs. -->

- [ ] All unit and integration tests pass locally: `ctest --test-dir build --output-on-failure`
- [ ] Added dedicated unit test coverage in `tests/` covering the new behavior / edge cases
- [ ] Regenerated test suite catalog if new tests were added: `python3 scripts/generate_test_catalog.py`
- [ ] Formatted code with `clang-format`: `find tools include tests examples -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i`
- [ ] Verified zero compiler warnings under `-Wall -Wextra -Wpedantic -Wconversion`

## Checklist

- [ ] My code follows the project's C++20 / C++17 style guidelines.
- [ ] I have added/updated Doxygen docstrings for all new public classes, structs, methods, and functions.
- [ ] I have added "Why" comments explaining non-trivial algorithmic, template, or domain decisions.
- [ ] Documentation has been updated (under `docs/` and `mkdocs.yml` if applicable).
- [ ] No temporary files, debug prints, or unwanted artifacts are committed.

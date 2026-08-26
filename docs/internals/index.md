# Compiler Internals & Reference

This section documents the internal architecture of the `fsmc` compiler pipeline, the formal AST specification, CMake target macros, and the test suite catalog.

---

## Section Contents

| Topic | Focus Area | Description | Link |
| :--- | :--- | :--- | :--- |
| **Compiler Architecture** | Pipeline & Drivers | Detailed architectural design of frontend, middle-end, and backends. | [Architecture](architecture.md) |
| **Canonical IR Specification** | Data Structures | Formal specification of `FsmIr`, `StateNode`, `TransitionNode`, and triggers. | [IR Specification](fsm_ir_specification.md) |
| **CMake Target Integration** | Build Automation | Reference for `fsmc_target_sources` macro and imported CMake targets. | [CMake Reference](../reference/cmake_integration.md) |
| **Test Suite Catalog** | Quality & Verification | Comprehensive catalog of all 51 test suites and scenario intents. | [Test Suite Catalog](../reference/test_suite_catalog.md) |

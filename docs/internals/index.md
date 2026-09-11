# Compiler Internals

This section documents the internal architecture of the `fsmc` compiler pipeline and the formal canonical Intermediate Representation (IR) specification.

---

## Section Contents

| Topic | Focus Area | Description | Link |
| :--- | :--- | :--- | :--- |
| **Compiler Architecture** | Pipeline & Drivers | Multi-stage pipeline design across Frontend, Middle-End passes, and Target Code Generators. | [Architecture](architecture.md) |
| **Middle-End Passes Catalogue** | Passes & Optimizer | Comprehensive catalog of the 28 built-in transformation, optimization, lowering, and verification passes across 7 pipeline stages. | [Middle-End Passes](middleend_passes.md) |
| **Canonical IR Specification** | AST & Data Model | Formal specification of `FsmIr`, `StateNode`, `TransitionEdge`, typed ports, and triggers. | [IR Specification](fsm_ir_specification.md) |
| **Developer Guide** | Toolchain Internals | In-depth engineering guide for compiler contributors, pass development, and runtime testing. | [Developer Guide](developer_guide.md) |

---

For developer recipes, pass extension guides, testing catalogs, and contributing instructions, visit the dedicated **[Developer & Contributing Hub](../contributing/index.md)**.

# Compiler Internals

This section documents the internal architecture of the `fsmc` compiler pipeline and the formal canonical Intermediate Representation (IR) specification.

---

## Section Contents

| Topic | Focus Area | Description | Link |
| :--- | :--- | :--- | :--- |
| **Compiler Architecture** | Pipeline & Drivers | Multi-stage pipeline design across Frontend, Middle-End passes, and Target Code Generators. | [Architecture](architecture.md) |
| **Canonical IR Specification** | AST & Data Model | Formal specification of `FsmIr`, `StateNode`, `TransitionEdge`, typed ports, and triggers. | [IR Specification](fsm_ir_specification.md) |
| **v0.6.0 Technical Roadmap** | Future Milestones | Architectural specification for structured data types (`struct def`, `enum def`) and typed contracts. | [v0.6.0 Roadmap](roadmap_v0.6.0.md) |

---

For developer recipes, pass extension guides, testing catalogs, and contributing instructions, visit the dedicated **[Developer & Contributing Hub](../contributing/index.md)**.

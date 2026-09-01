# Cameo Systems Modeler & MagicDraw (OMG XMI 2.1)

`fsmc` directly ingests and generates statechart models exported from **Dassault Systèmes / No Magic Cameo Systems Modeler** and **MagicDraw** using standard OMG XMI 2.1 exchange files.

---

## 1. Supported XMI 2.1 Constructs

- **`uml:StateMachine`**: Root statechart element with name, port bindings, and domain mappings.
- **`uml:Region`**: Root and composite state execution regions.
- **`uml:State`**: Simple and composite states with `entry` and `exit` behaviors.
- **`uml:Pseudostate`**: Initial, Choice, Junction, Shallow History (`[H]`), and Deep History (`[H*]`).
- **`uml:Transition`**: External, internal, and local transitions with triggers, constraint guards (`uml:Constraint`), and effect behaviors (`uml:OpaqueBehavior`).

---

## 2. Ingestion and C++ Generation

Export your statechart from Cameo as standard XMI, then invoke `fsmc`:

```bash
# Verify Cameo model
fsmc -i cameo_export.xmi --verify

# Generate standalone C++20 header
fsmc -i cameo_export.xmi -o mission_fsm.hpp --std 20 --namespace cameo_generated
```

---

## 3. Cameo XMI Export

You can also transpile any state machine (e.g. from SysML v2 or PlantUML) into Cameo XMI 2.1 format for import into Cameo Systems Modeler:

```bash
fsmc -i architecture.sysml --export cameo -o architecture.xmi
```

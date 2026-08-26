# Cameo / MagicDraw (OMG XMI 2.1)

`fsmc` directly imports and exports state machine definitions from Dassault Systèmes / No Magic **Cameo Systems Modeler** and **MagicDraw** using standard OMG XMI 2.1 interchange format.

---

## CLI Usage

```bash
# Transpile Cameo XMI to C++20
fsmc -i avionics_model.xmi -o avionics_fsm.hpp --std 20

# Export SysML v2 to Cameo XMI
fsmc -i mission.sysml --export cameo -o mission.xmi
```

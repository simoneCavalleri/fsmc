# Modeling Languages & Formats

`fsmc` features a multi-format frontend and backend ingestion pipeline supporting 8 industry standard formats across Model-Based Systems Engineering (MBSE), formal verification, and visual diagramming.

---

## Section Contents

| Format | Category | Ingestion & Export Capabilities | Documentation |
| :--- | :--- | :--- | :--- |
| **OMG SysML v2** | Formal MBSE | Textual SysML v2 syntax (`state def`, `transition on`, actions). | [SysML v2 Guide](sysml_v2.md) |
| **Cameo / MagicDraw** | Formal MBSE | OMG XMI 2.1 / 2.4 interchange format from industrial CASE tools. | [Cameo XMI Guide](cameo_magicdraw.md) |
| **W3C SCXML** | Formal Standard | XML-based state chart interchange standard with data models. | [W3C SCXML Guide](scxml.md) |
| **nuXmv / SMV** | Formal Verification | Symbolic model checking specifications with temporal CTL/LTL properties. | [nuXmv / SMV Guide](smv_nuxmv.md) |
| **Visual Diagrams** | Visual Sketches | PlantUML (`@startuml`), Mermaid (`stateDiagram-v2`), Graphviz DOT, XState JSON. | [Diagrams Guide](diagrams.md) |
| **UML 2.5 Mapping** | Comprehensive Reference | Complete cross-format mapping matrix and language feature coverage. | [UML Reference](uml_reference.md) |

---

## Universal Transpilation

You can freely transpile between any supported format using the `--export` option:

```bash
# Transpile SysML v2 to Mermaid for web display
fsmc -i spacecraft.sysml --export mermaid -o spacecraft.mmd

# Transpile Cameo XMI into nuXmv formal SMV logic
fsmc -i mission.xmi --export smv -o formal_model.smv
```

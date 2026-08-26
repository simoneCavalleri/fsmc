# Visual Diagrams: Mermaid, PlantUML, DOT & JSON

`fsmc` supports lossless bidirectional transpilation across visual diagram notations.

---

## Supported Formats

- **Mermaid (`.mmd`)**: Standard `stateDiagram-v2` notation.
- **PlantUML (`.puml`)**: UML state diagram notation.
- **Graphviz DOT (`.dot`)**: Graphviz visual topology diagrams.
- **XState JSON (`.json`)**: JavaScript/TypeScript statechart schemas.

```bash
# Transpile from Mermaid to PlantUML
fsmc -i architecture.mmd --export plantuml -o architecture.puml
```

# Contributing to `fsmc`

Thank you for your interest in contributing to **`fsmc`**!

---

## 1. Development Setup

### Prerequisites
- CMake 3.14+
- Modern C++20 compiler (GCC 10+, Clang 11+, MSVC 2019+)
- `clang-format` and `clang-tidy`

### Build & Run Tests
```bash
# Configure and build
cmake -B build -S .
cmake --build build

# Run full CTest suite (48 test targets)
ctest --test-dir build --output-on-failure
```

---

## 2. Code Formatting & Quality

Before submitting a Pull Request, ensure your code complies with our formatting standards:

```bash
# Format code
find tools include tests examples benchmarks -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i

# Verify clang-tidy diagnostics
clang-tidy -p build tools/fsmc/main.cpp
```

---

## 3. Adding New Frontend Parsers

When adding support for a new state machine or diagram format, follow the two-category frontend rule:

1. **Formal Metamodels (`include/fsm/frontend/formal/`)**:
   - For formats backed by a typed mathematical metamodel (e.g. SysML, SCXML, Cameo XMI, Simulink Stateflow, AUTOSAR ARXML).
   - Override `kind()` returning `FrontendKind::Formal`.
   - Add unit tests under `tests/frontend/formal/test_<format>_parser.cpp`.
2. **Visual Diagrams (`include/fsm/frontend/diagram/`)**:
   - For graphical sketching or text-based diagram notations (e.g. PlantUML, Mermaid, Graphviz DOT, D2).
   - Override `kind()` returning `FrontendKind::Diagram`.
   - Add unit tests under `tests/frontend/diagram/test_<format>_parser.cpp`.

---

## 4. Submitting Changes

1. Fork the repository on GitHub.
2. Create a feature branch: `git checkout -b feature/my-new-feature`.
3. Add unit tests for your changes in `tests/`.
4. Commit your changes: `git commit -m "feat: add support for X"`.
5. Push to your branch and open a Pull Request.

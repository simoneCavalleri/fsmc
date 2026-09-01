# Developer & Contributing Hub

Welcome to the **`fsmc` Developer & Contributing Hub**. Whether you are extending the compiler with new frontend parsers, implementing middle-end optimization passes, adding target code emitters, or optimizing the zero-heap C++ runtime, this section provides the resources and guidelines you need.

---

## Contributing Resources

| Resource | Focus Area | Description | Link |
| :--- | :--- | :--- | :--- |
| **Developer Guide & Recipes** | Extensibility & Workflows | Step-by-step developer recipes for adding parsers, middle-end passes, serializers, and runtime features. | [Developer Guide](../internals/developer_guide.md) |
| **Test Suite Catalog** | Quality & Verification | Comprehensive catalog of all 54 GoogleTest suites with test scenarios and intents. | [Test Suite Catalog](../reference/test_suite_catalog.md) |
| **CMake Integration Reference** | Build System Automation | Documentation of `fsmc_target_sources` macro and imported CMake interface libraries. | [CMake Reference](../reference/cmake_integration.md) |
| **Compiler Architecture** | Pipeline & Drivers | Detailed design of compiler stages, data flow, and Intermediate Representation. | [Compiler Architecture](../internals/architecture.md) |
| **Canonical IR AST Specification** | Data Model | Formal specification of `FsmIr`, `StateNode`, `TransitionEdge`, and port definitions. | [IR Specification](../internals/fsm_ir_specification.md) |

---

## Fast-Track Contribution Workflow

1. **Fork and Clone the Repository:**
   ```bash
   git clone https://github.com/simoneCavalleri/fsmc.git
   cd fsmc
   ```

2. **Configure and Build C++ Targets:**
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Debug -DFSMC_BUILD_TESTS=ON
   cmake --build build -j$(nproc)
   ```

3. **(Optional) Setup Documentation Environment:**
   ```bash
   python3 -m venv .venv
   source .venv/bin/activate
   pip install -r requirements-docs.txt
   ```

4. **Run the Full Verification Pipeline:**
   ```bash
   # Step A: Ensure standalone runtime headers are synchronized
   python3 scripts/generate_standalone_runtime.py --check

   # Step B: Run all 54 CTest test suites (100% must pass)
   ctest --test-dir build --output-on-failure

   # Step C: Verify documentation builds with zero errors
   mkdocs build

   # Step D: (Optional) Live preview local documentation server
   mkdocs serve
   ```

5. **Submit a Pull Request:**
   - Ensure all code compiles cleanly without warnings under `-Wall -Wextra -Wpedantic -Werror`.
   - Write unit tests following the Arrange-Act-Assert (AAA) pattern with standard Doxygen documentation blocks.

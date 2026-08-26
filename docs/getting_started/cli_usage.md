# Command-Line Tools Reference

The `fsmc` toolchain provides two dedicated CLI executables:
1. **`fsmc`**: The primary compiler driver, code generator, and diagram transpiler.
2. **`fsm-opt`**: The intermediate representation optimizer, static analyzer, and pass executor.

---

## 1. `fsmc` Compiler Driver

### Synopsis
```text
fsmc -i <input_model> [OPTIONS]
fsmc [OPTIONS] <input_model>
fsmc -i <input_model> --export <format> -o <output_diagram>
fsmc -i <input_model> --verify
fsmc --export-runtime <directory> [--std 17|20]
```

### Options Reference

#### Input and Output Options
| Flag | Description | Default |
| :--- | :--- | :--- |
| `-i, --input <file>` | Path to input model file (`.sysml`, `.puml`, `.mmd`, `.xmi`, `.scxml`, `.json`, `.dot`). | Positional |
| `-o, --output <file>` | Path to output generated C++ header or exported diagram. | `stdout` |
| `-n, --name <name>` | Generated C++ class name. | Stem of filename |
| `--namespace <ns>` | Generated C++ namespace enclosing the state machine types. | `fsm_generated` |
| `--context <type>` | Custom context struct type name passed to transitions. | `no_context` |
| `--format <fmt>` | Explicitly select parser: `sysml2`, `plantuml`, `mermaid`, `cameo`, `scxml`, `json`, `dot`, `auto`. | `auto` |

#### Code Generation Options
| Flag | Description | Default |
| :--- | :--- | :--- |
| `--std <17\|20>` | Target C++ standard (`17` or `20`). | `17` |
| `--standalone` | Emit self-contained header with embedded zero-alloc runtime. | `true` |
| `--modular` | Emit lightweight header that includes `<fsm/runtime/cpp/fsm.hpp>`. | `false` |
| `--no-thread-safe` | Disable generation of the `thread_safe_fsm` asynchronous wrapper. | `false` |
| `--allow-diagram-codegen` | Explicitly allow C++ generation from informal diagram formats (PlantUML/Mermaid). | `false` |

#### Optimization and Middle-End Options
| Flag | Description | Default |
| :--- | :--- | :--- |
| `-O0, --no-opt` | Disable all middle-end passes. | Enabled (`-O1`) |
| `-O1, -O2, --optimize`| Enable standard optimization passes (canonicalization, inlining). | `-O1` |
| `--prune-dead-states` | Prune unreachable states and statically dead transitions. | `false` (on in `-O2`) |
| `--no-guard-simplification` | Disable algebraic boolean reductions on guards. | `false` |
| `--inline-submachines` | Inline modular submachines referenced via `SubmachineRef`. | `false` |

#### Verification and Compliance Options
| Flag | Description | Default |
| :--- | :--- | :--- |
| `--verify, --check` | Run formal model checker (livelock, choice completeness, EFSM interval analysis) and exit. | `false` |
| `-Werror` | Treat all compiler and middle-end diagnostics warnings as fatal errors. | `false` |
| `--strict-determinism` | Fail compilation on non-deterministic branch collisions or unprioritized triggers. | `false` |
| `--check-races` | Perform static data-race analysis across parallel orthogonal regions. | `false` |
| `--req-audit` | Print Requirement Traceability Matrix to terminal before codegen. | `false` |
| `--rtm-output <file>` | Export Requirement Traceability Matrix report to file. | None |
| `--rtm-format <json\|md>` | Format for RTM export (`markdown` or `json`). | Auto from extension |

---

## 2. `fsm-opt` IR Optimizer & Linter

`fsm-opt` operates directly on the canonical Intermediate Representation (`FsmIr`). It is designed for compiler developers, static analysis pipelines, and CI/CD linting gates.

### Synopsis
```text
fsm-opt -i <input_model> [OPTIONS]
```

### Options Reference

| Flag | Description |
| :--- | :--- |
| `--passes=<p1,p2,...>` | Execute a custom comma-separated list of middle-end passes. |
| `--list-passes` | Print all registered middle-end passes and exit. |
| `--emit-ir` | Emit canonical JSON Intermediate Representation (default). |
| `--emit-smv` | Emit formal nuXmv / SMV verification model. |
| `--emit-sysml` | Emit normalized SysML v2 state definition. |
| `--metrics, --stats` | Print formal graph complexity metrics (states, transitions, cyclomatic complexity). |
| `--profile` | Print per-pass execution timings and memory footprints. |
| `--print-before-all` | Dump IR state before executing pass pipeline. |
| `--print-after-all` | Dump IR state after executing pass pipeline. |

### Available Middle-End Passes in `fsm-opt`

1. `canonicalize`: Normalizes state hierarchy, FQNs, and sorts canonically.
2. `guard-simplification`: Bottom-up boolean algebra reduction ($\neg(\neg A) \to A$, $A \land \text{true} \to A$).
3. `determinism`: Enforces deterministic event dispatch and priority ordering.
4. `race-check`: Static data-race analysis on parallel orthogonal variables.
5. `inline-submachines`: Splicing and inlining of modular SubmachineRef statecharts.
6. `dead-state-pruning`: Physical elimination of unreachable states and dead branches.
7. `choice-completeness`: Verifies choice pseudostate branch exhaustiveness.
8. `choice-inlining`: Collapses choice/junction pseudostates into direct composite transitions.
9. `timed-deadlock`: Detects zero-duration timeouts and timer invariant conflicts.
10. `efsm-data-path`: Abstract interpretation for unreachable variable intervals and dead guards.
11. `safety-verifier`: Graph reachability, deadlock traps, and livelock cycle checks.
12. `model-checking`: Formal verification of temporal LTL/CTL formulas.

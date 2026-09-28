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
| `-i, --input <file>` | Path to input model file (`.sysml`, `.puml`, `.mmd`, `.xmi`, `.scxml`, `.json`, `.dot`, `.sfx`, `.stateflow`, `.xml`). | Positional argument |
| `-o, --output <file>` | Path to output generated code or exported diagram file. | `stdout` |
| `-t, --target <lang>` | Target code generator backend: `cpp` (default). | `cpp` |
| `-n, --name <name>` | Generated state machine class/struct name. | Inferred from filename or `MyFSM` |
| `--namespace, --package <ns>` | Generated namespace / package / module enclosing the state machine types. | `fsm_generated` |
| `--format <fmt>` | Override input parser format: `sysml2`, `plantuml`, `mermaid`, `cameo`, `scxml`, `json`, `dot`, `stateflow`, `auto`. | `auto` |
| `-e, --export <fmt>` | Export diagram or formal model to: `mermaid`, `plantuml`, `sysml2`, `json`, `dot`, `scxml`, `cameo`, `stateflow`, `smv`. | None |
| `-s, --sidecar <file>` | Explicit companion manifest file (`.fsm.yaml`, `.fsm.json`) for diagram ingestion and external property attachment. | None |
| `--submachine-dir <dir>` | Search directory for external submachine diagram files referenced in models. | Current directory |

#### Optimization and Code Transformation
| Flag | Description | Default |
| :--- | :--- | :--- |
| `-O0, --no-opt` | Disable all middle-end optimization passes. | Enabled (`-O1`) |
| `-O1` | Enable standard optimization passes (canonicalization, guard simplification). | Active |
| `-O2, --optimize` | Enable aggressive optimizations including unreachable dead state and transition pruning. | Inactive |
| `--prune-dead-states` | Prune unreachable states and statically dead transitions before codegen. | `false` (active in `-O2`) |
| `--no-guard-simplification` | Disable algebraic boolean reductions on guard expressions. | `false` |
| `--inline-submachines` | Inline modular submachines referenced via `SubmachineRef` into a single flat/composite FSM. | `false` |
| `--pipe-through <cmd>` | Stream canonical JSON IR through an external Unix filter/tool and re-read stdout. | None |
| `--load-pass-plugin <path>` | Dynamically load a C++ shared library (`.so`) implementing custom `fsm::Pass` transformations. | None |

#### Safety, Verification and Compliance
| Flag | Description | Default |
| :--- | :--- | :--- |
| `verify <file>` | Standalone sub-command to run formal verification, deadlocks, invariants, and LTL/CTL checks. | - |
| `--verify, --check` | Run formal model checker (livelock, choice completeness, EFSM interval analysis, reachability) and exit. | `false` |
| `--engine <auto\|nuxmv>` | Verification engine selector (runs internal `ModelChecker`; for nuXmv solver execution, export via `-e smv`). | `auto` |
| `--ltl "<formula>"` | Injects an ad-hoc Linear Temporal Logic property for verification. | `""` |
| `--ctl "<formula>"` | Injects an ad-hoc Computation Tree Logic property for verification. | `""` |
| `-Werror` | Treat all compiler diagnostics and middle-end warnings as fatal errors. | `false` |
| `--strict-determinism` | Fail compilation on non-deterministic branch collisions or unprioritized triggers. | `false` |
| `--check-races` | Perform static data-race analysis across parallel orthogonal regions. | `false` |
| `--emit-test-harness <file>` | Synthesize a standalone GoogleTest C++ harness verifying MC/DC condition coverage (C++ API: `McdcHarnessGenerator`). | None |
| `--req-audit` | Print Requirement Traceability Matrix (`@fsm:req`) to terminal before code generation. | `false` |
| `--rtm-output <file>` | Export Requirement Traceability Matrix report to a file. | None |
| `--rtm-format <json\|md>` | Format for Requirement Traceability Matrix export (`markdown` or `json`). | Inferred from file extension |

#### C++ Backend Options (`--target cpp`)
| Flag | Description | Default |
| :--- | :--- | :--- |
| `--std <17\|20>` | Target C++ standard (`17` or `20`). | `17` |
| `--c++17, -std=c++17` | Shortcut alias to target C++17 standard. | `17` |
| `--c++20, -std=c++20` | Shortcut alias to target C++20 standard. | `17` |
| `--standalone` | Emit self-contained header with embedded zero-alloc runtime (0 external dependencies). | `true` |
| `--modular` | Emit lightweight header that includes external `<fsm/backend/cpp/runtime/fsm.hpp>`. | `false` |
| `--export-runtime <dir>` | Export standalone runtime library headers (`fsm.hpp`, `spsc_fsm.hpp`, etc.) to the specified directory. | None |
| `--no-thread-safe` | Disable generation of the `thread_safe_fsm` asynchronous wrapper. | `false` |
| `--no-stubs` | Do not emit default stub functors for actions and guards. | `false` |
| `--allow-diagram-codegen` | Explicitly allow C++ generation from informal visual diagram formats (PlantUML, Mermaid, DOT, JSON). | `false` |

#### General Options
| Flag | Description |
| :--- | :--- |
| `-h, --help` | Show command-line help message and exit. |
| `-v, --version` | Show version information and exit. |

---

## 2. `fsm-opt` IR Optimizer & Linter

`fsm-opt` operates directly on the canonical Intermediate Representation (`FsmIr`). It is designed for compiler developers, static analysis pipelines, and CI/CD linting gates.

### Synopsis
```text
fsm-opt -i <input_model> [OPTIONS]
fsm-opt [OPTIONS] <input_model>
```

### Options Reference

#### Input and Output Options
| Flag | Description | Default |
| :--- | :--- | :--- |
| `-i, --input <file>` | Input model or IR file (`.sysml`, `.puml`, `.mmd`, `.xmi`, `.scxml`, `.json`, `.dot`, `.sfx`, `.stateflow`, `.xml`). | Positional argument |
| `-o, --output <file>` | Output file path for transformed IR or emitted model. | `stdout` |
| `--format <fmt>` | Override parser format (`sysml2`, `plantuml`, `mermaid`, `cameo`, `scxml`, `json`, `dot`, `stateflow`). | `auto` |

#### Pass Pipeline and Optimization
| Flag | Description | Default |
| :--- | :--- | :--- |
| `--passes=<p1,p2,...>` | Execute a custom comma-separated list of middle-end passes. | Standard pipeline |
| `--list-passes` | List all available registered Middle-End passes and exit. | - |
| `--prune-dead` | Enable dead state and dead transition pruning pass. | `false` |
| `--pipe-through <cmd>` | Stream canonical JSON IR through an external Unix filter/tool and re-read stdout. | None |
| `--load-pass-plugin <path>` | Dynamically load a C++ shared library (`.so`) implementing custom `fsm::Pass` transformations. | None |
| `--print-before-all` | Dump IR state in JSON format before executing pass pipeline. | `false` |
| `--print-after-all` | Dump IR state in JSON format after executing pass pipeline. | `false` |
| `-Werror` | Treat all diagnostic warnings as fatal errors. | `false` |

#### IR Serialization and Formal Emission
| Flag | Description | Default |
| :--- | :--- | :--- |
| `--emit-ir` | Emit optimized canonical JSON Intermediate Representation. | Default |
| `--emit-puml` | Emit canonical PlantUML state diagram. | None |
| `--emit-mmd` | Emit canonical Mermaid `stateDiagram-v2`. | None |
| `--emit-sysml` | Emit canonical OMG SysML v2 state definition. | None |
| `--emit-json` | Emit canonical XState JSON statechart. | None |
| `--emit-dot` | Emit canonical Graphviz DOT diagram. | None |
| `--emit-scxml` | Emit canonical W3C SCXML statechart. | None |
| `--emit-cameo` | Emit canonical Cameo / MagicDraw OMG XMI 2.1 model. | None |
| `--emit-stateflow` | Emit canonical MathWorks Simulink Stateflow XML chart. | None |
| `--emit-smv` | Emit canonical nuXmv / SMV formal verification specification. | None |

#### Analysis, Model Checking and Metrics
| Flag | Description |
| :--- | :--- |
| `--metrics, --stats` | Display formal graph complexity, state count, and transition metrics. |
| `--profile` | Print PassManager execution timings and memory footprints. |
| `--verify, --check` | Run formal model checking passes (Safety, LTL/CTL, Reachability). |

#### General Options
| Flag | Description |
| :--- | :--- |
| `-h, --help` | Show command-line help message and exit. |
| `-v, --version` | Show version information and exit. |

---

## 3. Middle-End Optimization Passes Catalogue

The `fsmc` middle-end provides 30 modular passes (28 standard passes across 7 pipeline stages, plus dynamic plugins and the pipe-through filter) executable via `fsm-opt --passes=...` or automatically configured by compiler optimization levels (`-O0`, `-O1`, `-O2`):

1. **`canonicalize`**: Normalizes state hierarchy, fully qualified names (`FQN`), and sorts nodes and transitions into deterministic order.
2. **`guard-simplification`**: Bottom-up boolean algebra reduction (`not(not A) => A`, `A and true => A`, `A and false => false`).
3. **`determinism`**: Enforces deterministic event dispatch ordering and validates transition priorities.
4. **`race-check`**: Static concurrency data-race analysis on shared variables accessed across parallel orthogonal regions.
5. **`inline-submachines`**: Splicing and inlining of modular `SubmachineRef` statecharts into a single composite hierarchy.
6. **`dead-state-pruning`**: Elimination of unreachable states and dead transition branches.
7. **`choice-completeness`**: Verifies choice pseudostate branch exhaustiveness and presence of unconditional fallback branches.
8. **`choice-inlining`**: Collapses choice and junction pseudostates into direct composite transitions.
9. **`timed-deadlock`**: Detects zero-duration timeouts and racing timer invariant conflicts.
10. **`efsm-data-path`**: Abstract interpretation for unreachable variable intervals and dead guard constraints.
11. **`safety-verifier`**: Graph reachability, deadlock traps, and livelock cycle validation.
12. **`model-checking`**: Formal symbolic verification of temporal LTL and CTL formulas against Kripke state transition graphs.
13. **`orthogonal-product`**: Cartesian product expansion and flattening of parallel orthogonal regions ($S_A \times S_B$).
14. **`wcet-analysis`**: Static Worst-Case Execution Time estimation, micro-step cascade bounds, and Zeno-cycle detection.
15. **`constant-folding`**: Register value propagation, boolean algebra simplification, and tautology/contradiction dead branch pruning.
16. **`state-minimization`**: Hopcroft/Moore state minimization collapsing bisimilar/equivalent states into minimal representations.
17. **`guard-satisfiability`**: SMT-based or domain-based guard unsatisfiability analysis.
18. **`fork-join-lowering`**: Lowers fork splits and join rendezvous barriers into product-state transitions.
19. **`history-lowering`**: Lowers shallow and deep history into shadow state registers.
20. **`deferred-event-lowering`**: Lowers deferred events into bounded FIFO buffers.
21. **`boundary-action-fusion`**: Flattens LCA boundary cascades into linear action sequences (exit $\to$ transition action $\to$ entry).
22. **`dead-action-elimination`**: Dead store elimination (DSE) across datapath variables and unused assignments.
23. **`register-liveness`**: Liveness analysis and variable interference graph construction.
24. **`transition-fusion`**: Fuses deterministic zero-time microsteps into macro-transitions.
25. **`common-action-factoring`**: Factors identical actions across convergent/divergent transition edges.
26. **`livelock-analysis`**: Detects non-progressive internal cycles.
27. **`priority-conflict-check`**: Verifies hierarchical preemption determinism and priority uniqueness.
28. **`timed-invariants-verifier`**: Formally verifies clock invariants against outgoing transition deadlines.
29. **`event-queue-bound`**: Computes static upper bound on event queue depth.
30. **`pipe-through`**: Filters and transforms IR via external Unix command.

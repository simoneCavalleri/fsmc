# Master Test Suite & Behavioral Verification Catalog

> **Note**: This catalog is automatically generated from the in-code `@brief Test Intent` comments across `tests/`.
> To update this file, run: `cmake --build build --target generate_test_catalog` or `python3 scripts/generate_test_catalog.py`.

**Total Documented Subsystems**: 5  
**Total Test Suites & Binaries**: 25  
**Total Documented Test Cases**: 102  

---

## Frontend Parser Subsystem

### [`test_parser_classification.cpp`](../tests/frontend/common/test_parser_classification.cpp) (`tests/frontend/common/test_parser_classification.cpp`)
- *(Executable binary test verification)*

### [`test_parser_factory_and_lexer.cpp`](../tests/frontend/common/test_parser_factory_and_lexer.cpp) (`tests/frontend/common/test_parser_factory_and_lexer.cpp`)
- *(Executable binary test verification)*

### [`test_parser_negative.cpp`](../tests/frontend/common/test_parser_negative.cpp) (`tests/frontend/common/test_parser_negative.cpp`)
#### `ParserNegativeTest.PlantUmlRejectsMalformedInputs`
**Test Intent**: Verify PlantUmlParser rejects malformed, empty, and corrupted syntax with informative error

**Scenario**:
  - Pass empty string, header-only diagram, and corrupted tokens to PlantUmlParser.
  - Verify parse() returns false and populates the error string.

#### `ParserNegativeTest.MermaidRejectsMalformedInputs`
**Test Intent**: Verify MermaidParser rejects empty inputs and malformed transition statements.

**Scenario**:
  - Test empty input, header-only diagram, and invalid transition arrows.
  - Verify parse failure is reported cleanly.

#### `ParserNegativeTest.XmlParsersRejectCorruptInputs`
**Test Intent**: Verify XML parsers (Cameo XMI and W3C SCXML) reject malformed XML tags and non-XML text.

**Scenario**:
  - Feed unclosed XML tags and plain text to CameoXmiParser and ScxmlParser.
  - Verify parsing fails without exceptions.

#### `ParserNegativeTest.JsonParserRejectsInvalidInputs`
**Test Intent**: Verify JsonStateParser rejects invalid JSON syntax, wrong root types, and empty objects.

**Scenario**:
  - Pass unquoted keys, JSON arrays, and state machines with 0 states.
  - Verify rejection and non-empty error message.

#### `ParserNegativeTest.Sysml2RejectsMalformedInputs`
**Test Intent**: Verify Sysml2Parser rejects empty definitions and invalid token streams.

**Scenario**:
  - Pass empty text, empty state def blocks, and invalid tokens to Sysml2Parser.
  - Verify parser returns false.

#### `ParserNegativeTest.ModelCheckerDetectsDefects`
**Test Intent**: Verify FsmValidator semantic diagnostics (unreachable island states, trap/deadlock states).

**Scenario**:
  - Construct model with unreachable state "Island" and trap state "BlackHole" (no exit transitions).
  - Verify FsmValidator emits semantic warnings for both design defects.

### [`test_dot_parser.cpp`](../tests/frontend/diagram/test_dot_parser.cpp) (`tests/frontend/diagram/test_dot_parser.cpp`)
#### `DotParserTest.BasicDotParsing`
**Test Intent**: Verify Graphviz DOT graph parsing with transition labels and initial pseudostate (`__start__`).

**Scenario**:
  - Parse Graphviz DOT `digraph` with edge labels formatted as `event [guard] / action`.
  - Verify initial point node points to Disconnected, and transitions are populated into FsmIr.

#### `DotParserTest.CompositeClusterParsing`
**Test Intent**: Verify DOT `subgraph cluster_<Name>` parsing into hierarchical composite states.

**Scenario**:
  - Parse DOT graph containing a cluster subgraph `cluster_InFlight`.
  - Verify InFlight is parsed as a Composite StateKind with nested sub-states.

### [`test_json_parser.cpp`](../tests/frontend/diagram/test_json_parser.cpp) (`tests/frontend/diagram/test_json_parser.cpp`)
#### `JsonParserTest.BasicJsonParsing`
**Test Intent**: Verify XState-compatible JSON statechart format parsing.

**Scenario**:
  - Parse JSON state machine with states, `"on"` event maps, target strings, guards, and action lists.
  - Verify IR elements are populated accurately.

#### `JsonParserTest.CompositeStatesParsing`
**Test Intent**: Verify nested composite states within JSON schema.

**Scenario**:
  - Parse JSON with nested `"states"` property inside `"InFlight"`.
  - Verify composite state flags and sub-state parent mappings.

#### `JsonParserTest.ArrayTransitionsPerEvent`
**Test Intent**: Verify array of conditional transitions per event key in JSON.

**Scenario**:
  - Parse `"ConnectCmd": [ { target: ..., guard: ... }, { target: ..., guard: ... } ]`.
  - Verify multiple transitions for the same event trigger are captured.

#### `JsonParserTest.DocumentInsertionOrderPreservation`
**Test Intent**: Verify document insertion order preservation of state definitions in JSON.

**Scenario**:
  - Define states in specific order: ZetaState -> AlphaState -> MuState -> BetaState.
  - Verify FsmIr preserves this exact ordering.

#### `JsonParserTest.ParsePortsAndContracts`
**Test Intent**: Verify parsing of v0.4.0 typed ports and range constraints in JSON schema.

### [`test_plantuml_parser.cpp`](../tests/frontend/diagram/test_plantuml_parser.cpp) (`tests/frontend/diagram/test_plantuml_parser.cpp`)
#### `ParserTest.MermaidBasicParsingAndValidation`
**Test Intent**: Verify Mermaid syntax parsing, state aliases, guard/action extraction, and validation.

**Scenario**:
  - Parse Mermaid `stateDiagram-v2` with state aliases, transition labels, guards `[Guard]`, and actions `/ Action`.
  - Verify FsmIr element counts and validation pass.

#### `ParserTest.MermaidCommentsNotesAndComplexHierarchy`
**Test Intent**: Verify Mermaid comment stripping (`%%`), note stripping, and composite state hierarchy.

**Scenario**:
  - Parse Mermaid diagram containing comments, notes, and nested composite states.
  - Verify parent-child links and initial sub-state assignment.

#### `ParserTest.PlantUmlBasicParsingAndValidation`
**Test Intent**: Verify basic PlantUML syntax parsing and model validation.

**Scenario**:
  - Parse PlantUML with transitions, guards, actions, and initial state pointer.
  - Verify FsmIr element extraction and FsmValidator passing.

#### `ParserTest.PlantUmlCommentsAndCompositeHierarchy`
**Test Intent**: Verify PlantUML single-line and multi-line comment stripping and composite states.

**Scenario**:
  - Parse PlantUML containing `' comment` and `/' ... '/` block comments with internal transitions.
  - Verify hierarchy and internal state actions.

#### `ParserTest.ValidatorDetectsMissingTargetAndDeadlocks`
**Test Intent**: Verify FsmValidator detects undefined transition target states.

**Scenario**:
  - Construct FsmIr with transition to a non-existent state `UnknownTarget`.
  - Verify FsmValidator::validate() reports errors and fails validity check.

#### `ParserTest.ParserRejectsEmptyInput`
**Test Intent**: Verify parsers gracefully reject empty and whitespace-only inputs.

**Scenario**:
  - Feed empty string and whitespace-only string to PlantUmlParser and MermaidParser.
  - Verify parser returns false with an informative error message.

#### `ParserTest.PlantUmlEntryExitPointPriorityAndInvariant`
**Test Intent**: Verify PlantUML parsing of entryPoint, exitPoint, stay duration (time invariant), and transition

**Scenario**:
  - Parse PlantUML with `state ep <<entryPoint>>`, `state xp <<exitPoint>>`, `Active : invariant stay <= 100ms`, and
  - `(prio=3)`.
  - Verify IR captures StateKind::EntryPoint, StateKind::ExitPoint, time_invariant, and transition priority.

#### `ParserTest.MermaidEntryExitPointAndPriority`
**Test Intent**: Verify Mermaid parsing of entryPoint, exitPoint, and transition priority.

**Scenario**:
  - Parse Mermaid with `state ep <<entryPoint>>`, `state xp <<exitPoint>>`, and `Idle --> Active : (prio=4) EvStart`.
  - Verify IR captures StateKind::EntryPoint, StateKind::ExitPoint, and transition priority.

#### `ParserTest.PlantUmlAndMermaidPortDirectives`
**Test Intent**: Verify PlantUML and Mermaid parsing of @fsm:port directives into FsmIr.

### [`test_directive_parser.cpp`](../tests/frontend/directive/test_directive_parser.cpp) (`tests/frontend/directive/test_directive_parser.cpp`)
#### `DirectiveParserTest.ParseStateDirective`
**Test Intent**: Verify `@fsm:state` directive parsing for traceability requirements and history metadata.

**Scenario**:
  - Parse `@fsm:state history=deep satisfies=["REQ-1", "SAFETY-04"] do_activity="sensor_worker"`.
  - Verify state kind is DeepHistory, requirements array is populated, and do_activity is set.

#### `DirectiveParserTest.ParseDeferDirective`
**Test Intent**: Verify `@fsm:defer [...]` directive parsing for deferred events.

**Scenario**:
  - Parse `%% @fsm:defer [EvSensorReady, EvAck, EvTimeout]`.
  - Verify all 3 event identifiers are parsed into state deferred_events.

#### `DirectiveParserTest.ParseSignalDirective`
**Test Intent**: Verify `@fsm:signal` directive parsing with payload attributes and validation expressions.

**Scenario**:
  - Parse `' @fsm:signal EvPacketRecv{uint32_t len, const uint8_t* ptr} validator="len > 0 && ptr != nullptr"'`.
  - Verify SignalDefinition attributes, types, and validator constraints are parsed.

#### `DirectiveParserTest.ParseTransDirective`
**Test Intent**: Verify `@fsm:trans` directive parsing for custom transition IDs, guard ASTs, and actions.

**Scenario**:
  - Parse `%% @fsm:trans id="tr_001" guard_ast="ctx.is_valid(payload)" action_sig="ctx.on_data(payload)"`.
  - Verify TransitionEdge metadata is populated.

#### `DirectiveParserTest.ParsePortDirective`
**Test Intent**: Verify `@fsm:port` directive parsing with direction, numeric bounds, and constraint expression.

### [`test_cameo_parser.cpp`](../tests/frontend/formal/test_cameo_parser.cpp) (`tests/frontend/formal/test_cameo_parser.cpp`)
#### `CameoParserTest.BasicXmiParsing`
**Test Intent**: Verify Cameo Systems Modeler OMG XMI 2.x standard XML schema parsing.

**Scenario**:
  - Parse XML containing `<uml:StateMachine>`, `<subvertex xmi:type="uml:State">`, `<transition>`, `<trigger>`,
  - `<effect>`.
  - Verify initial pseudostate and transitions are mapped accurately to FsmIr.

#### `CameoParserTest.CompositeAndChoiceParsing`
**Test Intent**: Verify Cameo nested composite regions and choice pseudostates (`kind="choice"`).

**Scenario**:
  - Parse XML with nested regions and choice nodes.
  - Verify choice resolution and composite state structure.

#### `CameoParserTest.AttributeStyleEffectAndActionParsing`
**Test Intent**: Verify attribute-style XML transition properties (`trigger=...`, `guard=...`, `effect=...`).

**Scenario**:
  - Parse XML with inline attributes instead of child XML nodes.
  - Verify actions and guards are identified accurately.

#### `CameoParserTest.NativeEntryExitAndDoActivity`
**Test Intent**: Verify Cameo `<entry>`, `<doActivity>`, `<exit>`, and `<deferrableTrigger>` parsing.

**Scenario**:
  - Parse XML containing state lifecycle activities.
  - Verify actions and deferred events are recorded on StateNode.

#### `CameoParserTest.XmlEntityDecodingInNamesAndGuards`
**Test Intent**: Verify XML entity decoding (`&amp;`, `&lt;`, `&gt;`, `&quot;`, `&apos;`) and identifier

**Scenario**:
  - Parse XML with encoded entity characters in attribute values.
  - Verify entities are unescaped before identifier sanitization.

#### `CameoParserTest.HistoryAndJunctionPseudostates`
**Test Intent**: Verify Cameo UML pseudostates (`shallowHistory`, `deepHistory`, `junction`).

**Scenario**:
  - Parse XML with UML pseudostates and verify parsing completes cleanly.

### [`test_scxml_parser.cpp`](../tests/frontend/formal/test_scxml_parser.cpp) (`tests/frontend/formal/test_scxml_parser.cpp`)
#### `ScxmlParserTest.BasicScxmlParsingWithInternalTransitions`
**Test Intent**: Verify W3C SCXML parsing with internal transitions and `<send event="..."/>` actions.

**Scenario**:
  - Parse SCXML document with transitions containing guards (`cond="..."`) and child action tags (`<send>`).
  - Verify targetless transitions are categorized as internal transitions.

#### `ScxmlParserTest.UserReportedIndustrialPressSnippet`
**Test Intent**: Verify SCXML industrial press controller snippet with attribute-based actions.

**Scenario**:
  - Parse 6-state industrial machine with transition attributes `action="ActionName"`.
  - Verify all 9 transitions, guards, and action bindings are captured.

#### `ScxmlParserTest.AttributePermutationsAndSelfClosingStates`
**Test Intent**: Verify SCXML attribute ordering permutations and self-closing `<state .../>` tags.

**Scenario**:
  - Parse SCXML with varying XML attribute order and empty leaf states.
  - Verify seamless parsing without tag mismatch errors.

#### `ScxmlParserTest.NativeDatamodelAndLifecycleHooks`
**Test Intent**: Verify W3C SCXML `<datamodel>`, `<data>`, `<onentry>`, `<onexit>`, and `<assign>` tags.

**Scenario**:
  - Parse SCXML datamodel definitions (`<data id="..." expr="..." type="..."/>`).
  - Parse `<onentry>` and `<onexit>` action blocks with variable assignments.
  - Verify FsmIr variables and state lifecycle action signatures are captured.

### [`test_smv_parser.cpp`](../tests/frontend/formal/test_smv_parser.cpp) (`tests/frontend/formal/test_smv_parser.cpp`)
#### `SmvParserTest.BasicSmvParsing`
**Test Intent**: Verify formal nuXmv / SMV parsing of states, events, and transitions.

#### `SmvParserTest.SmvVariablesAndInit`
**Test Intent**: Verify SMV variable ranges and initial assignments.

#### `SmvParserTest.SmvLtlAndInvariants`
**Test Intent**: Verify SMV temporal specifications (LTLSPEC and INVARSPEC).

#### `SmvParserTest.SmvCodegenCompatibility`
**Test Intent**: Verify C++20 code generation from SMV-parsed model.

#### `SmvParserTest.NegativeErrorHandling`
**Test Intent**: Error handling for invalid/empty SMV content.

### [`test_sysml2_flight_control.cpp`](../tests/frontend/formal/test_sysml2_flight_control.cpp) (`tests/frontend/formal/test_sysml2_flight_control.cpp`)
- *(Executable binary test verification)*

### [`test_sysml2_parser.cpp`](../tests/frontend/formal/test_sysml2_parser.cpp) (`tests/frontend/formal/test_sysml2_parser.cpp`)
#### `Sysml2ParserTest.MultilineTransitionParsing`
**Test Intent**: Verify OMG SysML v2 multi-line transition syntax parsing.

**Scenario**:
  - Parse `transition name first Source accept Event if Guard do Action then Target;`.
  - Verify name, initial state, triggers, guards, actions, and target states are captured in IR.

#### `Sysml2ParserTest.CompactTransitionParsing`
**Test Intent**: Verify OMG SysML v2 compact transition syntax parsing.

**Scenario**:
  - Parse shorthand `transition from S accept E do A then D;`.
  - Verify all transition elements are populated into the transition table model.

#### `Sysml2ParserTest.CompositeStatesAndCodegen`
**Test Intent**: Verify SysML v2 composite state hierarchy and C++ code generator emission.

**Scenario**:
  - Parse nested `state Standby { entry; then Diagnostics; ... }`.
  - Verify composite metadata and compile generated C++ standalone code.

#### `Sysml2ParserTest.NativeSysml2AttributesAndItemDefs`
**Test Intent**: Verify SysML v2 attribute declarations, typed item defs (signals), and state actions.

**Scenario**:
  - Parse `attribute battery_percent : Integer = 100;`.
  - Parse `item def EvTelemetry { attribute battery_mv : Integer; ... }`.
  - Parse `satisfy requirement ...`, `entry action`, `do action`, `exit action`, `defer`.
  - Verify types are mapped correctly to C++ primitives (uint32_t, float, bool).

#### `Sysml2ParserTest.ParallelRegionsAndSubmachineRef`
**Test Intent**: Verify SysML v2 parallel orthogonal states and submachine references.

**Scenario**:
  - Parse `parallel state Operational { state NavRegion ... state CommsRegion ... }`.
  - Parse submachine invocation `state SubGuidance :> GuidanceSubmachine;`.
  - Verify IR correctly classifies states and links submachines.

#### `Sysml2ParserTest.EntryExitPointTimeInvariantAndPriority`
**Test Intent**: Verify SysML v2 parsing of EntryPoint, ExitPoint, stay duration (time invariant), and transition

**Scenario**:
  - Parse state machine with `entry point EnPort;`, `exit point ExPort;`, `stay duration <= 500[ms];`, and `transition
  - [priority=10]`.
  - Verify IR captures StateKind::EntryPoint, StateKind::ExitPoint, time_invariant, and transition priority.

#### `Sysml2ParserTest.ChoiceNodeComparisonGuardParsing`
**Test Intent**: Verify SysML v2 choice pseudostate guard parsing with comparison operators.

**Scenario**:
  - Parse a `decide` node with outgoing branches guarded by `> 30.0`, `!= false`, and `else`.
  - Verify that the IR captures distinct, non-empty guard expressions for each branch.
  - Verify that boolean keyword aliases (not, and, or) are normalized to C++ operators.
  - Verify that the choice node is eliminated and transitions are inlined from the source state.

#### `Sysml2ParserTest.SemanticEfsmAssignmentActionParsing`
**Test Intent**: Verify SysML v2 semantic EFSM action parsing from do { } assignment blocks.

**Scenario**:
  - Parse `do { counter = counter + 1; }` on a transition.
  - Verify the IR emits a named semantic action (increment_counter) rather than a raw expression.
  - Parse `do { value += 5; }` and verify an assign_value action with += semantics.

---

## Formal IR Subsystem

### [`test_fsm_ir.cpp`](../tests/ir/test_fsm_ir.cpp) (`tests/ir/test_fsm_ir.cpp`)
#### `FsmIrTest.ModularHeaderSubcomponents`
**Test Intent**: Verify modular IR header decoupling, enum converters, and trigger variants.

**Scenario**:
  - Validate conversions for StateKind, TransitionEdgeKind, and TriggerVariant.
  - Verify ActionSignature and ActionAssignment fields.

#### `FsmIrTest.DeterministicIdGeneration`
**Test Intent**: Verify deterministic FNV-1a 64-bit ID computation for state node identification.

**Scenario**:
  - Compute hashes for identical and differing hierarchical strings.
  - Verify stability across runs and uniqueness across different state names.

#### `FsmIrTest.StateHierarchyAndOrthogonalRegions`
**Test Intent**: Verify hierarchical state representations, orthogonal regions, and JSON serialization.

**Scenario**:
  - Build composite state with parallel orthogonal regions.
  - Canonicalize and serialize to JSON.
  - Verify all orthogonal regions, signals, and guard ASTs are faithfully preserved.

#### `FsmIrTest.TemporalPropertiesAndFormalVerificationAst`
**Test Intent**: Verify formal verification AST representation for temporal properties (LTL/CTL).

**Scenario**:
  - Build safety property AST: `G (LowBattery -> F SafeLand)`.
  - Build mutual exclusion invariant AST: `G (!(StateA && StateB))`.
  - Verify canonical sorting, requirement traceability link, and JSON serialization.

#### `FsmIrTest.StateVariablesAndStructuredActions`
**Test Intent**: Verify extended finite state machine (EFSM) state variables and bounded domains.

**Scenario**:
  - Define variables with min/max bounds and initial values.
  - Define transition edge with assignments `retry_count = retry_count + 1`.
  - Verify serialization to JSON.

#### `FsmIrTest.ForkJoinTransitionsAndSubmachines`
**Test Intent**: Verify Fork/Join multi-source / multi-target transitions and submachine references.

**Scenario**:
  - Construct Fork transition (1 source -> 2 targets) and Join transition (2 sources -> 1 target).
  - Construct SubmachineRef with port mappings.
  - Verify serialization to JSON.

#### `FsmIrTest.DeterministicIdCollisionResistanceAcrossLargeSet`
**Test Intent**: Verify collision resistance of deterministic ID generator across 10,000 keys.

**Scenario**:
  - Generate 10,000 unique hierarchical state keys.
  - Verify each computed deterministic ID is completely unique with 0 collisions.

#### `FsmIrTest.FormalPropertyAstConstruction`
**Test Intent**: Verify manual AST construction for temporal logic implications (`P -> Q`).

**Scenario**:
  - Construct composite PropertyAstNode representing `Globally(SafetyLock) -> Finally(Arming)`.
  - Verify operator, children, and properties.

#### `FsmIrTest.PriorityTimeInvariantAndEntryExitPoints`
**Test Intent**: Verify priority, time_invariant, EntryPoint, and ExitPoint state kinds in FsmIr.

**Scenario**:
  - Create states with EntryPoint and ExitPoint kinds.
  - Set time_invariant on state and priority on transition edge.
  - Verify serialization to JSON preserves time_invariant and priority.

#### `FsmIrTest.DomainPortSeparationAndZeroContext`
**Test Intent**: Verify domain-separated PortDefinition, SignalDefinition, VariableDefinition and zero Context references.

---

## Middle-End Verification & Transformation Subsystem

### [`test_model_checker.cpp`](../tests/middleend/analysis/test_model_checker.cpp) (`tests/middleend/analysis/test_model_checker.cpp`)
#### `ModelCheckerTest.SoundModelVerification`
**Test Intent**: Verify formal validation passes for a sound state machine with zero defects.

**Scenario**:
  - Validate standard FSM (Idle -> Running -> Paused/Stopped -> [*]).
  - Verify validation result has is_valid == true and 0 errors.

#### `ModelCheckerTest.LivelockCycleDetection`
**Test Intent**: Verify model checker detection of livelock cycles with no exit transitions.

**Scenario**:
  - Parse circular loop: StateA -> StateB -> StateC -> StateA.
  - Verify diagnostic engine emits SafetyCritical diagnostic for Livelock.

#### `ModelCheckerTest.ChoiceMissingFallback`
**Test Intent**: Verify model checker detects Choice nodes lacking an unconditional fallback branch.

**Scenario**:
  - Choice node branches on [IsFast] and [IsSlow] without a default else branch.
  - Verify SafetyCritical Choice diagnostic is emitted.

#### `ModelCheckerTest.ChoiceDuplicateGuards`
**Test Intent**: Verify model checker detects duplicate/conflicting guard conditions on Choice branches.

**Scenario**:
  - Choice node has two outgoing branches with identical guard `[IsFast]`.
  - Verify warning diagnostic is emitted for non-deterministic choice guards.

#### `ModelCheckerTest.DeadlockTrapState`
**Test Intent**: Verify model checker detects deadlock/trap states (states with no exit transitions).

**Scenario**:
  - Active transitions to TrapState on ErrorEvent, and TrapState has 0 outgoing transitions.
  - Verify Deadlock diagnostic warning is reported.

#### `ModelCheckerTest.NondeterministicTransitionConflict`
**Test Intent**: Verify model checker detects non-deterministic transition conflicts for identical events.

**Scenario**:
  - State Idle has two unconditional transitions for the same event `StartCmd` (one to StateA, one to StateB).
  - Verify SafetyCritical Determinism conflict diagnostic is emitted.

#### `ModelCheckerTest.DuplicateTimerTransitions`
**Test Intent**: Verify model checker detects duplicate timer transitions from the same state.

**Scenario**:
  - State Active has two transitions with identical timer duration `after_500ms`.
  - Verify TimedTransition diagnostic warning is emitted.

#### `ModelCheckerTest.EFSMDataPathIntervalAnalysis`
**Test Intent**: Verify EFSM Interval Analysis detects unsatisfiable guard conditions across data paths.

**Scenario**:
  - Define EFSM with batteryLevel initialized to 20.
  - Transition Idle -> Active with assignment batteryLevel = batteryLevel + 10 (range [30, 30]).
  - Transition Active -> Turbo with unsatisfiable guard 'batteryLevel > 100'.
  - Verify EFSMIntervalAnalyzer flags the dead branch with W_EFSM_UNSATISFIABLE_GUARD warning.

### [`test_model_checker_ltl.cpp`](../tests/middleend/analysis/test_model_checker_ltl.cpp) (`tests/middleend/analysis/test_model_checker_ltl.cpp`)
#### `LtlParserTest.BasicUnaryAndBinaryOperators`
**Test Intent**: Verify LTL formula tokenization and operator parsing (G, F, X, !, &&, ||, U, ->).

**Scenario**:
  - Parse unary temporal operators (Globally, Finally, Next, Not).
  - Parse binary temporal operators (And, Or, Until, Implies).

#### `LtlParserTest.ComplexTemporalFormulas`
**Test Intent**: Verify complex nested temporal logic formulas (response properties, mutual exclusion).

**Scenario**:
  - Parse `G (LowBattery -> F SafeLand)`.
  - Parse `G (! (StateA && StateB))`.

#### `DirectiveParserTest.PropertyAndVariableDirectives`
**Test Intent**: Verify `@fsm:property` and `@fsm:var` directive parsing.

**Scenario**:
  - Parse formal property directive string with LTL specification and traceability requirement ID.
  - Parse variable directive string with domain bounds min/max.

#### `ModelCheckerTest.SafetyInvariantPassedAndViolated`
**Test Intent**: Verify safety invariant evaluation, violation detection, and step-by-step trace generation.

**Scenario**:
  - Verify invariant `G (! (Idle && Armed))` passes.
  - Verify safety property `G (! HazardFault)` fails, generating a 3-step counterexample trace (Idle -> Arming ->

#### `ModelCheckerTest.ResponseLivenessVerification`
**Test Intent**: Verify response liveness property verification (`G (Trigger -> F Target)`).

**Scenario**:
  - State machine moves Standby -> InFlight -> ReturnToHome -> Landed.
  - Verify property `G (InFlight -> F Landed)` passes.

#### `ModelCheckerTest.PassManagerIntegration`
**Test Intent**: Verify integration of formal verification within PassManager optimization pipeline.

**Scenario**:
  - Run PassManager default pipeline over an FSM with liveness properties.
  - Verify pipeline execution succeeds with 0 diagnostic errors.

#### `SmvSerializerTest.GenerateValidSmvModule`
**Test Intent**: Verify nuXmv / SMV formal model generation with state transitions and LTLSPEC.

**Scenario**:
  - Serialize FsmIr with state variables and properties into SMV format.
  - Verify `MODULE main`, state domain, variable domains, init state, and `LTLSPEC` clauses are emitted.

#### `ModelCheckerTest.CounterexampleTraceGenerationOnViolation`
**Test Intent**: Verify counterexample trace generation when reaching a prohibited fatal error state.

**Scenario**:
  - FSM reaches FatalError on Fault trigger.
  - Property asserts `G (!FatalError)`.
  - Verify counterexample trace accurately begins in Idle and concludes in FatalError.

#### `SmvSerializerTest.GenerateCtlSpecProperties`
**Test Intent**: Verify SMV serializer emission for CTLSPEC temporal properties.

**Scenario**:
  - Verify SMV serializer formats CTL properties and state declarations accurately.

### [`test_timed_smv.cpp`](../tests/middleend/analysis/test_timed_smv.cpp) (`tests/middleend/analysis/test_timed_smv.cpp`)
- *(Executable binary test verification)*

### [`test_middleend_passes.cpp`](../tests/middleend/passes/test_middleend_passes.cpp) (`tests/middleend/passes/test_middleend_passes.cpp`)
#### `MiddleendPassesTest.GuardSimplificationAlgebraicReductions`
**Test Intent**: Verify algebraic boolean simplifications (double negation, constant folding, idempotency).

**Scenario**:
  - Simplify !(!Ready) -> Ready.
  - Simplify Ready && true -> Ready.
  - Simplify Ready && false -> false.
  - Simplify Ready || false -> Ready.
  - Simplify Ready || true -> true.
  - Simplify Ready && Ready -> Ready.

#### `MiddleendPassesTest.OrthogonalInterferenceDataRaceDetection`
**Test Intent**: Verify OrthogonalInterferencePass detects concurrent data races in parallel (AND) states.

**Scenario**:
  - Construct parallel state with RegA and RegB concurrently modifying `battery_level`.
  - Verify pass emits a SafetyCritical diagnostic (W_CONCURRENT_DATA_RACE).

#### `MiddleendPassesTest.DeterminismEnforcementAndPriorityOrdering`
**Test Intent**: Verify DeterminismEnforcementPass canonical priority sorting and collision detection.

**Scenario**:
  - Define multiple transitions from Idle for StartCmd with priorities 2 and 1.
  - Verify pass sorts priority 1 before priority 2 in the transition table.

#### `MiddleendPassesTest.SubmachineInliningSplicing`
**Test Intent**: Verify SubmachineInliningPass graph splicing and entry port remapping.

**Scenario**:
  - Host FSM references submachine `ProtocolFSM`.
  - Provide submachine model to SubmachineInliningPass resolver.
  - Verify submachine states and transitions are seamlessly spliced into the parent graph.

#### `MiddleendPassesTest.DeadStateAndTransitionPruning`
**Test Intent**: Verify DeadStatePruningPass removes unreachable states and dead transitions.

**Scenario**:
  - FSM has reachable path Init -> Active.
  - FSM has unreachable Island state and a transition with guard == "false".
  - Verify pass prunes Island and the false transition from the IR.

#### `MiddleendPassesTest.ChoiceInliningBranchFlattening`
**Test Intent**: Verify ChoiceInliningPass flattens choice pseudostates into direct composite transitions.

**Scenario**:
  - FSM has state Idle, Choice node evaluate_health, targets Nominal and Degraded.
  - Idle -> evaluate_health (event StartCmd, action InitSubsystem).
  - evaluate_health -> Nominal (guard BatteryOk, action EnablePower).
  - evaluate_health -> Degraded (guard else, action LogError).
  - Verify pass flattens into 2 direct transitions (Idle -> Nominal, Idle -> Degraded) with combined actions.

#### `MiddleendPassesTest.DeterminismEnforcementUnconditionalCollision`
**Test Intent**: Verify determinism enforcement detects non-deterministic collisions on identical-priority branches.

#### `MiddleendPassesTest.EfsmIntervalAnalysisContractVerification`
**Test Intent**: Verify EFSM interval analysis validates port domain bounds and detects contract violations.

**Scenario**:
  - Define InPort `sensor_val` in [0, 100] and OutPort `actuator_cmd` in [0, 200].
  - Run interval analysis.
  - Verify no errors on compliant models.

#### `MiddleendPassesTest.EfsmIntervalAnalysisOutOfRangePortAssignment`
**Test Intent**: Verify EFSM interval analyzer detects out-of-range assignments violating OutPort contracts.

**Scenario**:
  - Define OutPort `heater_power` with range [0.0, 100.0].
  - Add transition with action assigning `heater_power = 150.0f`.
  - Verify analyzer emits W_PORT_RANGE_VIOLATION diagnostic.

#### `MiddleendPassesTest.EfsmIntervalAnalysisUnsatisfiableGuardDetection`
**Test Intent**: Verify EFSM interval analyzer detects unsatisfiable guards over bounded InPorts.

**Scenario**:
  - Define InPort `sensor_temp` bounded to [-50.0, 50.0].
  - Transition has guard `in.sensor_temp > 90.0f`.
  - Verify analyzer detects unsatisfiable guard and reports diagnostic.

### [`test_pass_manager.cpp`](../tests/middleend/test_pass_manager.cpp) (`tests/middleend/test_pass_manager.cpp`)
#### `PassManagerTest.RunDefaultPipeline`
**Test Intent**: Verify PassManager default optimization and analysis pipeline execution.

**Scenario**:
  - Run default optimization pipeline over an FSM containing an unreachable trap state and choice without fallback.
  - Verify passes collect execution statistics and emit appropriate diagnostic warnings (W0201, W0103).

#### `PassManagerTest.CustomPassRegistration`
**Test Intent**: Verify custom pass registration and extension in PassManager.

**Scenario**:
  - Create a custom `IPass` subclass (`CustomInstrumentationPass`).
  - Register it on PassManager and run pipeline over FSM.
  - Verify state metadata modification and pass statistics recording.

---

## C++ Backend Codegen Subsystem

### [`test_cpp17_standalone.cpp`](../tests/backend/cpp/test_cpp17_standalone.cpp) (`tests/backend/cpp/test_cpp17_standalone.cpp`)
- *(Executable binary test verification)*

### [`test_cpp20_standalone.cpp`](../tests/backend/cpp/test_cpp20_standalone.cpp) (`tests/backend/cpp/test_cpp20_standalone.cpp`)
- *(Executable binary test verification)*

### [`test_cpp_e2e_compiler.cpp`](../tests/backend/cpp/test_cpp_e2e_compiler.cpp) (`tests/backend/cpp/test_cpp_e2e_compiler.cpp`)
#### `CppE2ECompilerTest.StandaloneCompilationAndExecutionCpp17AndCpp20`
**Test Intent**: Verify host compiler compilation and runtime execution of standalone generated C++17 and C++20 code.

**Scenario**:
  - Generate standalone C++17 and C++20 headers for IndustrialThermostat EFSM.
  - Compile both standalone headers with g++ under -Wall -Wextra -Werror -pedantic -Wconversion.
  - Execute compiled binaries asserting synchronous step() control loops, reactive dispatch() with payload,

#### `CppE2ECompilerTest.RuntimeExporterBundlingAndResilience`
**Test Intent**: Verify `RuntimeExporter` bundles standalone runtime headers for C++17 and C++20 and handles IO errors gracefully.

**Scenario**:
  - Export standalone runtime into temporary directories for C++17 and C++20.
  - Verify `fsm.hpp` is created.
  - Attempt to export into an invalid path and verify false return without abnormal termination.

### [`test_cpp_model_emitter.cpp`](../tests/backend/cpp/test_cpp_model_emitter.cpp) (`tests/backend/cpp/test_cpp_model_emitter.cpp`)
#### `CppModelEmitterTest.PartitionedDomainStructuresEmission`
**Test Intent**: Verify C++ emission of partitioned domain structures (InPorts, OutPorts, Registers, Services).

**Scenario**:
  - Build FsmIr with InPorts (with numeric assert constraints), OutPorts, Registers, and external Actions.
  - Emit domain structures using CppModelEmitter::emit_domain_structures.
  - Verify that structs with exact member names, types, default initializers, and RPC virtual interfaces are generated.

#### `CppModelEmitterTest.TypedSignalPayloadsWithValidators`
**Test Intent**: Verify C++ emission of strongly-typed signal structs with payload attributes and constexpr validators.

**Scenario**:
  - Define signal `EvTelemetry` with attributes `len`, `ptr` and validation expressions.
  - Emit events using CppModelEmitter::emit_events.
  - Verify explicit constructor generation and `[[nodiscard]] constexpr bool is_valid()` validator implementation.

#### `CppModelEmitterTest.StatesLifecycleHooksAndRequirements`
**Test Intent**: Verify C++ emission of state lifecycle hooks (`on_entry`, `on_exit`), time invariants, and traceability docstrings.

**Scenario**:
  - State has traceability requirements (REQ-SAFE-01, REQ-REALTIME-02), entry/exit actions, and time invariant.
  - Emit states using CppModelEmitter::emit_states.
  - Verify Doxygen comments `/// @satisfies`, `/// @invariant`, and partitioned on_entry/on_exit signatures.

#### `CppModelEmitterTest.TransitionTablePriorityOrdering`
**Test Intent**: Verify C++ emission of transition tables sorted by descending priority.

**Scenario**:
  - Define transitions with priority 100 (high) and priority 1 (low).
  - Emit transition table using CppModelEmitter::emit_transition_table.
  - Verify priority 100 transition appears before priority 1 transition in the generated table.

#### `CppModelEmitterTest.EfsmResolvedGuardGeneration`
**Test Intent**: Verify automated C++ emission of resolved EFSM guard expressions over InPorts and Registers.

**Scenario**:
  - Define guard with cpp_expression "in.soc > 30.0f && !reg.is_faulty".
  - Emit guards with include_stubs = true.
  - Verify generated struct returns the direct expression.

### [`test_generated_fsm.cpp`](../tests/backend/cpp/test_generated_fsm.cpp) (`tests/backend/cpp/test_generated_fsm.cpp`)
- *(Executable binary test verification)*

---

## Integration & Build Subsystem

### [`test_cmake_integration.cpp`](../tests/integration/test_cmake_integration.cpp) (`tests/integration/test_cmake_integration.cpp`)
- *(Executable binary test verification)*

### [`test_playground_presets_roundtrip.cpp`](../tests/integration/test_playground_presets_roundtrip.cpp) (`tests/integration/test_playground_presets_roundtrip.cpp`)
#### `PlaygroundPresetsRoundtripTest.PresetAutonomousUavMissionSysML2`
**Test Intent**: Verify Autonomous UAV Mission SysML v2 preset end-to-end.

#### `PlaygroundPresetsRoundtripTest.PresetIndustrialPressSCXML`
**Test Intent**: Verify Industrial Press SCXML preset end-to-end.

#### `PlaygroundPresetsRoundtripTest.PresetConnectionManagerPlantUML`
**Test Intent**: Verify Connection Manager PlantUML preset end-to-end.

#### `PlaygroundPresetsRoundtripTest.PresetSmartThermostatJSON`
**Test Intent**: Verify Smart Thermostat XState JSON preset end-to-end.

#### `PlaygroundPresetsRoundtripTest.PresetSatelliteMissionCameoXMI`
**Test Intent**: Verify Satellite Mission Cameo XMI preset end-to-end.

#### `PlaygroundPresetsRoundtripTest.PresetAsyncMotorControllerMermaid`
**Test Intent**: Verify Async Motor Controller Mermaid preset end-to-end.

---

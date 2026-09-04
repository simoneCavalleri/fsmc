# Master Test Suite & Behavioral Verification Catalog

> **Note**: This catalog is automatically generated from the in-code `@brief Test Intent` comments across `tests/`.
> To update this file, run: `cmake --build build --target generate_test_catalog` or `python3 scripts/generate_test_catalog.py`.

**Total Documented Subsystems**: 10  
**Total Test Suites & Binaries**: 55  
**Total Documented Test Cases**: 262  

---

## Core Runtime Subsystem

### [`test_async_and_guards.cpp`](../tests/backend/cpp/runtime/test_async_and_guards.cpp) (`tests/backend/cpp/runtime/test_async_and_guards.cpp`)
#### `AsyncAndGuardsTest.GuardRejectionAndAcceptance`
**Test Intent**: Verify guard predicate rejection, acceptance, and status tracking.

**Scenario**:
  - When allow_transition is false, dispatch returns guard_rejected and FSM stays in Initial.
  - When allow_transition is true, dispatch succeeds and transitions to StateGuarded.

#### `AsyncAndGuardsTest.DeferredEventsQueuingAndReplay`
**Test Intent**: Verify runtime deferred event queueing and automated cascade replay.

**Scenario**:
  - Dispatch EvDeferred in StateInitialWithDeferred (queued with status deferred).
  - Dispatch EvUnlock to enter StateGuarded (which accepts EvDeferred) -> triggers automatic replay into

#### `AsyncAndGuardsTest.ThreadSafeFsmPostAsyncAndHandlers`
**Test Intent**: Verify thread_safe_fsm asynchronous futures, callbacks, and failure handlers.

**Scenario**:
  - Post asynchronous events via `post_async()`, `post(evt, callback)`.
  - Verify rejection, deferred, and failure handlers receive notifications.

#### `AsyncAndGuardsTest.WorkerExceptionSafetyAndFuturePropagation`
**Test Intent**: Verify worker thread resilience and future exception propagation.

**Scenario**:
  - Action throws an exception.
  - Verify future.get() throws the propagated exception.
  - Verify worker thread remains alive and processes subsequent events normally.

#### `AsyncAndGuardsTest.ManualEnqueueAndAutoStartPostAsync`
**Test Intent**: Verify manual queue polling mode, auto-starting worker for `post_async()`, and `with_registers`.

**Scenario**:
  - Enqueue events manually and drain with `process_all()`.
  - Verify `post_async()` auto-starts background worker so futures never deadlock.
  - Verify thread-safe mutable and const access to registers via `with_registers()`.

#### `AsyncAndGuardsTest.TransitionInfoExplicitKind`
**Test Intent**: Verify strongly-typed `transition_kind` inspection (external vs internal).

**Scenario**:
  - Construct external and internal transition_info structs.
  - Verify is_external(), is_internal(), and to_string() formatters.

#### `AsyncAndGuardsTest.ExceptionHandlerRegistrationAndLastException`
**Test Intent**: Verify exception handler registration and `last_exception()` querying.

**Scenario**:
  - Register global exception handler on thread_safe_fsm.
  - Post fire-and-forget event that throws.
  - Verify handler captures exception and `last_exception()` returns non-null pointer until cleared.

#### `AsyncAndGuardsTest.ObserverInvokedOutsideLockCanQueryState`
**Test Intent**: Verify observers and handlers are invoked outside mutex to permit concurrent state querying.

**Scenario**:
  - Query current_state_name() from within observer callback.
  - Verify no deadlock occurs and state name matches target.

#### `AsyncAndGuardsTest.SelfStopWorkerFromWorkerThreadDoesNotDeadlock`
**Test Intent**: Verify `stop_worker()` can be safely called from inside worker thread callbacks without self-join

**Scenario**:
  - Inside observer running on worker thread, call `ts_sm.stop_worker()`.
  - Verify worker cleanly terminates without deadlock.

#### `AsyncAndGuardsTest.CascadingEventsDuringShutdownDrained`
**Test Intent**: Verify cascading events posted during shutdown or `process_all()` are completely drained.

**Scenario**:
  - Transitioning to StateB posts EvToC.
  - Verify calling `stop_worker()` or `process_all()` drains both EvToB and cascading EvToC.

#### `AsyncAndGuardsTest.DestructorDrainsAllQueuedTasksSafely`
**Test Intent**: Verify thread_safe_fsm destructor cleanly drains pending tasks before releasing resources.

**Scenario**:
  - Enqueue tasks and let FSM go out of scope.
  - Verify destructor processes all tasks.

#### `AsyncAndGuardsTest.ModularTraitsAndRuntimeHeaders`
**Test Intent**: Verify modular traits headers and direct `async_event_queue` push/pop mechanics.

**Scenario**:
  - Test type_list traits (size, contains).
  - Test direct async_event_queue try_pop and queue size.

#### `AsyncAndGuardsTest.ThreadSafeFsmReentrancyPreventionAndDraining`
**Test Intent**: Verify thread_safe_fsm detects same-thread reentrant dispatch and safely defers/drains it without

### [`test_choice.cpp`](../tests/backend/cpp/runtime/test_choice.cpp) (`tests/backend/cpp/runtime/test_choice.cpp`)
#### `ChoiceTest.ChoicePseudostateParsingAndCodegen`
**Test Intent**: Verify Choice pseudostate expansion and code generation.

**Scenario**:
  - Parse PlantUML containing `state AuthChoice <<choice>>` and conditional outgoing branches.
  - Verify Choice node is captured as a choice_node in the Formal IR.
  - Verify C++ code generator expands the choice into direct guarded rows in the transition table
  - (e.g., row<Idle, LoginCmd, AdminView>::when<IsAdminGuard> and row<Idle, LoginCmd, UserView>::when<IsUserGuard>).

### [`test_composite_guards.cpp`](../tests/backend/cpp/runtime/test_composite_guards.cpp) (`tests/backend/cpp/runtime/test_composite_guards.cpp`)
#### `CompositeGuardsTest.DirectCombinatorsEvaluation`
**Test Intent**: Verify C++ compile-time composite guard combinators (`and_`, `or_`, `not_`).

**Scenario**:
  - Evaluate `not_<IsEmergencyStop>`.
  - Evaluate 3-way conjunction `and_<IsPowerOk, IsDoorClosed, not_<IsEmergencyStop>>`.
  - Evaluate disjunction `or_<IsEmergencyStop, not_<IsTempSafe>>`.
  - Evaluate complex nested combinator: `(PowerOk && DoorClosed) || ManualOverride`.

#### `CompositeGuardsTest.GuardExpressionParserBasicAndNested`
**Test Intent**: Verify AST parsing and operator precedence in GuardExpressionParser.

**Scenario**:
  - Parse atomic guards, negation `!A`, conjunction `A && B`, and disjunction `A || B`.
  - Verify `&&` binds tighter than `||` (`A || B && C` -> `fsm::or_<A, fsm::and_<B, C>>`).
  - Verify parentheses override default precedence (`(A || B) && C` -> `fsm::and_<fsm::or_<A, B>, C>`).
  - Verify 4-level deep nested boolean formulas.

#### `CompositeGuardsTest.GuardExpressionParserEdgeCasesAndFuzzing`
**Test Intent**: Verify whitespace resilience, empty inputs, and roundtrip diagram string formatting.

**Scenario**:
  - Parse expressions with irregular whitespace formatting.
  - Test empty and whitespace-only guard strings.
  - Test roundtrip conversion between C++ template representation and diagram string format.

#### `CompositeGuardsTest.MultiFormatParserCompositeGuards`
**Test Intent**: Verify composite guard expression extraction across all supported diagram parsers.

**Scenario**:
  - Parse composite guard expressions from PlantUML, Mermaid, SysML v2, SCXML, DOT, and JSON.
  - Verify every parser properly decodes entities and compiles the expression into the normalized C++ template type.

#### `CompositeGuardsTest.FsmRuntimeExecutionWithCompositeGuards`
**Test Intent**: Verify end-to-end runtime evaluation of composite guards during event dispatch.

**Scenario**:
  - Define transition table with `fsm::and_<IsPowerOk, IsDoorClosed, fsm::not_<IsEmergencyStop>>`.
  - Test failure with power off, door open, and emergency stop active.
  - Test success when all composite conditions are satisfied, transitioning to Running.

### [`test_context_contract.cpp`](../tests/backend/cpp/runtime/test_context_contract.cpp) (`tests/backend/cpp/runtime/test_context_contract.cpp`)
#### `DomainContractTest.SignalValidatorExecution`
**Test Intent**: Verify runtime and constexpr validation logic on typed signal structs.

#### `DomainContractTest.Cpp20ConceptsValidation`
**Test Intent**: Verify compile-time C++20 concept requirements on user-defined services/ports structs.

#### `DomainContractTest.CompileTimeDomainSafety`
**Test Intent**: Verify compile-time safety and initialization for Registers.

#### `DomainContractTest.ThreadSafeWithRegistersMutation`
**Test Intent**: Verify thread_safe_fsm::with_registers executes callable under internal lock.

#### `DomainContractTest.SnapshotRegistersIsolation`
**Test Intent**: Verify thread_safe_fsm::snapshot_registers is independent from subsequent mutations.

#### `DomainContractTest.ThreadSafeWithRegistersConstReadOnly`
**Test Intent**: Verify const overload of with_registers for read-only access.

### [`test_deep_history_multi_level.cpp`](../tests/backend/cpp/runtime/test_deep_history_multi_level.cpp) (`tests/backend/cpp/runtime/test_deep_history_multi_level.cpp`)
#### `DeepHistoryTest.FourLevelDeepHistoryAstAndCodegen`
**Test Intent**: Verify AST construction and C++ codegen for 4-level deep hierarchical history.

**Scenario**:
  - Parse PlantUML with 4-level nesting (Operating -> SubSystem -> Module -> Level4Active/Calibrating).
  - Verify deep history target flag `Operating[H*]`.
  - Verify code generator emits history guards for deepest leaf substates.

#### `DeepHistoryTest.RuntimeExecutionRestoresDeepLeafState`
**Test Intent**: Verify runtime deep history restoration of deeply nested leaf states.

**Scenario**:
  - Navigate from Standby to Level4Active, then advance to Level4Calibrating.
  - Interrupt with EStopEvent to transition to Emergency state.
  - Dispatch ResumeDeepCmd -> verify runtime FSM restores Level4Calibrating leaf state directly.

#### `DeepHistoryTest.InitialEntryWithoutPriorHistoryFallsBackToDefault`
**Test Intent**: Verify default initial sub-state fallback when entering history with no prior visit.

**Scenario**:
  - Start FSM directly in Emergency state without having visited Operating before.
  - Dispatch ResumeDeepCmd -> verify fallback transition to the default initial leaf (Level4Active).

### [`test_deferred.cpp`](../tests/backend/cpp/runtime/test_deferred.cpp) (`tests/backend/cpp/runtime/test_deferred.cpp`)
#### `DeferredEventsTest.PlantUmlParsing`
**Test Intent**: Verify PlantUML `defer <Event>` directive parsing into state deferred events.

**Scenario**:
  - Parse PlantUML with `Initializing : defer RequestCmd` and `Initializing : defer DataPacket`.
  - Verify IR state contains both deferred event names.

#### `DeferredEventsTest.MermaidParsing`
**Test Intent**: Verify Mermaid `defer <Event>` syntax parsing.

**Scenario**:
  - Parse Mermaid with `Booting : defer UserInput`.
  - Verify Booting state records UserInput in deferred_events.

#### `DeferredEventsTest.CameoParsing`
**Test Intent**: Verify Cameo / MagicDraw XMI deferrableTrigger element parsing.

**Scenario**:
  - Parse OMG XMI containing `<deferrableTrigger name="RequestCmd"/>`.
  - Verify state records RequestCmd in deferred_events list.

#### `DeferredEventsTest.ScxmlParsing`
**Test Intent**: Verify W3C SCXML `<defer event="..."/>` syntax parsing.

**Scenario**:
  - Parse SCXML with `<defer event="RequestCmd"/>` child element inside `<state>`.
  - Verify parsed FsmIr captures the deferred event definition.

#### `DeferredEventsTest.JsonParsing`
**Test Intent**: Verify JSON statechart `"defer": [...]` array parsing.

**Scenario**:
  - Parse XState JSON with `"defer": ["RequestCmd", "DataPacket"]`.
  - Verify both deferred events are captured in IR.

#### `DeferredEventsTest.DotParsing`
**Test Intent**: Verify Graphviz DOT `defer="A, B"` attribute parsing.

**Scenario**:
  - Parse DOT graph with `Initializing [defer="RequestCmd, DataPacket"]`.
  - Verify parsed FsmIr captures both comma-separated deferred events.

#### `DeferredEventsTest.SyncRuntimeCascadeReplay`
**Test Intent**: Verify synchronous runtime cascade replay of deferred events upon state transitions.

**Scenario**:
  - Dispatch RequestCmd and DataPacket while in Initializing state (both must be deferred into queue).
  - Dispatch InitDone: FSM enters Ready, automatically un-defers and processes RequestCmd (moving to Processing),

#### `DeferredEventsTest.AsyncRuntimeExecution`
**Test Intent**: Verify asynchronous multi-threaded deferred event processing.

**Scenario**:
  - Start thread_safe_fsm worker thread.
  - Post deferred events from producer thread.
  - Post trigger event and wait for worker thread to asynchronously cascade replay and reach Completed state.

#### `DeferredEventsTest.ConfigurableDeferredCapacity`
**Test Intent**: Verify configurable DeferredCapacity template parameter across all runtime wrappers.

### [`test_flight_recorder.cpp`](../tests/backend/cpp/runtime/test_flight_recorder.cpp) (`tests/backend/cpp/runtime/test_flight_recorder.cpp`)
#### `FlightRecorderTest.CircularRingBufferPushAndWrap`
**Test Intent**: Verify TraceBuffer circular ring buffer pushes and overwrites without allocations.

**Scenario**:
  - Create TraceBuffer with fixed capacity 4.
  - Push 6 entries.
  - Verify size saturates at 4 and oldest entries are evicted in FIFO order.

#### `FlightRecorderTest.ChronologicalIndexingAndDump`
**Test Intent**: Verify chronological indexing, last_entry retrieval and dump formatting.

**Scenario**:
  - Push entries to TraceBuffer and test last_entry and dump output.

#### `FlightRecorderTest.FlightRecorderObserverRecording`
**Test Intent**: Verify flight_recorder_observer records transitions and tracks ticks.

**Scenario**:
  - Instantiate flight_recorder_observer and dispatch synthetic on_transition calls.
  - Verify recorded items in the inner buffer.

#### `FlightRecorderTest.FsmDeterministicTimerTick`
**Test Intent**: Verify fsm::fsm integrates deterministic timer manager and synchronous tick().

**Scenario**:
  - Instantiate synchronous fsm.
  - Start a timer via timer_manager().
  - Step time with tick(50) and tick(60).
  - Verify timer expiration count.

### [`test_fsm.cpp`](../tests/backend/cpp/runtime/test_fsm.cpp) (`tests/backend/cpp/runtime/test_fsm.cpp`)
#### `FsmCoreTest.BasicTransitionsAndIntrospection`
**Test Intent**: Verify basic synchronous state transitions and compile-time introspection.

**Scenario**:
  - Define a 3-state machine (Idle -> Running -> Stopped -> Idle).
  - Verify compile-time type introspection (state_count, transition_count, has_state, has_event).
  - Dispatch valid events in sequence and verify immediate active state updates.
  - Dispatch unhandled events and verify that the machine remains in the current state with an unhandled result.

#### `FsmCoreTest.HooksExecutionOrderAndPayloads`
**Test Intent**: Verify strict lifecycle hook execution order and event payload forwarding.

**Scenario**:
  - When entering initial state StateA: StateA::on_enter() must be called.
  - When transitioning StateA -> StateB with EventGotoB{"Hello FSM"}:
  - 1. StateA::on_exit() is invoked.
  - 2. CustomAction is executed with the payload.
  - 3. StateB::on_enter(evt) is invoked with payload parameter.

#### `FsmCoreTest.GuardValidation`
**Test Intent**: Verify guard predicate rejection, acceptance, and dispatch result statuses.

**Scenario**:
  - With key != 42: guard returns false, transition is rejected, state remains Locked, status is guard_rejected.
  - With an unhandled event: status is unhandled, state remains Locked.
  - With key == 42: guard returns true, transition succeeds, state becomes Unlocked, status is success.

#### `FsmCoreTest.ThreadSafeQueueManualProcessing`
**Test Intent**: Verify thread_safe_fsm synchronous sending and manual batch processing.

**Scenario**:
  - Call send() synchronously to apply transition immediately under mutex.
  - Call enqueue() to push events into thread-safe queue.
  - Call process_all() to drain and execute queued events deterministically.

#### `FsmCoreTest.ConcurrentMultithreadedWorker`
**Test Intent**: Verify asynchronous background worker thread handling concurrent event posting.

**Scenario**:
  - Start worker thread with start_worker().
  - Launch 10 concurrent producer threads, each posting 100 IncrementEvent events.
  - Wait for worker thread to process all 1000 events.
  - Verify final accumulated state count is exactly 1000 with zero race conditions.

#### `FsmCoreTest.DualChannelMachineDualParadigmAndZeroHeap`
**Test Intent**: Verify dual-mode execution (continuous sampled step + event-driven reactive dispatch) and

#### `FsmCoreTest.NonDefaultConstructibleServicesSupport`
**Test Intent**: Verify fsm supports non-default-constructible Services when bound in constructor.

**Scenario**:
  - Construct an fsm instance passing a non-default-constructible Services object by reference.
  - Call step() and dispatch() overloads that omit the srv parameter.
  - Verify the runtime dereferences the bound services without stack-allocating a dummy instance.

### [`test_hfsm.cpp`](../tests/backend/cpp/runtime/test_hfsm.cpp) (`tests/backend/cpp/runtime/test_hfsm.cpp`)
#### `HfsmTest.PlantUmlCompositeStateParsing`
**Test Intent**: Verify hierarchical state machine (HFSM) parsing from PlantUML syntax.

**Scenario**:
  - Parse PlantUML with nested `state Active { [*] --> Idle ... }` block and top-level transitions.
  - Verify parent-child relationships (Idle and Processing have parent Active).
  - Verify composite state properties (is_composite == true, initial_sub_state == Idle).
  - Verify validation passes with zero errors.

#### `HfsmTest.MermaidCompositeStateParsing`
**Test Intent**: Verify hierarchical composite state machine parsing from Mermaid syntax.

**Scenario**:
  - Parse Mermaid `stateDiagram-v2` with `state Session { [*] --> Connected ... }`.
  - Verify parent-child navigation and initial sub-state assignment for Session.
  - Validate integrity through FsmValidator.

### [`test_history.cpp`](../tests/backend/cpp/runtime/test_history.cpp) (`tests/backend/cpp/runtime/test_history.cpp`)
#### `HistoryTest.PlantUmlHistoryTargetParsing`
**Test Intent**: Verify PlantUML shallow history pseudo-state syntax parsing (`Operating[H]`).

**Scenario**:
  - Parse PlantUML with `Paused --> Operating[H] : Resume`.
  - Verify target state is flagged with has_history == true and transition is target_is_history.

#### `HistoryTest.MermaidDeepHistoryTargetParsing`
**Test Intent**: Verify Mermaid deep history pseudo-state syntax parsing (`Operating[H*]`).

**Scenario**:
  - Parse Mermaid diagram with `Suspended --> Operating[H*] : Recover`.
  - Verify target composite state is flagged with has_deep_history == true.

#### `HistoryTest.HistoryCodegenExpansion`
**Test Intent**: Verify code generation of history guards and sub-state parent metadata.

**Scenario**:
  - Generate C++20 header for FSM with shallow history.
  - Verify generated substates contain `parent = "Operating"`.
  - Verify transition table contains conditional rows guarded by `fsm::history_is<Operating, StepX>`.

#### `HistoryTest.RuntimeHistoryRestoresLastVisitedSubstate`
**Test Intent**: Verify runtime history recording and exact restoration of the last active sub-state.

**Scenario**:
  - Enter composite state Operating (sub-state Step1), advance to Step2.
  - Dispatch Pause event to exit Operating -> Paused (fsm records Operating history as Step2).
  - Dispatch Resume event to transition to Operating[H] -> verifies Step2 is restored.
  - Advance to Step3, Pause, and Resume -> verifies Step3 is restored.

#### `HistoryTest.BoundedHistoryStorageCapacity`
**Test Intent**: Verify bounded compile-time max_history_capacity based on states with parent attribute.

### [`test_internal_transition.cpp`](../tests/backend/cpp/runtime/test_internal_transition.cpp) (`tests/backend/cpp/runtime/test_internal_transition.cpp`)
#### `InternalTransitionTest.RuntimeInternalTransitionExecutesActionWithoutEntryExit`
**Test Intent**: Verify internal transitions execute actions without triggering state entry or exit hooks.

**Scenario**:
  - Enter initial ActiveState (on_enter hook runs).
  - Dispatch internal transition event (PingEvent).
  - Verify only the action executes, while on_exit and on_enter hooks are completely bypassed.

#### `InternalTransitionTest.ParserInternalTransitionAndCodegen`
**Test Intent**: Verify parser recognition of internal transitions and code generation to `fsm::internal_row`.

**Scenario**:
  - Parse PlantUML syntax `Idle : Ping / ResetWatchdog`.
  - Verify transition is recorded with TransitionEdgeKind::Internal.
  - Verify C++ generator outputs `fsm::internal_row<Idle, Ping>::then<ResetWatchdog>`.

### [`test_observer.cpp`](../tests/backend/cpp/runtime/test_observer.cpp) (`tests/backend/cpp/runtime/test_observer.cpp`)
#### `ObserverTest.SyncFsmObserverHooks`
**Test Intent**: Verify synchronous observer callbacks receive comprehensive transition metadata.

**Scenario**:
  - Register observer callback receiving `fsm::transition_info`.
  - Dispatch external transitions, internal transitions, and unhandled events.
  - Verify observer receives correct source, target, event name, transition kind (external/internal),

#### `ObserverTest.ThreadSafeFsmObserverHooks`
**Test Intent**: Verify thread_safe_fsm observer firing asynchronously on background worker thread.

**Scenario**:
  - Register observer callback protected by mutex.
  - Post 5 events into async queue.
  - Wait for worker thread to process queue and verify all 5 transition events were recorded safely.

#### `ObserverTest.ThreadSafeFsmPostAsyncAndUnhandledHandler`
**Test Intent**: Verify `post_async()` returning `std::future<dispatch_result>` and unhandled handlers.

**Scenario**:
  - Call `post_async()` and block on `future.get()` for both valid and unhandled events.
  - Verify unhandled handler is invoked on invalid events.

#### `ObserverTest.ReentrantSendInsideObserverDeadlockFree`
**Test Intent**: Verify reentrant `send()` calls from inside observer callbacks are deadlock-free.

**Scenario**:
  - Register observer callback that immediately issues another `send()` event synchronously.
  - Verify recursive/reentrant lock acquisition completes without deadlock.

### [`test_policy_config.cpp`](../tests/backend/cpp/runtime/test_policy_config.cpp) (`tests/backend/cpp/runtime/test_policy_config.cpp`)
#### `PolicyConfigTest.DefaultPolicyExtraction`
**Test Intent**: Verify default policy extraction in fsm::config.

**Scenario**:
  - Instantiate fsm::config<PolicyTestTable> with no modifier policies.
  - Verify all domain interfaces resolve to default no_* types and capacities.

#### `PolicyConfigTest.ArbitraryOrderPolicyExtraction`
**Test Intent**: Verify custom policy extraction in arbitrary order.

**Scenario**:
  - Instantiate fsm::config with with_registers, with_ports, with_services, and with_queue_capacity.
  - Verify policies are correctly mapped regardless of specification order.

#### `PolicyConfigTest.MakeFsmExecution`
**Test Intent**: Verify instantiation and execution of fsm::make_fsm.

**Scenario**:
  - Instantiate synchronous FSM via fsm::make_fsm<PolicyTestTable, with_registers<DummyRegisters>>.
  - Verify state transitions and register manipulation.

#### `PolicyConfigTest.MakeSpscFsmExecution`
**Test Intent**: Verify instantiation and lock-free execution of fsm::make_spsc_fsm.

**Scenario**:
  - Instantiate spsc_fsm via fsm::make_spsc_fsm with with_registers and with_queue_capacity.
  - Post events, process transitions, and verify seqlock snapshot.

#### `PolicyConfigTest.MakeThreadSafeFsmSafeByDesign`
**Test Intent**: Verify instantiation and safe-by-design access of fsm::make_thread_safe_fsm.

**Scenario**:
  - Instantiate thread_safe_fsm via fsm::make_thread_safe_fsm.
  - Verify with_registers and snapshot_registers without uncoordinated naked references.

### [`test_ring_buffer_overflow.cpp`](../tests/backend/cpp/runtime/test_ring_buffer_overflow.cpp) (`tests/backend/cpp/runtime/test_ring_buffer_overflow.cpp`)
- *(Executable binary test verification)*

### [`test_spsc_fsm.cpp`](../tests/backend/cpp/runtime/test_spsc_fsm.cpp) (`tests/backend/cpp/runtime/test_spsc_fsm.cpp`)
#### `SpscFsmTest.CompileTimeIntrospection`
**Test Intent**: Verify compile-time introspection on spsc_fsm.

#### `SpscFsmTest.BasicProducerConsumerExecution`
**Test Intent**: Verify basic SPSC execution across distinct producer and consumer threads.

#### `SpscFsmTest.ConcurrentLockFreeReads`
**Test Intent**: Verify lock-free concurrent reads while consumer executes transitions.

#### `SpscFsmTest.SpscFsmTriviallyCopyableConstraint`
**Test Intent**: Verify compile-time validation of trivially copyable registers for spsc_fsm.

**Scenario**:
  - Verify std::is_trivially_copyable_v is true for SampleRegisters and no_registers.
  - Demonstrate compile-time compatibility with spsc_fsm.

### [`test_spsc_queue.cpp`](../tests/backend/cpp/runtime/test_spsc_queue.cpp) (`tests/backend/cpp/runtime/test_spsc_queue.cpp`)
#### `SpscRingBufferTest.SingleThreadBasicOps`
**Test Intent**: Verify single-threaded SPSC ring buffer FIFO semantics and capacity boundaries.

**Scenario**:
  - Push items until capacity is reached and verify queue reports full.
  - Attempt to push beyond capacity and verify rejection.
  - Pop all items and verify exact FIFO order and empty queue status.

#### `SpscRingBufferTest.MultiThreadedConcurrentStress`
**Test Intent**: Stress-test SPSC ring buffer under high-throughput concurrent multi-threading.

**Scenario**:
  - One producer thread continuously pushes 100,000 sequenced integers.
  - One consumer thread continuously pops items into a consumed collection.
  - Verify all 100,000 items are received in exact sequential order without data races or dropped elements.

#### `SpscRingBufferTest.NonTrivialObjectLifecyclesAndEmplace`
**Test Intent**: Verify exact constructor and destructor lifecycle management for non-trivial objects.

**Scenario**:
  - Emplace objects with multi-argument constructors into ring buffer.
  - Pop objects and verify live instance count updates with exact 1-to-1 parity.
  - Destroy the ring buffer and verify remaining slotted elements are cleanly destroyed with 0 leaks.

#### `SpscRingBufferTest.SpscRingBufferByteStorageAndDefaultConstructible`
**Test Intent**: Verify std::byte aligned storage and default constructibility static assertion.

#### `SpscRingBufferTest.NonDefaultConstructiblePayload`
**Test Intent**: Verify SPSC ring buffer supports non-default-constructible payload types.

**Scenario**:
  - Emplace instances of NonDefaultType into ring buffer.
  - Pop values and verify content preservation.
  - Verify in-place destructor clean-up without default construction requirements.

### [`test_thread_safe_stress.cpp`](../tests/backend/cpp/runtime/test_thread_safe_stress.cpp) (`tests/backend/cpp/runtime/test_thread_safe_stress.cpp`)
#### `ThreadSafeStressTest.HighConcurrency20Threads50kEvents`
**Test Intent**: Stress-test thread_safe_fsm under intense 20-thread concurrency (50,000 total events).

**Scenario**:
  - Launch 20 concurrent producer threads, each posting 2,500 mixed external and internal events.
  - Concurrently run a consumer thread executing `process_all()`.
  - Verify no deadlocks, segmentation faults, or lost events occur during high-contention locking.

#### `ThreadSafeStressTest.ConcurrentTimedAndImmediateEvents`
**Test Intent**: Verify thread-safe concurrency mixing immediate posts and delayed timed transitions.

**Scenario**:
  - Launch 8 threads simultaneously issuing immediate posts and delayed deadline posts.
  - Wait for timed events to expire and drain.
  - Verify all events are recorded without race conditions.

### [`test_timed_transitions.cpp`](../tests/backend/cpp/runtime/test_timed_transitions.cpp) (`tests/backend/cpp/runtime/test_timed_transitions.cpp`)
#### `TimedTransitionsTest.SyncTimedEventDispatch`
**Test Intent**: Verify synchronous dispatch of compile-time duration timed events (`fsm::after_ms<500>`).

**Scenario**:
  - Define transition table with `Timeout500ms`.
  - Dispatch timed event directly and verify transition from Connecting to Disconnected.

#### `TimedTransitionsTest.AsyncPostDelayedPriorityChronologicalOrder`
**Test Intent**: Verify chronological priority deadline scheduling with `post_delayed()`.

**Scenario**:
  - Post Step3 (60ms delay), Step2 (30ms delay), and Step1 (5ms delay) in reverse order.
  - Verify priority queue executes events in strict chronological order: Step1 -> Step2 -> Step3.

#### `TimedTransitionsTest.AsyncReentrantActionSelfPost`
**Test Intent**: Verify recursive lock safety when actions self-post events to the asynchronous queue.

**Scenario**:
  - ActionSelfPost is executed on Step1, queries active state, and self-posts Step2 back into the FSM.
  - Verify no deadlocks or mutex violations occur, reaching StateC smoothly.

#### `TimedTransitionsTest.SampledDiscreteInStateResidenceGuard`
**Test Intent**: Verify discrete sampled time model with in_state_for guard and step_result.

#### `TimedTransitionsTest.StaleTimerCancellationOnStateChange`
**Test Intent**: Verify delayed timed event cancellation upon mid-flight state transitions.

**Scenario**:
  - Post delayed state timeout for StateA -> StateB.
  - Manually trigger an immediate external transition before the timer fires.
  - Verify that when the timer expires, the obsolete callback is safely discarded without effect.

#### `TimedTransitionsTest.UnifiedStepWithDeterministicTick`
**Test Intent**: Verify unified step(dt) advancing deterministic timer manager and observer tick.

#### `TimedTransitionsTest.TickExpiredCallbackAndThreadSafeWrappers`
**Test Intent**: Verify tick(dt, on_expired) callback and thread-safe / SPSC wrappers.

### [`test_traits_and_hooks.cpp`](../tests/backend/cpp/runtime/test_traits_and_hooks.cpp) (`tests/backend/cpp/runtime/test_traits_and_hooks.cpp`)
#### `TraitsAndHooksTest.TypeListAlgorithms`
**Test Intent**: Verify compile-time type list algorithms and transformations.

**Scenario**:
  - Validate size, front element extraction, list concatenation, and element presence (contains).
  - Validate order-preserving deduplication (type_list_unique_t).
  - Validate conversion to std::variant and std::tuple.

#### `TraitsAndHooksTest.ReflectionAndDemangling`
**Test Intent**: Verify compile-time name reflection, parent hierarchy querying, and type demangling.

**Scenario**:
  - Extract names from static member `::name`, member function `.name()`, and fallback type demangling.
  - Extract event names and verify parent hierarchy relationship for nested composite states.

#### `TraitsAndHooksTest.HookSafeInvocations`
**Test Intent**: Verify hook detection and safe dispatch across all valid hook arities.

#### `TraitsAndHooksTest.GuardAndActionMultiArityInvocations`
**Test Intent**: Verify guard and action dispatch with variable argument signatures.

#### `TraitsAndHooksTest.DispatchResultAndObserverPolicies`
**Test Intent**: Verify dispatch_result statuses, boolean cast semantics, and observer detection traits.

**Scenario**:
  - Verify is_success(), is_deferred(), is_guard_rejected(), is_unhandled() statuses.
  - Verify detection of dynamic vs no-op static observers.
  - Verify compile-time detection of history pseudostates and deferred events across type_list.

#### `TraitsAndHooksTest.DispatchResultTransitionTraceInspection`
**Test Intent**: Verify transition_trace struct and trace introspection on dispatch_result.

**Scenario**:
  - Construct dispatch_result with explicit transition_trace.
  - Verify access to source, target, event, guard, action, and transition_kind.
  - Verify is_internal() and is_external() query helpers.

#### `TraitsAndHooksTest.LegacyContextPoisonCheck`
**Test Intent**: Certify at compile-time that legacy monolithic context signatures (guard(Context&),

#### `TraitsAndHooksTest.StateNameStaticResolutionConsistency`
**Test Intent**: Verify static state name resolution and compile-time string reflection.

**Scenario**:
  - Query get_state_name_static for struct with static constexpr std::string_view name.
  - Verify fallback demangled name for struct without explicit name member.
  - Verify target state name is populated in rejected guard dispatch trace.

#### `TraitsAndHooksTest.HistoryIsOverloadAndTypeSafety`
**Test Intent**: Verify history_is guard helper signature and type safety.

**Scenario**:
  - Instantiate fsm::history_is<Parent, Sub> guard.
  - Invoke with multi-channel domain parameters and mock FSM instance.
  - Verify history matches expected active substate.

#### `TraitsAndHooksTest.ConceptAndScalarSanityCompliance`
**Test Intent**: Verify C++20 Concept constraints (fsm::Guard and fsm::Action) and scalar type rejection.

**Scenario**:
  - Prove valid callable functors satisfy fsm::Guard and fsm::Action concepts.
  - Prove default no_guard and no_action sentinel types satisfy concepts.
  - Prove primitive scalar types (int, double) are rejected at compile time.

#### `TraitsAndHooksTest.TypeListIndexOfCompileTimeLookup`
**Test Intent**: Verify type_list_index_of compile-time index computation and termination.

**Scenario**:
  - Query index of first element (should be 0).
  - Query index of intermediate and last elements.
  - Query index of type not present in list (should return static_cast<std::size_t>(-1)).

### [`test_zero_alloc_runtime.cpp`](../tests/backend/cpp/runtime/test_zero_alloc_runtime.cpp) (`tests/backend/cpp/runtime/test_zero_alloc_runtime.cpp`)
#### `ZeroAllocRuntimeTest.StaticRingBufferBasicOps`
**Test Intent**: Verify boundary conditions, peek inspection, and FIFO ordering for static_ring_buffer.

**Scenario**:
  - Push items up to capacity 4.
  - Verify rejection on overflow.
  - Inspect head item via peek() without removing.
  - Pop items and verify exact FIFO order.

#### `ZeroAllocRuntimeTest.TrueCompileTimeZeroOverheadSize`
**Test Intent**: Verify true zero-allocation footprint (sizeof <= 32 bytes) for embedded runtimes.

**Scenario**:
  - Check compile-time machine size with no_observer policy (no heap vectors or std::function objects).
  - Dispatch transitions synchronously and verify state progression.

#### `ZeroAllocRuntimeTest.SpscFsmOperations`
**Test Intent**: Verify spsc_fsm operations with zero dynamic allocations and lock-free SPSC execution.

**Scenario**:
  - Enqueue events into fixed static queue.
  - Process events one-by-one via process_one() and in batch via run_until_empty().
  - Verify state inspection and queue queries.

#### `ZeroAllocRuntimeTest.StaticRingBufferPeekAndClear`
**Test Intent**: Verify mutable peek inspection and buffer clearing for static_ring_buffer.

**Scenario**:
  - Modify head item in place via mutable peek() pointer.
  - Call clear() and verify size becomes 0 and empty() returns true.

#### `ZeroAllocRuntimeTest.SpscFsmQueueOverflowHandling`
**Test Intent**: Verify deterministic queue overflow rejection in spsc_fsm.

**Scenario**:
  - Instantiate static SPSC FSM with capacity 2.
  - Enqueue 2 events until queue_full() is true.
  - Attempt to post 3rd event and verify post() returns false without exceptions or heap allocation.
  - Process one event and verify queue accepts subsequent posts.

#### `ZeroAllocRuntimeTest.StaticVectorOperations`
**Test Intent**: Verify static_vector operations (push, pop, erase, copy, move, bounds).

#### `ZeroAllocRuntimeTest.StaticVectorResourceResetOnEraseAndPopBack`
**Test Intent**: Verify static_vector RAII resource reset on pop_back and erase.

#### `ZeroAllocRuntimeTest.TrueZeroAllocWithHistoryAndDeferredEvents`
**Test Intent**: Verify that FSM with History and Deferred Events operates with 100% Zero-Heap storage.

---

## C++ Backend Codegen Subsystem

### [`test_cpp17_standalone.cpp`](../tests/backend/cpp/test_cpp17_standalone.cpp) (`tests/backend/cpp/test_cpp17_standalone.cpp`)
- *(Executable binary test verification)*

### [`test_cpp20_standalone.cpp`](../tests/backend/cpp/test_cpp20_standalone.cpp) (`tests/backend/cpp/test_cpp20_standalone.cpp`)
- *(Executable binary test verification)*

### [`test_cpp_e2e_compiler.cpp`](../tests/backend/cpp/test_cpp_e2e_compiler.cpp) (`tests/backend/cpp/test_cpp_e2e_compiler.cpp`)
#### `CppE2ECompilerTest.StandaloneCompilationAndExecutionCpp17AndCpp20`
**Test Intent**: Verify host compiler compilation and runtime execution of standalone generated C++17 and C++20

**Scenario**:
  - Generate standalone C++17 and C++20 headers for IndustrialThermostat EFSM.
  - Compile both standalone headers with g++ under -Wall -Wextra -Werror -pedantic -Wconversion.
  - Execute compiled binaries asserting synchronous step() control loops, reactive dispatch() with payload,

#### `CppE2ECompilerTest.RuntimeExporterBundlingAndResilience`
**Test Intent**: Verify `RuntimeExporter` bundles standalone runtime headers for C++17 and C++20 and handles IO

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
**Test Intent**: Verify C++ emission of strongly-typed signal structs with payload attributes and constexpr

**Scenario**:
  - Define signal `EvTelemetry` with attributes `len`, `ptr` and validation expressions.
  - Emit events using CppModelEmitter::emit_events.
  - Verify explicit constructor generation and `[[nodiscard]] constexpr bool is_valid()` validator implementation.

#### `CppModelEmitterTest.StatesLifecycleHooksAndRequirements`
**Test Intent**: Verify C++ emission of state lifecycle hooks (`on_entry`, `on_exit`), time invariants, and

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

#### `CppModelEmitterTest.EnumAndStructDefinitionsEmission`
**Test Intent**: Verify C++ emission of SysML v2 / formal IR Enums and Structs.

**Scenario**:
  - Define EnumDefinition 'FlightMode' with explicit underlying type and literals.
  - Define StructDefinition 'Waypoint' with typed fields and default values.
  - Emit via CppModelEmitter and verify enum class, to_string constexpr, and struct definitions.

### [`test_generated_fsm.cpp`](../tests/backend/cpp/test_generated_fsm.cpp) (`tests/backend/cpp/test_generated_fsm.cpp`)
- *(Executable binary test verification)*

---

## Diagram & Emitter Backend Subsystem

### [`test_diagram_export.cpp`](../tests/backend/diagram/test_diagram_export.cpp) (`tests/backend/diagram/test_diagram_export.cpp`)
#### `FormatExportTest.CameoToMermaidExport`
**Test Intent**: Verify cross-format export from Cameo OMG XMI to Mermaid state diagrams.

**Scenario**:
  - Parse Cameo XMI into FsmIr.
  - Export to Mermaid diagram syntax.
  - Re-parse exported Mermaid string with MermaidParser and verify model equivalence.

#### `FormatExportTest.ScxmlToPlantUmlExport`
**Test Intent**: Verify cross-format export from W3C SCXML to PlantUML state diagrams.

**Scenario**:
  - Parse SCXML into FsmIr.
  - Export to PlantUML syntax.
  - Re-parse exported PlantUML with PlantUmlParser and verify state graph equivalence.

#### `FormatExportTest.Sysml2Export`
**Test Intent**: Verify SysML v2 state definition export serialization.

**Scenario**:
  - Build FsmIr and export to OMG SysML v2 textual notation.
  - Verify `state def`, `entry; then ...`, `first ... accept ... if ... do ... then ...` syntax.

#### `FormatExportTest.IndustrialPressRoundtripAcrossPlantUmlMermaidJson`
**Test Intent**: Verify multi-format roundtrip fidelity for complex hierarchical state machine (PlantUML ->

**Scenario**:
  - Parse deep hierarchical Industrial Press statechart with composite states and history transitions.
  - Export to PlantUML, Mermaid, and JSON.
  - Re-parse all three representations and verify hierarchy, guards, and action retention.

#### `FormatExportTest.EntryExitPointTimeInvariantAndPriorityMultiFormatRoundtrip`
**Test Intent**: Verify multi-format serialization of EntryPoint, ExitPoint, time_invariant, and transition

**Scenario**:
  - Construct FsmIr with EntryPoint, ExitPoint, stay duration / time_invariant, and transition priority.
  - Serialize to PlantUML, SysML v2, and JSON.
  - Re-parse each representation and verify full retention of kinds, invariants, and priorities.

#### `FormatExportTest.SmvFormalModelVerificationExport`
**Test Intent**: Verify nuXmv / SMV formal model serialization with extended variables, prioritized transitions,

**Scenario**:
  - Build FSM with bounded integer variable 'retry_count' (0..5), boolean 'armed', state enum, and transitions with

#### `FormatExportTest.CameoAndScxmlPseudostatesAndOrthogonalExport`
**Test Intent**: Verify Cameo OMG XMI and SCXML export for hierarchical pseudostates (Choice, Deep/Shallow

**Scenario**:
  - Construct hierarchical FSM with parent composite state containing Choice, DeepHistory, EntryPoint, ExitPoint.
  - Export to Cameo OMG XMI 2.1 and SCXML 1.0.
  - Verify presence of proper XML tags, pseudostate kinds, and history semantics.

#### `FormatExportTest.DotGraphvizExport`
**Test Intent**: Verify DOT / Graphviz diagram serialization and syntax integrity.

**Scenario**:
  - Export model to Graphviz DOT format.
  - Verify digraph header, state styling, and transition edges.
  - Re-parse with DotParser to confirm full lossless syntax compatibility.

---

## Formal Model Checking & nuXmv Subsystem

### [`test_formal_roundtrip.cpp`](../tests/backend/formal/test_formal_roundtrip.cpp) (`tests/backend/formal/test_formal_roundtrip.cpp`)
#### `LosslessRoundtripTest.ConnectionManagerPreset`
**Test Intent**: Verify lossless roundtrip serialization across all 7 supported diagram/schema formats.

**Scenario**:
  - Build baseline FsmIr from ConnectionManager model.
  - Serialize to Mermaid, PlantUML, SysML v2, JSON, DOT, SCXML, Cameo XMI.
  - Parse each emitted format back to FsmIr and assert structural equality.

#### `LosslessRoundtripTest.AsyncMotorControllerPreset`
**Test Intent**: Verify lossless multi-format roundtrip for Async Motor Controller preset.

**Scenario**:
  - 5-state motor controller with regenerative braking and overcurrent fault transitions.
  - Verify all 7 format roundtrips preserve state graph topology.

#### `LosslessRoundtripTest.MissionControllerPreset`
**Test Intent**: Verify lossless multi-format roundtrip for Aerospace Mission Controller preset.

**Scenario**:
  - 7-state mission controller with flight phases, abort branches, and panel deployments.
  - Verify roundtrip fidelity across all serializers.

#### `LosslessRoundtripTest.IndustrialPressPreset`
**Test Intent**: Verify lossless multi-format roundtrip for Industrial Press controller.

**Scenario**:
  - 6-state industrial machine with automated and manual controls.
  - Verify all formats preserve transitions, guards, and action bindings.

#### `LosslessRoundtripTest.Sysml2SpacecraftPreset`
**Test Intent**: Verify OMG SysML v2 syntax parsing and lossless 7-format roundtrip.

**Scenario**:
  - Parse SpacecraftController defined in native SysML v2 syntax.
  - Verify roundtrip equality across all format serializers.

#### `LosslessRoundtripTest.DeepHierarchyAndDeferredEvents`
**Test Intent**: Verify nested composite states and deferred event list preservation during multi-format

**Scenario**:
  - Parse 3-level deep hierarchy with deferred events (`defer EvSensor`).
  - Serialize to Mermaid, SysML v2, SCXML and verify nested states and deferred lists are retained.

#### `LosslessRoundtripTest.ShallowAndDeepHistory`
**Test Intent**: Verify shallow `[H]` and deep `[H*]` history pseudostate roundtrip serialization.

**Scenario**:
  - Transitions target `Active[H]` and `Active[H*]`.
  - Verify target_is_history and target_is_deep_history flags are preserved in serializers.

#### `LosslessRoundtripTest.ComplexBooleanGuards`
**Test Intent**: Verify complex compound boolean guard expressions (`&&`, `||`, `!`) across format roundtrips.

**Scenario**:
  - Transitions with guard predicates: `HasTokenGuard && IsAdminGuard && !IsBlacklistedGuard`.
  - Verify expressions survive parsing, serialization, and re-parsing losslessly.

#### `LosslessRoundtripTest.ClosedLoop7HopFormatRing`
**Test Intent**: Verify 7-hop circular conversion ring without data loss (PlantUML -> Mermaid -> SysML2 -> SCXML
-> JSON -> DOT -> PlantUML).

**Scenario**:
  - Serialize through a closed chain of 7 different format representations.
  - Verify the final reconstructed model is identical to the initial one.

#### `LosslessRoundtripTest.NativeLanguageRoundtripAllProperties`
**Test Intent**: Verify lossless preservation of native EFSM variables, signals, requirements, and lifecycle

**Scenario**:
  - Model with state variables, typed signals, traceability reqs, entry/do/exit actions, and deferred events.
  - Test roundtrips to SysML v2, SCXML, JSON, and PlantUML.
  - Verify all metadata attributes remain intact.

#### `LosslessRoundtripTest.TypedPortsAndContractsRoundtrip`
**Test Intent**: Verify lossless roundtrip of Typed In/Out Ports and Numeric Assert Constraints across

#### `LosslessRoundtripTest.AutonomousUavMissionPreset`
**Test Intent**: Verify 100% lossless multi-format roundtrip and traceability requirements for Autonomous UAV

#### `LosslessRoundtripTest.UniversalDataDefinitionsRoundtripAcrossAllFormats`
**Test Intent**: Verify universal lossless roundtrip of enum and struct definitions

---

## Requirements Traceability (RTM) Subsystem

### [`test_rtm_emitter.cpp`](../tests/backend/rtm/test_rtm_emitter.cpp) (`tests/backend/rtm/test_rtm_emitter.cpp`)
#### `RtmEmitterTest.AuditTraceabilityVerification`
**Test Intent**: Verify RtmEmitter::audit_traceability reports untraced states and summary statistics.

**Scenario**:
  - Run audit_traceability on model where some states lack formal traceability tags.
  - Verify diagnostic engine receives audit notifications.

---

## Diagnostic Engine Subsystem

### [`test_diagnostics.cpp`](../tests/diagnostic/test_diagnostics.cpp) (`tests/diagnostic/test_diagnostics.cpp`)
#### `DiagnosticEngineTest.ErrorRenderingWithCaret`
**Test Intent**: Verify diagnostic engine source code rendering with line numbers, caret underlines, and help

**Scenario**:
  - Report a warning diagnostic with a specific SourceSpan (line 2, col 7, length 11) and help suggestion.
  - Verify rendered output contains file location, source code excerpt, caret underline `^~~~~~~~~~~`, and suggestion.

---

## Frontend Parser Subsystem

### [`test_parser_classification.cpp`](../tests/frontend/common/test_parser_classification.cpp) (`tests/frontend/common/test_parser_classification.cpp`)
- *(Executable binary test verification)*

### [`test_parser_factory_and_lexer.cpp`](../tests/frontend/common/test_parser_factory_and_lexer.cpp) (`tests/frontend/common/test_parser_factory_and_lexer.cpp`)
#### `ParserFactoryAndLexerTest.CppKeywordEscaping`
**Test Intent**: Verify C++ reserved keyword detection and escaping utilities.

**Scenario**:
  - Verify standard C++ keywords (class, default, switch, volatile, template) return true from is_cpp_keyword.
  - Verify non-keywords return false.
  - Verify escape_cpp_keyword appends trailing underscore to keywords and preserves user identifiers.

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
**Test Intent**: Verify parsing of typed ports and range constraints in JSON schema.

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

#### `DirectiveParserTest.ParseEnumDirective`
**Test Intent**: Verify `@fsm:enum` directive parsing and roundtrip serialization.

#### `DirectiveParserTest.ParseStructDirective`
**Test Intent**: Verify `@fsm:struct` directive parsing and roundtrip serialization.

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

### [`test_scxml_semantic_completeness.cpp`](../tests/frontend/formal/test_scxml_semantic_completeness.cpp) (`tests/frontend/formal/test_scxml_semantic_completeness.cpp`)
#### `ScxmlSemanticCompletenessTest.ParallelOrthogonalRegions`
**Test Intent**: Verify SCXML parallel element creates StateKind::Parallel and orthogonal regions.

**Scenario**:
  - Ingest SCXML with root parallel and child states representing concurrent orthogonal regions.
  - Verify StateKind::Parallel and populated orthogonal_regions vector in parent state.

#### `ScxmlSemanticCompletenessTest.FinalStatesAndCompletionEvents`
**Test Intent**: Verify SCXML final element creates StateKind::Final state nodes and completion transitions.

**Scenario**:
  - Ingest SCXML with composite state ending in <final id="TaskDone">.
  - Ingest completion transition triggered by done.state.TaskDone.
  - Verify StateKind::Final and transition trigger.

#### `ScxmlSemanticCompletenessTest.SendAndRaiseEventsDispatch`
**Test Intent**: Verify SCXML send and raise elements dispatch internal events and register actions.

**Scenario**:
  - Parse transition containing <raise event="EvInternalAlert"/> and onentry with <send event="EvTelemetrySync"/>.
  - Verify events are registered in FsmIr event model.

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

### [`test_stateflow_parser.cpp`](../tests/frontend/formal/test_stateflow_parser.cpp) (`tests/frontend/formal/test_stateflow_parser.cpp`)
#### `StateflowParserTest.BasicChartIngestion`
**Test Intent**: Verify Stateflow XML model ingestion, state hierarchies, and transition labels.

**Scenario**:
  - Ingest Stateflow XML chart with states and transitions formatted as Event [Guard] / { Action }.
  - Verify parsed state machine hierarchy, triggers, guards, and actions.

#### `StateflowParserTest.TemporalLogicTriggers`
**Test Intent**: Verify Stateflow temporal logic syntax after(N, sec) and after(N, msec).

**Scenario**:
  - Parse transition with labelString="after(500, msec)".
  - Parse transition with labelString="after(2, sec)".
  - Verify synthesized TimeTrigger structures.

#### `StateflowParserTest.ParserFactoryIntegration`
**Test Intent**: Verify ParserFactory auto-detection and registration for Stateflow models.

**Scenario**:
  - Query ParserFactory by format name "stateflow".
  - Query ParserFactory by extension ".sfx" and ".stateflow".
  - Query ParserFactory detect_format_from_content on XML containing <Stateflow>.

#### `StateflowParserTest.SerializerRoundtripWithDirectives`
**Test Intent**: Verify StateflowSerializer XML generation, EmitterFactory registration, and full lossless parsing.

**Scenario**:
  - Construct FsmIr with hierarchy, parallel state, history junction, do_activity, directives (var, port, enum, struct).
  - Serialize to Stateflow XML using StateflowSerializer and EmitterFactory.
  - Parse back with StateflowParser and verify all structural and metadata elements.

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

### [`test_sysml2_structured_data.cpp`](../tests/frontend/formal/test_sysml2_structured_data.cpp) (`tests/frontend/formal/test_sysml2_structured_data.cpp`)
#### `Sysml2StructuredDataTest.EnumDefinitionsIngestion`
**Test Intent**: Verify SysML v2 ingestion of user-defined enum definitions and literals.

**Scenario**:
  - Parse SysML v2 source containing enum def with default and custom underlying types.
  - Verify parsed EnumDefinition models in FsmIr (names, literals, values).

#### `Sysml2StructuredDataTest.StructAndDatatypeDefinitionsIngestion`
**Test Intent**: Verify SysML v2 ingestion of struct def and datatype def with physical units and initializers.

**Scenario**:
  - Parse struct def with attribute types, units, and default values.
  - Parse datatype def and verify is_datatype flag is set.

#### `Sysml2StructuredDataTest.NativeTemporalTriggersIngestion`
**Test Intent**: Verify native temporal triggers accept after(duration) and accept at(time) in SysML v2.

**Scenario**:
  - Parse transitions with accept after 500 [ms], after(2.5 [s]), and accept at(12:00).
  - Verify synthesized TimeTrigger objects in FsmIr transition edges.

#### `Sysml2StructuredDataTest.ConnectionPseudostatesIngestion`
**Test Intent**: Verify fork, join, entry point, and exit point pseudostates in SysML v2.

**Scenario**:
  - Parse SysML v2 containing fork, join, entry point, and exit point pseudostates.
  - Verify StateKind attributes in FsmIr state nodes.

#### `Sysml2StructuredDataTest.Sysml2StructuredDataRoundtrip`
**Test Intent**: Verify lossless SysML v2 serialization and re-ingestion roundtrip for enums and structs.

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
**Test Intent**: Verify domain-separated PortDefinition, SignalDefinition, VariableDefinition and zero Context

---

## Middle-End Verification & Transformation Subsystem

### [`test_guard_satisfiability.cpp`](../tests/middleend/analysis/test_guard_satisfiability.cpp) (`tests/middleend/analysis/test_guard_satisfiability.cpp`)
#### `GuardSatisfiabilityTest.MutuallyExclusiveNumericGuardsNoWarning`
**Test Intent**: Verify that provably disjoint numeric guard intervals emit no warnings.

**Scenario**:
  - Define two transitions on the same source state and event with guards 'x > 50' and 'x <= 30'.
  - Run GuardSatisfiabilityPass and verify that diag.has_warnings() is false.

#### `GuardSatisfiabilityTest.OverlappingGuardsEmitWarningW0301`
**Test Intent**: Verify that overlapping guard intervals on the same event and priority emit warning W0301.

**Scenario**:
  - Define two transitions with guards 'x > 10' and 'x > 20' sharing identical priority 1.
  - Run GuardSatisfiabilityPass and verify that diagnostic code W0301 is emitted.

#### `GuardSatisfiabilityTest.DeadGuardEmitWarningW0302`
**Test Intent**: Verify that contradictory guard conditions (e.g. x > 100 && x < 50) emit dead guard warning

**Scenario**:
  - Define a transition with guard 'x > 100 && x < 50' whose interval intersection is empty.
  - Run GuardSatisfiabilityPass and verify that diagnostic code W0302 is emitted.

#### `GuardSatisfiabilityTest.DifferentPrioritiesAvoidW0301`
**Test Intent**: Verify that overlapping guards with differentiated transition priorities do not emit W0301.

**Scenario**:
  - Define two overlapping guards ('x > 10' and 'x > 20') with distinct priorities (priority 1 vs priority 2).
  - Run GuardSatisfiabilityPass and verify that no ambiguity warning is emitted.

#### `GuardSatisfiabilityTest.BooleanGuardsMutuallyExclusive`
**Test Intent**: Verify that complementary boolean guards (enabled == true vs enabled == false) are recognized as

**Scenario**:
  - Define two transitions on event 'Toggle' with boolean guards 'enabled == true' and 'enabled == false'.
  - Run GuardSatisfiabilityPass and verify that diag.has_warnings() is false.

#### `GuardSatisfiabilityTest.FastZeroAllocationIntervalParsing`
**Test Intent**: Verify zero-allocation parse_guard_domain with qualifiers and numeric formats.

**Scenario**:
  - Test parse_guard_domain with qualifiers (in., reg.) and relational operators (>=, <, ==).
  - Verify interval boundaries are parsed accurately without throwing.

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
**Test Intent**: Verify determinism enforcement detects non-deterministic collisions on identical-priority

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

### [`test_middleend_v060_passes.cpp`](../tests/middleend/passes/test_middleend_v060_passes.cpp) (`tests/middleend/passes/test_middleend_v060_passes.cpp`)
#### `MiddleEndV060PassesTest.OrthogonalProductCartesianExpansion`
**Test Intent**: Verify OrthogonalProductPass expands concurrent regions into Cartesian product states.

**Scenario**:
  - Create a Parallel state with 2 orthogonal regions:
  - Region 1: states A1, A2 with transition A1 -> A2 on EvA.
  - Region 2: states B1, B2 with transition B1 -> B2 on EvB.
  - Execute OrthogonalProductPass.
  - Verify the parent is transformed into StateKind::Composite with 4 product states:

#### `MiddleEndV060PassesTest.WcetAnalysisZenoCycleDetection`
**Test Intent**: Verify WcetAnalysisPass detects infinite zero-time Zeno cycles.

**Scenario**:
  - Create cyclic eventless transitions between states LoopA and LoopB.
  - Execute WcetAnalysisPass.
  - Verify pass returns false and reports diagnostic error E_ZENO_CYCLE.

#### `MiddleEndV060PassesTest.WcetAnalysisBoundedMicroSteps`
**Test Intent**: Verify WcetAnalysisPass computes bounded micro-steps for terminating chains.

**Scenario**:
  - Create a linear sequence of eventless transitions Step1 -> Step2 -> Step3 -> Quiescent.
  - Execute WcetAnalysisPass.
  - Verify pass passes without error and calculates max micro-steps equal to 3.

#### `MiddleEndV060PassesTest.ConstantFoldingGuardEvaluation`
**Test Intent**: Verify ConstantFoldingPass folds tautological guards and eliminates false transitions.

**Scenario**:
  - Create transition T1 with guard "1 == 1" (tautology).
  - Create transition T2 with guard "0 == 1" (contradiction).
  - Create transition T3 with guard "5 > 2" (tautology).
  - Execute ConstantFoldingPass.
  - Verify T1 and T3 have guards stripped (unconditional), and T2 is eliminated from IR.

#### `MiddleEndV060PassesTest.StateMinimizationEquivalencePartitioning`
**Test Intent**: Verify StateMinimizationPass merges behaviorally equivalent states.

**Scenario**:
  - Create states StateA, StateB1, StateB2.
  - Both StateB1 and StateB2 transition to StateA on EvReset with same action and guard,

#### `MiddleEndV060PassesTest.PipeThroughUnixFilter`
**Test Intent**: Verify PipeThroughPass filters IR faithfully through an external Unix command.

**Scenario**:
  - Pipe IR through the standard POSIX utility 'cat'.
  - Verify IR roundtrips with preserved states, transitions and metadata.

#### `MiddleEndV060PassesTest.PluginLoaderErrorHandling`
**Test Intent**: Verify PluginLoader handles non-existent or invalid plugin files gracefully.

**Scenario**:
  - Attempt to load a non-existent shared library.
  - Verify load_plugin returns false and logs diagnostic error E_PLUGIN_LOAD.

### [`test_data_definitions_ir.cpp`](../tests/middleend/test_data_definitions_ir.cpp) (`tests/middleend/test_data_definitions_ir.cpp`)
#### `DataDefinitionsIrTest.EnumDefinitionAndLiterals`
**Test Intent**: Verify EnumDefinition and EnumLiteral creation, query methods, and value semantics.

**Scenario**:
  - Instantiate EnumLiteral with default and explicit numeric values.
  - Create EnumDefinition with custom underlying integer type and literals.
  - Verify lookup methods has_literal, find_literal, find_literal_mut, and add_literal.
  - Validate structural equality via operator==.

#### `DataDefinitionsIrTest.StructDefinitionAndFields`
**Test Intent**: Verify StructDefinition and StructField attributes, ISQ units, domain contracts, and equality.

**Scenario**:
  - Instantiate StructField with type, default initializer, ISQ physical units, and min/max contracts.
  - Create StructDefinition for both composite structs and value datatypes (is_datatype).
  - Verify field lookup, mutation, and structural equality via operator==.

#### `DataDefinitionsIrTest.FsmIrIntegration`
**Test Intent**: Verify FsmIr metamodel container integration, lookup methods, and canonical sorting.

**Scenario**:
  - Register multiple enums and structs inside FsmIr.
  - Validate find_enum, find_enum_mut, find_struct, and find_struct_mut.
  - Call canonicalize() and verify deterministic alphabetical sorting for enums and structs.
  - Verify FsmIr::operator== includes enums and structs in equality checks.

#### `DataDefinitionsIrTest.FsmIrJsonSerializationRoundtrip`
**Test Intent**: Verify lossless JSON IR serialization for user-defined enums and struct definitions.

**Scenario**:
  - Build FsmIr with complete enum and struct definitions including contracts, units, and descriptions.
  - Serialize to JSON using FsmIrSerializer::serialize_json.
  - Verify presence and structure of "enums" and "structs" JSON arrays.

#### `DataDefinitionsIrTest.DiagramJsonSerializerExport`
**Test Intent**: Verify diagram JsonSerializer emission of enums and structs for Studio Playground export.

**Scenario**:
  - Serialize FsmIr with enums and structs using JsonSerializer::serialize.
  - Verify enums and structs sections are rendered properly in diagram JSON.

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

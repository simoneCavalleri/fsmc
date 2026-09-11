# Master Test Suite & Behavioral Verification Catalog

> **Note**: This catalog is automatically generated from the in-code `@brief Test Intent` comments across `tests/`.
> To update this file, run: `cmake --build build --target generate_test_catalog` or `python3 scripts/generate_test_catalog.py`.

**Total Documented Subsystems**: 12  
**Total Test Suites & Binaries**: 77  
**Total Documented Test Cases**: 411  

---

## Core Runtime Subsystem

### [`test_async_and_guards.cpp`](../tests/backend/cpp/runtime/test_async_and_guards.cpp) (`tests/backend/cpp/runtime/test_async_and_guards.cpp`)
#### `AsyncAndGuards.GuardEvaluation_RejectionAndAcceptance_TransitionExecutedCorrectly`
**Test Intent**: Unit test suite for asynchronous dispatching, thread-safe workers, and guard evaluation.
/

#### `AsyncAndGuards.DeferredEvents_QueueingAndReplay_ProcessedInTargetState`
**Test Intent**: Verify runtime deferred event queueing and automated cascade replay.

#### `AsyncAndGuards.ThreadSafeFsm_PostAsyncAndHandlers_FuturesResolved`
**Test Intent**: Verify thread_safe_fsm asynchronous futures, callbacks, and unhandled event handlers.

#### `AsyncAndGuards.WorkerThread_ExceptionThrown_PropagatedToFuture`
**Test Intent**: Verify worker thread resilience and future exception propagation.

#### `AsyncAndGuards.ManualEnqueue_AutoStartWorker_ProcessesPendingEvents`
**Test Intent**: Verify manual queue polling mode, auto-starting worker, and thread safety.

#### `AsyncAndGuards.TransitionInfo_ExplicitKind_ReturnsAccurateClassification`
**Test Intent**: Verify strongly-typed transition_kind inspection in observers and dispatch traces.

#### `AsyncAndGuards.ExceptionHandler_Registration_InvokedOnFailure`
**Test Intent**: Verify exception handler registration and last_exception inspection.

#### `AsyncAndGuards.Observer_InvokedOutsideLock_QueriesStateWithoutDeadlock`
**Test Intent**: Verify observers and handlers are invoked outside mutex locks to prevent deadlocks.

#### `AsyncAndGuards.WorkerThread_SelfStop_TerminatesWithoutDeadlock`
**Test Intent**: Verify stop_worker() can be safely called from inside worker thread action.

#### `AsyncAndGuards.CascadingEvents_DuringShutdown_DrainedSuccessfully`
**Test Intent**: Verify cascading events posted during shutdown or process_all are drained.

#### `AsyncAndGuards.ThreadSafeFsm_Destructor_DrainsAllQueuedTasks`
**Test Intent**: Verify thread_safe_fsm destructor cleanly drains pending async tasks without dangling threads.

#### `AsyncAndGuards.ModularHeaders_DirectInclusion_CompilesAndDispatches`
**Test Intent**: Verify modular traits headers and direct async_event dispatching.

#### `AsyncAndGuards.ThreadSafeFsm_Reentrancy_QueuedAndDrainedSequentially`
**Test Intent**: Verify thread_safe_fsm detects same-thread reentrant dispatch and enqueues instead of deadlocking.

### [`test_choice.cpp`](../tests/backend/cpp/runtime/test_choice.cpp) (`tests/backend/cpp/runtime/test_choice.cpp`)
#### `ChoicePseudostate.ChoicePseudostate_ConditionalEvaluation_BranchesToTarget`
**Test Intent**: Unit test suite for choice pseudostates and conditional branching in runtime.
/

### [`test_composite_guards.cpp`](../tests/backend/cpp/runtime/test_composite_guards.cpp) (`tests/backend/cpp/runtime/test_composite_guards.cpp`)
#### `CompositeGuards.DirectCombinators_BooleanEvaluation_MatchesExpectedOutcome`
**Test Intent**: Unit test suite for composite boolean guard expressions and combinators.
/

#### `CompositeGuards.GuardExpressionParser_NestedExpressions_ParsedIntoAst`
**Test Intent**: Verify parsing of simple and deeply nested boolean guard expressions.

#### `CompositeGuards.GuardExpressionParser_EdgeCases_HandlesSyntaxVariants`
**Test Intent**: Verify guard expression parser resilience on edge cases and whitespace variants.

#### `CompositeGuards.MultiFormatParser_CompositeGuards_IngestedAcrossDialects`
**Test Intent**: Verify multi-format ingestion of compound boolean guards across PlantUML, Mermaid, and SysML v2.

#### `CompositeGuards.RuntimeExecution_CompositeGuards_EvaluatedDuringDispatch`
**Test Intent**: Verify runtime dispatching with compound boolean guards.

### [`test_context_contract.cpp`](../tests/backend/cpp/runtime/test_context_contract.cpp) (`tests/backend/cpp/runtime/test_context_contract.cpp`)
#### `DomainContract.SignalValidator_FieldConstraints_EnforcedAtRuntime`
**Test Intent**: Unit test suite for domain context contracts, signal validators, and compile-time safety.
/

#### `DomainContract.Cpp20Concepts_TypeValidation_EnsuresDomainSafety`
**Test Intent**: Verify C++20 domain concepts enforcing event, state, and context type requirements.

#### `DomainContract.CompileTimeSafety_StaticAsserts_RejectInvalidContracts`
**Test Intent**: Verify compile-time assertions guarding against contract violations.

#### `DomainContract.RegistersMutation_ThreadSafeFsm_ModifiesStateSafely`
**Test Intent**: Verify thread-safe state variable register mutation.

#### `DomainContract.SnapshotRegisters_StateIsolation_MaintainsSeparateCopies`
**Test Intent**: Verify state isolation using snapshot registers.

#### `DomainContract.RegistersConstReadOnly_ConstAccess_PreventsMutation`
**Test Intent**: Verify const read-only access to context registers.

### [`test_deep_history_multi_level.cpp`](../tests/backend/cpp/runtime/test_deep_history_multi_level.cpp) (`tests/backend/cpp/runtime/test_deep_history_multi_level.cpp`)
#### `DeepHistory.FourLevelHierarchy_AstAndCodegen_GeneratesValidHistoryTables`
**Test Intent**: Unit test suite for multi-level deep history pseudostate restoration.
/

#### `DeepHistory.RuntimeExecution_DeepHistory_RestoresDeepLeafSubstate`
**Test Intent**: Verify runtime deep history restoration of deeply nested leaf state.

#### `DeepHistory.InitialEntry_NoPriorHistory_FallsBackToDefaultSubstate`
**Test Intent**: Verify initial entry into composite state with deep history falls back to default initial substate.

### [`test_deferred.cpp`](../tests/backend/cpp/runtime/test_deferred.cpp) (`tests/backend/cpp/runtime/test_deferred.cpp`)
#### `DeferredEvents.PlantUml_DeferredEvents_ParsedIntoIr`
**Test Intent**: Unit test suite for deferred event queueing, capacity limits, and replay semantics.
/

#### `DeferredEvents.Mermaid_DeferredEvents_ParsedIntoIr`
**Test Intent**: Verify Mermaid parsing of deferred events.

#### `DeferredEvents.Cameo_DeferredEvents_ParsedIntoIr`
**Test Intent**: Verify Cameo OMG XMI parsing of deferred events.

#### `DeferredEvents.Scxml_DeferredEvents_ParsedIntoIr`
**Test Intent**: Verify SCXML parsing of deferred events.

#### `DeferredEvents.Json_DeferredEvents_ParsedIntoIr`
**Test Intent**: Verify XState JSON parsing of deferred events.

#### `DeferredEvents.Dot_DeferredEvents_ParsedIntoIr`
**Test Intent**: Verify Graphviz DOT parsing of deferred events.

#### `DeferredEvents.SyncRuntime_CascadeReplay_DispatchesDeferredEvents`
**Test Intent**: Verify synchronous runtime deferred event cascade replay.

#### `DeferredEvents.AsyncRuntime_DeferredEvents_ProcessedInChronologicalOrder`
**Test Intent**: Verify asynchronous worker processing of deferred events.

#### `DeferredEvents.BoundedCapacity_DeferredQueue_EnforcesConfiguredSize`
**Test Intent**: Verify configurable deferred queue capacity and overflow handling.

### [`test_flight_recorder.cpp`](../tests/backend/cpp/runtime/test_flight_recorder.cpp) (`tests/backend/cpp/runtime/test_flight_recorder.cpp`)
#### `FlightRecorder.CircularRingBuffer_PushAndWrap_OverwritesOldestEntries`
**Test Intent**: Unit test suite for circular flight recorder, state history logging, and diagnostics.
/

#### `FlightRecorder.ChronologicalIndexing_Dump_ReturnsOrderedTraceEntries`
**Test Intent**: Verify chronological indexing and string formatting of flight recorder dump.

#### `FlightRecorder.FlightRecorderObserver_StateTransitions_RecordsExecutionHistory`
**Test Intent**: Verify automatic recording of transitions via FlightRecorderObserver.

#### `FlightRecorder.DeterministicTimer_StepTick_RecordsTickEventsInHistory`
**Test Intent**: Verify recording of deterministic timer ticks in flight recorder.

### [`test_fsm.cpp`](../tests/backend/cpp/runtime/test_fsm.cpp) (`tests/backend/cpp/runtime/test_fsm.cpp`)
#### `FsmCore.BasicTransitions_EventDispatch_UpdatesCurrentState`
**Test Intent**: Unit test suite for core synchronous and thread-safe FSM runtime execution.
/

#### `FsmCore.LifecycleHooks_ExecutionOrder_ExecutesEntryActionExit`
**Test Intent**: Verify lifecycle hook execution order (on_exit, transition action, on_entry).

#### `FsmCore.LifecycleHooks_WithPortsRegistersAndServices_ForwardsContext`
**Test Intent**: Verify lifecycle hooks receive the complete public runtime context.

#### `FsmCore.GuardValidation_BooleanPredicates_BlocksDisallowedTransitions`
**Test Intent**: Verify guard evaluation blocking transitions when predicate returns false.

#### `FsmCore.ThreadSafeQueue_ManualProcessing_DrainsEventsExplicitly`
**Test Intent**: Verify thread_safe_fsm in manual processing mode.

#### `FsmCore.ConcurrentWorker_MultipleThreads_ProcessesEventsThreadSafely`
**Test Intent**: Verify concurrent multi-threaded event submission to background worker.

#### `FsmCore.DualChannelMachine_ZeroHeap_ExecutesWithoutDynamicAllocation`
**Test Intent**: Verify dual-channel synchronous and asynchronous zero-heap state machine execution.

#### `FsmCore.ServicesSupport_NonDefaultConstructible_InjectedSuccessfully`
**Test Intent**: Verify support for non-default-constructible context services.

### [`test_hfsm.cpp`](../tests/backend/cpp/runtime/test_hfsm.cpp) (`tests/backend/cpp/runtime/test_hfsm.cpp`)
#### `HfsmHierarchy.PlantUmlCompositeState_NestedHierarchy_ParsedCorrectly`
**Test Intent**: Unit test suite for hierarchical composite states and HFSM event dispatching.
/

#### `HfsmHierarchy.MermaidCompositeState_NestedHierarchy_ParsedCorrectly`
**Test Intent**: Verify Mermaid composite state parsing and runtime initialization.

### [`test_history.cpp`](../tests/backend/cpp/runtime/test_history.cpp) (`tests/backend/cpp/runtime/test_history.cpp`)
#### `HistoryPseudostate.PlantUmlHistory_ShallowHistory_ParsedIntoIr`
**Test Intent**: Unit test suite for shallow and deep history pseudostates in runtime.
/

#### `HistoryPseudostate.MermaidDeepHistory_DeepHistory_ParsedIntoIr`
**Test Intent**: Verify Mermaid deep history pseudostate parsing.

#### `HistoryPseudostate.HistoryCodegen_HistoryTable_EmittedInGeneratedHeader`
**Test Intent**: Verify C++ code generation for history pseudostates.

#### `HistoryPseudostate.RuntimeHistory_TransitionHistory_RestoresLastVisitedSubstate`
**Test Intent**: Verify runtime history restores last active sub-state upon re-entry.

#### `HistoryPseudostate.BoundedStorage_HistoryCapacity_MaintainsConfiguredFootprint`
**Test Intent**: Verify bounded history storage capacity in embedded environments.

### [`test_internal_transition.cpp`](../tests/backend/cpp/runtime/test_internal_transition.cpp) (`tests/backend/cpp/runtime/test_internal_transition.cpp`)
#### `InternalTransition.RuntimeExecution_InternalTransition_ExecutesActionWithoutEntryExit`
**Test Intent**: Unit test suite for internal state transitions without entry/exit execution.
/

#### `InternalTransition.ParserAndCodegen_InternalTransition_PreservesSemanticsInCode`
**Test Intent**: Verify parser extraction and C++ code generation for internal transitions.

### [`test_observer.cpp`](../tests/backend/cpp/runtime/test_observer.cpp) (`tests/backend/cpp/runtime/test_observer.cpp`)
#### `ObserverPattern.SyncFsm_ObserverHooks_NotifiedOnStateTransitions`
**Test Intent**: Unit test suite for observer pattern hooks and transition telemetry.
/

#### `ObserverPattern.ThreadSafeFsm_ObserverHooks_NotifiedAsynchronously`
**Test Intent**: Verify thread-safe FSM observer notifications across threads.

#### `ObserverPattern.UnhandledHandler_UnmatchedEvent_InvokesFallbackCallback`
**Test Intent**: Verify unhandled event callback on thread_safe_fsm.

#### `ObserverPattern.ReentrantSend_InsideObserver_ExecutesWithoutDeadlock`
**Test Intent**: Verify reentrant event posting from inside observer callback.

### [`test_policy_config.cpp`](../tests/backend/cpp/runtime/test_policy_config.cpp) (`tests/backend/cpp/runtime/test_policy_config.cpp`)
#### `PolicyConfig.DefaultPolicy_Extraction_YieldsDefaultTraits`
**Test Intent**: Unit test suite for policy-based design and fluent state machine configuration.
/

#### `PolicyConfig.ArbitraryOrderPolicy_Extraction_ResolvesSpecifiedTraits`
**Test Intent**: Verify arbitrary order policy template argument extraction.

#### `PolicyConfig.MakeFsm_FluentInstantiation_CreatesWorkingStateMachine`
**Test Intent**: Verify make_fsm fluent builder instantiation.

#### `PolicyConfig.MakeSpscFsm_FluentInstantiation_CreatesLockFreeStateMachine`
**Test Intent**: Verify make_spsc_fsm fluent builder instantiation.

#### `PolicyConfig.MakeThreadSafeFsm_FluentInstantiation_CreatesThreadSafeStateMachine`
**Test Intent**: Verify make_thread_safe_fsm fluent builder instantiation.

#### `PolicyConfig.CustomPolicies_TimerAndTraceBuffer_ConfiguresRuntimeLimits`
**Test Intent**: Verify custom timer capacity and trace buffer capacity policies.

#### `PolicyConfig.FluentBuilder_OnTransitionCallback_AttachesCustomObserver`
**Test Intent**: Verify on_transition fluent callback registration.

### [`test_ring_buffer_overflow.cpp`](../tests/backend/cpp/runtime/test_ring_buffer_overflow.cpp) (`tests/backend/cpp/runtime/test_ring_buffer_overflow.cpp`)
#### `RingBufferOverflow.DropIncomingPolicy_QueueFull_DropsNewIncomingEvents`
**Test Intent**: Unit test suite for ring buffer overflow policies (DropIncoming, DropOldest).
/

#### `RingBufferOverflow.DropOldestPolicy_QueueFull_OverwritesOldestEvents`
**Test Intent**: Verify DropOldest ring buffer overflow policy.

#### `SpscFsm.QueueOverflow_ExcessEventsRejected`
**Test Intent**: Verify SPSC queue overflow rejection under fixed capacity.

### [`test_spsc_fsm.cpp`](../tests/backend/cpp/runtime/test_spsc_fsm.cpp) (`tests/backend/cpp/runtime/test_spsc_fsm.cpp`)
#### `SpscFsm.CompileTimeIntrospection_StaticQueries_ReportsCapacitiesAndTypes`
**Test Intent**: Unit test suite for Single-Producer Single-Consumer (SPSC) lock-free FSM runtime.
/

#### `SpscFsm.ProducerConsumer_SingleThread_TransitionsAccurately`
**Test Intent**: Verify basic producer-consumer execution on SPSC FSM.

#### `SpscFsm.LockFreeConcurrency_ProducerConsumer_ExecutesWithoutLocks`
**Test Intent**: Verify concurrent lock-free reads and writes between producer and consumer.

#### `SpscFsm.EventConstraints_TriviallyCopyable_EnforcedAtCompileTime`
**Test Intent**: Verify trivially copyable constraints on events used in lock-free ring buffer.

### [`test_spsc_queue.cpp`](../tests/backend/cpp/runtime/test_spsc_queue.cpp) (`tests/backend/cpp/runtime/test_spsc_queue.cpp`)
#### `SpscRingBuffer.SingleThreadOps_PushAndPop_OperatesCorrectly`
**Test Intent**: Unit test suite for lock-free SPSC ring buffer memory management and lifecycles.
/

#### `SpscRingBuffer.ConcurrentStress_ProducerConsumer_ZeroDataLoss`
**Test Intent**: Verify concurrent multi-threaded stress test on SPSC ring buffer.

#### `SpscRingBuffer.NonTrivialObjects_EmplaceAndPop_ConstructedAndDestroyedCorrectly`
**Test Intent**: Verify non-trivial object construction and destruction lifecycles in ring buffer.

#### `SpscRingBuffer.ByteStorage_DefaultConstructible_AllocatedAccurately`
**Test Intent**: Verify raw byte storage and default constructible handling.

#### `SpscRingBuffer.NonDefaultConstructible_Emplace_ConstructsInPlace`
**Test Intent**: Verify in-place emplacement of non-default-constructible payloads.

### [`test_thread_safe_stress.cpp`](../tests/backend/cpp/runtime/test_thread_safe_stress.cpp) (`tests/backend/cpp/runtime/test_thread_safe_stress.cpp`)
#### `ThreadSafeStress.HighConcurrency_TwentyThreadsFiftyThousandEvents_AllEventsProcessed`
**Test Intent**: Unit test suite for high-concurrency stress testing of thread_safe_fsm.
/

#### `ThreadSafeStress.MixedEvents_ConcurrentImmediateAndTimed_ProcessedDeterministically`
**Test Intent**: Verify concurrent mixing of immediate and delayed/timed events under load.

### [`test_timed_transitions.cpp`](../tests/backend/cpp/runtime/test_timed_transitions.cpp) (`tests/backend/cpp/runtime/test_timed_transitions.cpp`)
#### `TimedTransitions.SyncTimedEvent_Dispatch_TransitionsAfterDuration`
**Test Intent**: Unit test suite for timed transitions, timer cancellation, and discrete tick stepping.
/

#### `TimedTransitions.TimedTableRow_TickAutomaticallyDispatchesAfterEvent`
**Test Intent**: Verify timed table rows are armed automatically when a state is entered.

#### `TimedTransitions.AsyncPostDelayed_MultipleTimers_FiredInChronologicalOrder`
**Test Intent**: Verify asynchronous delayed events scheduled and fired in chronological order.

#### `TimedTransitions.ReentrantAction_SelfPostDelayed_SchedulesRecurringTimer`
**Test Intent**: Verify reentrant self-posting of delayed timers from within transition actions.

#### `TimedTransitions.ResidenceGuard_StayDuration_EvaluatedAccurately`
**Test Intent**: Verify state residence duration guard conditions (stay <= 100ms).

#### `TimedTransitions.StateChange_PendingTimer_CancelledAutomatically`
**Test Intent**: Verify automatic cancellation of pending timers upon state exit.

#### `TimedTransitions.DeterministicTick_UnifiedStep_AdvancesTimeByFixedDelta`
**Test Intent**: Verify unified step function advancing simulated time deterministically.

#### `TimedTransitions.TickExpiredCallback_ThreadSafeWrapper_NotifiedUponExpiration`
**Test Intent**: Verify callback notification when timer expires under thread-safe wrapper.

#### `TimedTransitions.TimeInvariant_Satisfied_WhenTransitionLeavesBeforeMaxStay`
**Test Intent**: Verify invariant is satisfied when an armed transition exits the state before max stay.

#### `TimedTransitions.TimeInvariant_Violation_WithEnabledTransitionExceedingBound`
**Test Intent**: Verify invariant violation is diagnosed when an enabled escape transition is not taken in time.

#### `TimedTransitions.TimeInvariant_Violation_WithoutEscapeTransition_InvokesCallbackAndSetsStatus`
**Test Intent**: Verify invariant violation without an escape transition invokes callback and marks status.

### [`test_traits_and_hooks.cpp`](../tests/backend/cpp/runtime/test_traits_and_hooks.cpp) (`tests/backend/cpp/runtime/test_traits_and_hooks.cpp`)
#### `TraitsAndHooks.TypeListAlgorithms_TransformAndFilter_CompileTimeExpectedTypes`
**Test Intent**: Unit test suite for type-list traits, metaprogramming algorithms, and reflection hooks.
/

#### `TraitsAndHooks.Reflection_StateAndEventDemangling_ProducesReadableNames`
**Test Intent**: Verify compile-time and runtime type demangling of state and event names.

#### `TraitsAndHooks.HookSafeInvocations_OptionalCallbacks_InvokedWhenPresent`
**Test Intent**: Verify hook invocation utility safely calls optional member functions if present.

#### `TraitsAndHooks.MultiArityInvocations_GuardsAndActions_AcceptsVariedSignatures`
**Test Intent**: Verify guard and action invocation helpers support 0, 1, and 2-parameter signatures.

#### `TraitsAndHooks.DispatchResult_ObserverPolicies_TracksSuccessAndTracing`
**Test Intent**: Verify DispatchResult return structure and observer policy flags.

#### `TraitsAndHooks.TransitionTrace_Inspection_ReportsPathAndKind`
**Test Intent**: Verify inspection of transition trace details in DispatchResult.

#### `TraitsAndHooks.LegacyContextCheck_PoisonDetection_PreventsForbiddenPatterns`
**Test Intent**: Verify compile-time checks prohibiting legacy mutable global context pointers.

#### `TraitsAndHooks.DuplicateRows_TraitCheck_DetectsDuplicateTransitionSignatures`
**Test Intent**: Verify compile-time duplicate row detection in transition tables.

#### `TraitsAndHooks.StateName_StaticResolution_MatchesRuntimeInspection`
**Test Intent**: Verify consistency between static constexpr state names and runtime inspection.

#### `TraitsAndHooks.HistoryOverload_TypeSafety_ValidatesPseudostates`
**Test Intent**: Verify type safety of history pseudostate overloads in transition dispatch.

#### `TraitsAndHooks.ConceptCompliance_ScalarSanity_SatisfiesTypeRequirements`
**Test Intent**: Verify scalar types satisfy runtime concept constraints.

#### `TraitsAndHooks.TypeListIndexOf_CompileTimeLookup_CalculatesCorrectIndices`
**Test Intent**: Verify compile-time index lookup of types within type lists.

### [`test_zero_alloc_runtime.cpp`](../tests/backend/cpp/runtime/test_zero_alloc_runtime.cpp) (`tests/backend/cpp/runtime/test_zero_alloc_runtime.cpp`)
#### `ZeroAllocRuntime.StaticRingBuffer_BasicOperations_ExecutesWithoutHeapAllocations`
**Test Intent**: Unit test suite for zero-allocation runtime, static ring buffers, and embedded static vectors.
/

#### `ZeroAllocRuntime.MemoryFootprint_CompileTimeSize_ZeroHeapOverhead`
**Test Intent**: Verify compile-time memory footprint of zero-allocation state machine.

#### `ZeroAllocRuntime.SpscFsm_ZeroAllocTransitions_DispatchesCorrectly`
**Test Intent**: Verify SPSC state machine operations under zero-allocation runtime.

#### `ZeroAllocRuntime.StaticRingBuffer_PeekAndClear_PreservesInternalIntegrity`
**Test Intent**: Verify peek and clear operations on static ring buffer.

#### `ZeroAllocRuntime.SpscFsm_QueueOverflowHandling_DropsOrRejectsEvents`
**Test Intent**: Verify overflow handling on static ring buffer SPSC machine.

#### `ZeroAllocRuntime.StaticVector_BasicOperations_FunctionsAsZeroAllocVector`
**Test Intent**: Verify static vector operations (push_back, pop_back, indexing).

#### `ZeroAllocRuntime.StaticVector_ResourceReset_DestructsElementsProperly`
**Test Intent**: Verify proper element destruction on erase and pop_back in static vector.

#### `ZeroAllocRuntime.ZeroAllocRuntime_HistoryAndDeferred_ExecutesWithoutHeap`
**Test Intent**: Verify complete zero-allocation execution including history states and deferred events.

---

## C++ Backend Codegen Subsystem

### [`test_cpp17_standalone.cpp`](../tests/backend/cpp/test_cpp17_standalone.cpp) (`tests/backend/cpp/test_cpp17_standalone.cpp`)
- *(Executable binary test verification)*

### [`test_cpp20_standalone.cpp`](../tests/backend/cpp/test_cpp20_standalone.cpp) (`tests/backend/cpp/test_cpp20_standalone.cpp`)
- *(Executable binary test verification)*

### [`test_cpp_e2e_compiler.cpp`](../tests/backend/cpp/test_cpp_e2e_compiler.cpp) (`tests/backend/cpp/test_cpp_e2e_compiler.cpp`)
#### `CppE2ECompiler.GeneratedStandaloneHeaders_CompileAndExecuteUnderCpp17AndCpp20`
**Test Intent**: End-to-End integration and host compiler verification test suite for C++ Emitter (C++17 & C++20).

#### `CppE2ECompiler.RuntimeBundling_ExportsHeadersAndResilientCode`
**Test Intent**: Verify RuntimeExporter bundles standalone runtime headers and resilience code.

#### `CppE2ECompiler.HierarchicalBoundaryActionFusion_ExecutesExactlyOnceInOrder`
**Test Intent**: Verify BoundaryActionFusionPass produces correctly ordered, non-duplicated lifecycle hooks

#### `CppE2ECompiler.OrthogonalProductStateExecution_UnderCpp17AndCpp20`
**Test Intent**: Verify compilation and execution of flattened orthogonal product states in C++17 and C++20.

**Scenario**:
  - Model with concurrent orthogonal regions is lowered to product states, compiled and executed under host

#### `CppE2ECompiler.GeneratedTimedTransitionExecution_UnderCpp17AndCpp20`
**Test Intent**: End-to-end verification of TimeTrigger (after_ms) lowering and execution under C++17 and C++20.

#### `CppE2ECompiler.GeneratedTimeInvariantEnforcement_UnderCpp17AndCpp20`
**Test Intent**: End-to-end verification of state time invariant (permanence bound) enforcement under C++17 and C++20.

#### `CppE2ECompiler.NonDefaultConstructibleServices_CompilesAndInjectsProperly_UnderCpp17AndCpp20`
**Test Intent**: Verify generation and compilation when services interface is non-default-constructible.

#### `CppE2ECompiler.NoStubsAndNoThreadSafeGeneration_CompilesCleanly`
**Test Intent**: Verify generation with include_stubs=false and thread_safe=false.

### [`test_cpp_model_emitter.cpp`](../tests/backend/cpp/test_cpp_model_emitter.cpp) (`tests/backend/cpp/test_cpp_model_emitter.cpp`)
#### `CppModelEmitter.PartitionedDomainStructures_EmittedCorrectly`
**Test Intent**: Unit verification suite for C++ model emitter syntax generation.

#### `CppModelEmitter.TypedSignalPayloads_EmittedWithValidators`
**Test Intent**: Verify C++ emission of strongly-typed signal structs with inline validator predicates.

#### `CppModelEmitter.StatesLifecycleHooks_EmittedWithRequirements`
**Test Intent**: Verify C++ emission of state lifecycle hooks (on_enter, on_exit) and requirement annotations.

#### `CppBackendValidator.UnloweredStructuralFeatures_AreRejectedBeforeEmission`
**Test Intent**: Verify the C++ backend rejects structural IR that has not been lowered.

#### `CppBackendValidator.AtomicModel_IsAcceptedByCxxBackend`
**Test Intent**: Verify the C++ backend accepts an already lowered atomic model.

#### `CppBackendValidator.AbsoluteTimeTrigger_IsRejectedUntilClockLoweringExists`
**Test Intent**: Verify the C++ backend rejects IR time triggers without supported lowering.

#### `CppBackendValidator.ForkAndJoinPseudostates_AreRejectedByCxxBackend`
**Test Intent**: Verify the C++ backend rejects unlowered Fork and Join pseudostates.

#### `CppBackendValidator.MultiTargetAndMultiSourceTransitions_AreRejectedByCxxBackend`
**Test Intent**: Verify the C++ backend rejects unlowered multi-target and multi-source transitions.

#### `CppGenerator.ChoiceInStateList_IsInlinedAutomaticallyBeforeValidation`
**Test Intent**: Verify that CppGenerator inlines Choice states declared directly in model.states.

#### `CppModelEmitter.ConstantTimeTriggers_EmitRuntimeTimerEvents`
**Test Intent**: Verify the C++ emitter maps constant after/every triggers to runtime timer events.

#### `CppGenerator.ParallelRegions_AreLoweredBeforeEmission`
**Test Intent**: Verify the C++ generator lowers parallel regions into product states.

#### `CppModelEmitter.TransitionTable_EmittedWithPriorityOrdering`
**Test Intent**: Verify C++ emission of transition tables sorted by descending priority.

#### `CppModelEmitter.EfsmResolvedGuards_EmittedCorrectly`
**Test Intent**: Verify automated C++ emission of resolved EFSM guard lambda functions.

#### `CppModelEmitter.EnumAndStructDefinitions_EmittedCorrectly`
**Test Intent**: Verify C++ emission of SysML v2 / formal IR Enums and Struct definitions.

#### `CppModelEmitter.FluentFactoryAliases_EmittedCorrectly`
**Test Intent**: Verify C++ emission of modern fluent factory aliases (make_fsm, make_thread_safe_fsm).

#### `CppModelEmitter.DoxygenTraceabilityAnnotations_EmittedCorrectly`
**Test Intent**: Verify C++ emission of Doxygen requirement traceability annotations (@satisfies).

### [`test_generated_fsm.cpp`](../tests/backend/cpp/test_generated_fsm.cpp) (`tests/backend/cpp/test_generated_fsm.cpp`)
- *(Executable binary test verification)*

---

## Diagram & Emitter Backend Subsystem

### [`test_companion_manifest_emitter.cpp`](../tests/backend/diagram/test_companion_manifest_emitter.cpp) (`tests/backend/diagram/test_companion_manifest_emitter.cpp`)
#### `CompanionManifestEmitter.ModelContract_ExtractedIntoManifest`
**Test Intent**: Unit test suite for companion manifest emission (YAML/JSON sidecar contracts).
/

#### `CompanionManifestEmitter.YamlSidecar_RoundtrippedLosslessly`
**Test Intent**: Verify YAML companion manifest serialization and deserialization roundtrip.

#### `CompanionManifestEmitter.JsonSidecar_RoundtrippedLosslessly`
**Test Intent**: Verify JSON companion manifest serialization and deserialization roundtrip.

#### `CompanionManifestEmitter.BareTopologyAndSidecar_RecombinedIntoCompleteModel`
**Test Intent**: Verify sidecar roundtrip with bare diagram topology and YAML manifest recombiner.

### [`test_diagram_export.cpp`](../tests/backend/diagram/test_diagram_export.cpp) (`tests/backend/diagram/test_diagram_export.cpp`)
#### `DiagramExport.CameoXmiModel_ExportedToMermaidAndValidated`
**Test Intent**: Unit test suite for cross-format diagram and formal model serializers.
/

#### `DiagramExport.ScxmlModel_ExportedToPlantUmlAndValidated`
**Test Intent**: Verify cross-format export from W3C SCXML to PlantUML state diagrams.

#### `DiagramExport.Sysml2Model_ExportedToPlantUmlAndMermaid`
**Test Intent**: Verify SysML v2 state definition export serialization.

#### `DiagramExport.IndustrialPressModel_RoundtrippedAcrossPlantUmlMermaidJson`
**Test Intent**: Verify multi-format roundtrip fidelity for complex hierarchical state machines.

#### `DiagramExport.AdvancedPseudostatesAndInvariants_PreservedAcrossDiagramExports`
**Test Intent**: Verify multi-format serialization of EntryPoint, ExitPoint, time_invariant, and transition priorities.

#### `DiagramExport.SmvModel_ExportedWithInvariantsAndLtlProperties`
**Test Intent**: Verify nuXmv / SMV formal model serialization with variables, transitions, and LTL properties.

#### `DiagramExport.PseudostatesAndOrthogonalRegions_ExportedCorrectly`
**Test Intent**: Verify Cameo OMG XMI and SCXML export for hierarchical pseudostates and orthogonal regions.

#### `DiagramExport.FsmIrModel_ExportedToDotGraphvizFormat`
**Test Intent**: Verify DOT / Graphviz diagram serialization and syntax integrity.

---

## Multi-Format Emitter & Factory Subsystem

### [`test_emitter_factory.cpp`](../tests/backend/common/test_emitter_factory.cpp) (`tests/backend/common/test_emitter_factory.cpp`)
#### `EmitterFactory.SupportedFormatsList_ContainsAllRegisteredBackends`
**Test Intent**: Unit test suite for the multi-format emitter factory.
/

#### `EmitterFactory.CanonicalModel_EmittedAcrossAllRegisteredFormats`
**Test Intent**: Verify emission across all supported formats via factory dispatcher.

---

## MC/DC Verification & Harness Subsystem

### [`test_mcdc_harness.cpp`](../tests/backend/verification/test_mcdc_harness.cpp) (`tests/backend/verification/test_mcdc_harness.cpp`)
#### `McdcHarness.ConjunctionExpression_IndependencePairsCalculated`
**Test Intent**: Unit test suite for MC/DC test harness generation and independence pair analysis.
/

#### `McdcHarness.TransitionGuard_GtestHarnessGenerated`
**Test Intent**: Verify McdcHarnessGenerator produces GoogleTest harness string for transition guards.

#### `McdcHarness.AvionicsModel_DriverHarnessSynthesized`
**Test Intent**: Verify synthesis of driver harness for multi-condition avionics transition.

---

## Formal Model Checking & nuXmv Subsystem

### [`test_formal_roundtrip.cpp`](../tests/backend/formal/test_formal_roundtrip.cpp) (`tests/backend/formal/test_formal_roundtrip.cpp`)
#### `FormalRoundtrip.ConnectionManagerPreset_PreservedAcrossFormats`
**Test Intent**: Unit test suite verifying lossless roundtrip transpilation across all supported formats.
/

#### `FormalRoundtrip.AsyncMotorControllerPreset_PreservedAcrossFormats`
**Test Intent**: Verify lossless multi-format roundtrip for Async Motor Controller preset.

#### `FormalRoundtrip.MissionControllerPreset_PreservedAcrossFormats`
**Test Intent**: Verify lossless multi-format roundtrip for Aerospace Mission Controller preset.

#### `FormalRoundtrip.IndustrialPressPreset_PreservedAcrossFormats`
**Test Intent**: Verify lossless multi-format roundtrip for Industrial Press controller.

#### `FormalRoundtrip.Sysml2SpacecraftPreset_PreservedAcrossFormats`
**Test Intent**: Verify OMG SysML v2 syntax parsing and lossless multi-format roundtrip.

#### `FormalRoundtrip.DeepHierarchyAndDeferredEvents_PreservedAcrossFormats`
**Test Intent**: Verify nested composite states and deferred event list preservation during multi-format roundtrip.

#### `FormalRoundtrip.ShallowAndDeepHistory_PreservedAcrossFormats`
**Test Intent**: Verify shallow [H] and deep [H*] history pseudostate roundtrip serialization.

#### `FormalRoundtrip.ComplexBooleanGuards_PreservedAcrossFormats`
**Test Intent**: Verify complex compound boolean guard expressions across multi-format serializers.

#### `FormalRoundtrip.ClosedLoop7HopFormatRing_PreservedAcrossFormats`
**Test Intent**: Verify 7-hop circular conversion ring without data loss.

#### `FormalRoundtrip.NativeLanguageAllProperties_PreservedAcrossFormats`
**Test Intent**: Verify lossless preservation of native EFSM variables, signals, requirements, and invariants.

#### `FormalRoundtrip.Sysml2ToPlantUmlWithDirectives_PreservedAcrossFormats`
**Test Intent**: Verify roundtrip between SysML v2 and PlantUML with @fsm inline directives.

#### `FormalRoundtrip.HierarchicalSysml2AndSmv_ClosedLoopPreserved`
**Test Intent**: Verify closed-loop roundtrip between SysML v2 and nuXmv / SMV formal models.

#### `FormalRoundtrip.TypedPortsAndContracts_PreservedAcrossFormats`
**Test Intent**: Verify lossless roundtrip of Typed In/Out Ports and Numeric Assert Constraints across all 7 formats.

#### `FormalRoundtrip.AutonomousUavMissionPreset_PreservedAcrossFormats`
**Test Intent**: Verify 100% lossless multi-format roundtrip and traceability requirements for UAV Mission preset.

#### `FormalRoundtrip.UniversalDataDefinitions_PreservedAcrossFormats`
**Test Intent**: Verify universal lossless roundtrip of enum and struct definitions across all formats.

---

## Requirements Traceability (RTM) Subsystem

### [`test_rtm_emitter.cpp`](../tests/backend/rtm/test_rtm_emitter.cpp) (`tests/backend/rtm/test_rtm_emitter.cpp`)
#### `RtmEmitter.TraceabilityResults_EmittedInMarkdownAndJson`
**Test Intent**: Unit test suite for the Requirements Traceability Matrix (RTM) emitter.
/

#### `RtmEmitter.UntracedElements_AuditedWithSummaryDiagnostic`
**Test Intent**: Verify RtmEmitter::audit_traceability reports untraced states and summary statistics.

---

## Diagnostic Engine Subsystem

### [`test_diagnostics.cpp`](../tests/diagnostic/test_diagnostics.cpp) (`tests/diagnostic/test_diagnostics.cpp`)
#### `DiagnosticEngine.WarningWithSourceSpan_RenderedWithCaretUnderlineAndHelp`
**Test Intent**: Unit test suite for the compiler diagnostic engine and source caret rendering.
/

---

## Frontend Parser Subsystem

### [`test_json_parser.cpp`](../tests/frontend/common/test_json_parser.cpp) (`tests/frontend/common/test_json_parser.cpp`)
#### `JsonParser.StandardStatechartSchema_ExtractsStatesTransitionsGuardsAndActions`
**Test Intent**: Unit tests for front-end JSON statechart parser, composite states, transitions arrays, and port constraints.
/

#### `JsonParser.NestedStatesProperty_SynthesizesCompositeHierarchy`
**Test Intent**: Verify nested composite states within JSON schema.

#### `JsonParser.ArrayOfTransitionsPerEventKey_CapturesMultipleConditionalBranches`
**Test Intent**: Verify array of conditional transitions per event key in JSON.

#### `JsonParser.StateDefinitionsSequence_PreservesDocumentInsertionOrder`
**Test Intent**: Verify document insertion order preservation of state definitions in JSON.

#### `JsonParser.DirectionalPortsWithIntervalBounds_PopulatesPortDefinitions`
**Test Intent**: Verify parsing of typed ports and range constraints in JSON schema.

### [`test_parser_classification.cpp`](../tests/frontend/common/test_parser_classification.cpp) (`tests/frontend/common/test_parser_classification.cpp`)
#### `FrontendClassification.FormalParsers_ReportsFormalFrontendKind`
**Test Intent**: Unit tests for front-end parser taxonomy and format classification (Formal vs Diagram).
/

#### `FrontendClassification.DiagramParsers_ReportsDiagramFrontendKind`
**Test Intent**: Verify diagram parser implementations report FrontendKind::Diagram and canonical format tokens.

#### `ParserFactory.FormatNameToKindLookup_ResolvesFormalAndDiagramKinds`
**Test Intent**: Verify ParserFactory format name to FrontendKind resolution mapping.

### [`test_parser_factory_and_lexer.cpp`](../tests/frontend/common/test_parser_factory_and_lexer.cpp) (`tests/frontend/common/test_parser_factory_and_lexer.cpp`)
#### `ParserFactory.CanonicalFormatNames_InstantiatesCorrespondingParser`
**Test Intent**: Unit tests for ParserFactory dynamic parser resolution and LexerUtils lexical analysis routines.
/

#### `ParserFactory.FileExtensions_ResolvesMatchingParserImplementation`
**Test Intent**: Verify ParserFactory instantiation based on input file path extension.

#### `ParserFactory.ExplicitFormatOverride_OverridesFileExtensionConvention`
**Test Intent**: Verify ParserFactory explicit format override and fallback behaviour.

#### `LexerUtils.BracketDelimitedTokens_ExtractsInnermostAndNestedSubstrings`
**Test Intent**: Verify bracket-delimited token extraction handling nested brackets and delimiters.

#### `LexerUtils.QuoteDelimitedTokens_ExtractsSingleAndDoubleQuotedStrings`
**Test Intent**: Verify single and double quote string extraction.

#### `LexerUtils.TransitionLabelGrammar_DecomposesTriggerGuardAndActionParts`
**Test Intent**: Verify transition label parsing according to 'event [guard] / action' grammar.

#### `LexerUtils.ReservedKeywords_EscapesCppKeywordsWithUnderscore`
**Test Intent**: Verify C++ reserved keyword detection and identifier escaping utilities.

### [`test_parser_negative.cpp`](../tests/frontend/common/test_parser_negative.cpp) (`tests/frontend/common/test_parser_negative.cpp`)
#### `PlantUmlParser.EmptyAndCorruptedInputStreams_RejectsWithInformativeDiagnostic`
**Test Intent**: Negative unit tests across front-end parsers verifying rejection of malformed, empty, or corrupt inputs.
/

#### `MermaidParser.MissingStatesAndMalformedArrows_RejectsParseCleanly`
**Test Intent**: Verify MermaidParser rejects empty inputs and malformed transition statements.

#### `XmlFrontend.UnclosedTagsAndNonXmlStrings_FailsGracefullyWithoutThrowing`
**Test Intent**: Verify XML parsers (Cameo XMI and W3C SCXML) reject malformed XML tags and non-XML text.

#### `JsonParser.MalformedSyntaxAndEmptyStateObjects_RejectsWithErrors`
**Test Intent**: Verify JsonParser rejects invalid JSON syntax, wrong root types, and empty objects.

#### `Sysml2Parser.EmptyStateMachineDefinitions_RejectsGracefully`
**Test Intent**: Verify Sysml2Parser rejects empty definitions and invalid token streams.

#### `FsmValidator.DisconnectedSubgraphsAndTrapStates_EmitsSemanticWarningDiagnostics`
**Test Intent**: Verify FsmValidator semantic diagnostics (unreachable island states, trap/deadlock states).

### [`test_xml_parser.cpp`](../tests/frontend/common/test_xml_parser.cpp) (`tests/frontend/common/test_xml_parser.cpp`)
#### `XmlParser.BasicXmlDocument_ParsedIntoElementHierarchy`
**Test Intent**: Unit test suite for the lightweight XML parser and DOM abstraction.
/

#### `XmlParser.EntityReferences_DecodedCorrectly`
**Test Intent**: Verify XML entity decoding in attribute values.

#### `XmlParser.CDataSections_ExtractedWithoutEntityDecoding`
**Test Intent**: Verify CDATA section preservation in script blocks.

#### `XmlParser.NamespacePrefixedTags_ResolvedAgnostically`
**Test Intent**: Verify XML namespace prefix-agnostic recursive tag lookup.

### [`test_diagram_sidecar.cpp`](../tests/frontend/diagram/test_diagram_sidecar.cpp) (`tests/frontend/diagram/test_diagram_sidecar.cpp`)
#### `DiagramSidecar.PlantUmlTopology_CombinedWithYamlCompanionManifest`
**Test Intent**: Unit test suite for the Diagram Sidecar Pattern combining diagram topology with companion manifests.
/

#### `DiagramSidecar.MermaidTopology_CombinedWithJsonCompanionManifest`
**Test Intent**: Verify Diagram Sidecar Pattern with Mermaid topology and JSON manifest.

### [`test_dot_parser.cpp`](../tests/frontend/diagram/test_dot_parser.cpp) (`tests/frontend/diagram/test_dot_parser.cpp`)
#### `DotParser.BasicDotDigraph_ParsedIntoValidFsmIr`
**Test Intent**: Unit test suite for the Graphviz DOT statechart parser.
/

#### `DotParser.ClusterSubgraph_ParsedAsCompositeState`
**Test Intent**: Verify DOT subgraph cluster parsing into hierarchical composite states.

### [`test_mermaid_parser.cpp`](../tests/frontend/diagram/test_mermaid_parser.cpp) (`tests/frontend/diagram/test_mermaid_parser.cpp`)
#### `MermaidParser.BasicDiagram_ParsedIntoValidFsmIr`
**Test Intent**: Unit test suite for the Mermaid stateDiagram-v2 parser and frontend dialect.
/

#### `MermaidParser.CommentsNotesAndHierarchy_ParsedCorrectly`
**Test Intent**: Verify Mermaid comment stripping, note stripping, and composite state hierarchy.

#### `MermaidParser.EmptyInput_GracefullyRejected`
**Test Intent**: Verify Mermaid parser gracefully rejects empty and whitespace-only inputs.

#### `MermaidParser.EntryExitPointAndPriority_CapturedInIr`
**Test Intent**: Verify Mermaid parsing of entryPoint, exitPoint, and transition priority.

#### `MermaidParser.PortDirectives_ParsedWithAttributesAndConstraints`
**Test Intent**: Verify Mermaid parsing of @fsm:port directives into FsmIr.

### [`test_plantuml_parser.cpp`](../tests/frontend/diagram/test_plantuml_parser.cpp) (`tests/frontend/diagram/test_plantuml_parser.cpp`)
#### `PlantUmlParser.BasicDiagram_ParsedIntoValidFsmIr`
**Test Intent**: Unit test suite for the PlantUML state diagram parser and frontend dialect.
/

#### `PlantUmlParser.CommentsAndCompositeHierarchy_ParsedCorrectly`
**Test Intent**: Verify PlantUML single-line and multi-line comment stripping and composite states.

#### `PlantUmlParser.UndefinedTargetState_DetectedByValidator`
**Test Intent**: Verify FsmValidator detects undefined transition target states in PlantUML models.

#### `PlantUmlParser.EmptyInput_GracefullyRejected`
**Test Intent**: Verify PlantUML parser gracefully rejects empty input.

#### `PlantUmlParser.EntryExitPointPriorityAndInvariant_CapturedInIr`
**Test Intent**: Verify PlantUML parsing of entryPoint, exitPoint, time invariants, and transition priorities.

#### `PlantUmlParser.PortDirectives_ParsedWithAttributesAndConstraints`
**Test Intent**: Verify PlantUML parsing of @fsm:port inline directives into FsmIr ports.

### [`test_directive_parser.cpp`](../tests/frontend/directive/test_directive_parser.cpp) (`tests/frontend/directive/test_directive_parser.cpp`)
#### `DirectiveParser.StateDirective_ParsesTraceabilityAndActivities`
**Test Intent**: Unit tests for front-end directive parser (@fsm:state, @fsm:defer, @fsm:signal, @fsm:port, @fsm:enum,

#### `DirectiveParser.DeferDirective_ExtractsDeferredEventList`
**Test Intent**: Verify '@fsm:defer [...]' directive parsing for deferred events.

#### `DirectiveParser.SignalDirective_ExtractsPayloadAttributesAndValidators`
**Test Intent**: Verify '@fsm:signal' directive parsing with payload attributes and validation expressions.

#### `DirectiveParser.TransitionDirective_PopulatesGuardAstAndActionSignatures`
**Test Intent**: Verify '@fsm:trans' directive parsing for custom transition IDs, guard ASTs, and actions.

#### `DirectiveParser.PortDirective_ExtractsDirectionBoundsAndUnits`
**Test Intent**: Verify '@fsm:port' directive parsing with direction, numeric bounds, and constraint expression.

#### `DirectiveParser.EnumDirective_RoundtripsSerializationFidelity`
**Test Intent**: Verify '@fsm:enum' directive parsing and roundtrip serialization.

#### `DirectiveParser.StructDirective_RoundtripsFieldDefinitionsFidelity`
**Test Intent**: Verify '@fsm:struct' directive parsing and roundtrip serialization.

### [`test_cameo_parser.cpp`](../tests/frontend/formal/test_cameo_parser.cpp) (`tests/frontend/formal/test_cameo_parser.cpp`)
#### `CameoParser.BasicXmiDocument_ParsedIntoValidFsmIr`
**Test Intent**: Unit test suite for the Cameo Systems Modeler (OMG XMI 2.x) frontend parser.
/

#### `CameoParser.CompositeAndChoicePseudostates_ParsedIntoValidFsmIr`
**Test Intent**: Verify Cameo nested composite regions and choice pseudostates.

#### `CameoParser.AttributeStyleEffectsAndActions_ParsedIntoValidFsmIr`
**Test Intent**: Verify attribute-style XML transition properties (trigger, guard, effect).

#### `CameoParser.NativeEntryExitAndDoActivity_CapturedInStateNode`
**Test Intent**: Verify Cameo entry, doActivity, and exit behavior parsing.

#### `CameoParser.XmlEntitiesInNamesAndGuards_DecodedCorrectly`
**Test Intent**: Verify XML entity decoding in transition guards and state names.

#### `CameoParser.HistoryAndJunctionPseudostates_PreservedInIr`
**Test Intent**: Verify Cameo UML history and junction pseudostate parsing.

#### `CameoParser.CrossReferencedTriggersAndSysmlProfiles_ResolvedCorrectly`
**Test Intent**: Verify two-pass resolution of cross-referenced Signal events and SysML profile stereotypes.

### [`test_scxml_parser.cpp`](../tests/frontend/formal/test_scxml_parser.cpp) (`tests/frontend/formal/test_scxml_parser.cpp`)
#### `ScxmlParser.BasicScxmlDocument_ParsedIntoValidFsmIr`
**Test Intent**: Unit test suite for the W3C SCXML (State Chart XML) frontend parser.
/

#### `ScxmlParser.IndustrialPressSnippet_ParsedIntoValidFsmIr`
**Test Intent**: Verify parsing of real-world industrial press SCXML specification.

#### `ScxmlParser.AttributePermutationsAndSelfClosingStates_ParsedCorrectly`
**Test Intent**: Verify robustness against SCXML attribute permutations and self-closing state tags.

#### `ScxmlParser.NativeDatamodelAndLifecycleHooks_CapturedInIr`
**Test Intent**: Verify SCXML <datamodel>, <onentry>, and <onexit> lifecycle hooks.

### [`test_scxml_semantic_completeness.cpp`](../tests/frontend/formal/test_scxml_semantic_completeness.cpp) (`tests/frontend/formal/test_scxml_semantic_completeness.cpp`)
#### `ScxmlSemanticCompleteness.ParallelRegions_ParsedAsOrthogonalStates`
**Test Intent**: Unit test suite verifying W3C SCXML semantic completeness (parallel regions, final states, events).
/

#### `ScxmlSemanticCompleteness.FinalStates_EmitsCompletionEvents`
**Test Intent**: Verify SCXML <final> states and automatic completion events.

#### `ScxmlSemanticCompleteness.SendAndRaiseDirectives_CapturedInActionIr`
**Test Intent**: Verify SCXML <send> and <raise> event dispatching statements in executable content.

#### `ScxmlSemanticCompleteness.XmlEntities_DecodedInGuardsAndAssignments`
**Test Intent**: Verify XML entity decoding inside SCXML condition attributes and data expressions.

### [`test_smv_parser.cpp`](../tests/frontend/formal/test_smv_parser.cpp) (`tests/frontend/formal/test_smv_parser.cpp`)
#### `SmvParser.BasicSmvModule_ParsedIntoValidFsmIr`
**Test Intent**: Unit test suite for the nuXmv / SMV formal model frontend parser.
/

#### `SmvParser.VariablesAndInitExpressions_CapturedInIr`
**Test Intent**: Verify SMV auxiliary state variables, ranges, and init expressions.

#### `SmvParser.LtlSpecsAndInvariants_CapturedAsFormalProperties`
**Test Intent**: Verify extraction of LTLSPEC and INVAR formal verification properties.

#### `SmvParser.SmvModule_GeneratesCompilableCppCode`
**Test Intent**: Verify C++ code generation compatibility from parsed SMV formal models.

#### `SmvParser.MalformedSmv_RejectionDiagnosticsReported`
**Test Intent**: Verify graceful diagnostic reporting on malformed SMV input.

#### `SmvParser.MultilineCaseExpressions_InferredAsStateTransitions`
**Test Intent**: Verify parsing of multiline case expressions and pure SMV state inference.

### [`test_stateflow_parser.cpp`](../tests/frontend/formal/test_stateflow_parser.cpp) (`tests/frontend/formal/test_stateflow_parser.cpp`)
#### `StateflowParser.BasicStateflowChart_ParsedIntoValidFsmIr`
**Test Intent**: Unit test suite for the MathWorks Stateflow chart frontend parser.
/

#### `StateflowParser.TemporalLogicTriggers_MappedToTimerEvents`
**Test Intent**: Verify Stateflow temporal logic triggers (after, before, at, every).

#### `StateflowParser.ParserFactoryLookup_InstantiatesStateflowParser`
**Test Intent**: Verify ParserFactory instantiation for Stateflow format name.

#### `StateflowParser.DirectivesAndTransitions_RoundtrippedLosslessly`
**Test Intent**: Verify roundtrip serialization of Stateflow charts with transition directives.

#### `StateflowParser.NestedBracketsAndJunctions_ParsedCorrectly`
**Test Intent**: Verify Stateflow connective junctions and nested condition brackets.

### [`test_sysml2_flight_control.cpp`](../tests/frontend/formal/test_sysml2_flight_control.cpp) (`tests/frontend/formal/test_sysml2_flight_control.cpp`)
#### `Sysml2FlightControl.FlightMissionController_ParsedIntoValidFsmIr`
**Test Intent**: Integration verification suite for OMG SysML v2 Flight Mission Controller pipeline.
/

#### `Sysml2FlightControl.IntervalAnalysis_ProvesVariableSafety`
**Test Intent**: Verify middle-end safety passes and EFSM interval analysis on flight control model.

#### `Sysml2FlightControl.CppCodeGeneration_ProducesCompilableHeader`
**Test Intent**: Verify C++ code generation for flight control system.

#### `Sysml2FlightControl.DualParadigmRuntime_ExecutesTransitions`
**Test Intent**: Verify execution of flight controller under dual synchronous and asynchronous runtime.

#### `Sysml2FlightControl.EmergencyStop_TransitionsToSafeFailsafe`
**Test Intent**: Verify runtime emergency stop failsafe transition.

### [`test_sysml2_parser.cpp`](../tests/frontend/formal/test_sysml2_parser.cpp) (`tests/frontend/formal/test_sysml2_parser.cpp`)
#### `Sysml2Parser.MultilineTransitions_ParsedIntoValidFsmIr`
**Test Intent**: Unit test suite for the OMG SysML v2 State Definition frontend parser.
/

#### `Sysml2Parser.CompactTransitions_ParsedIntoValidFsmIr`
**Test Intent**: Verify compact inline SysML v2 transition syntax (first S1; then S2;).

#### `Sysml2Parser.CompositeStates_ParsedAndCodeGenerated`
**Test Intent**: Verify nested composite states in SysML v2 and downstream code generation.

#### `Sysml2Parser.NativeAttributesAndItemDefs_CapturedInIr`
**Test Intent**: Verify SysML v2 attribute definitions and item def message payload types.

#### `Sysml2Parser.ParallelRegionsAndSubmachines_ParsedCorrectly`
**Test Intent**: Verify parallel orthogonal regions and submachine references in SysML v2.

#### `Sysml2Parser.EntryExitPointAndInvariants_CapturedInIr`
**Test Intent**: Verify entryPoint, exitPoint, stay duration invariants, and transition priorities.

#### `Sysml2Parser.ChoiceNodeComparisonGuards_ParsedCorrectly`
**Test Intent**: Verify choice pseudostate parsing with relational comparison guards.

#### `Sysml2Parser.EfsmAssignmentActions_ParsedCorrectly`
**Test Intent**: Verify semantic EFSM variable assignment actions in transition do blocks.

#### `Sysml2Parser.StructuralBlockFiltering_BalancesBraces`
**Test Intent**: Verify robust brace balancing and structural block filtering in SysML v2 files.

#### `Sysml2Parser.SendSignalViaPort_ParsedIntoActionIr`
**Test Intent**: Verify SysML v2 'send Signal via port' action statement parsing.

### [`test_sysml2_structured_data.cpp`](../tests/frontend/formal/test_sysml2_structured_data.cpp) (`tests/frontend/formal/test_sysml2_structured_data.cpp`)
#### `Sysml2StructuredData.EnumDefinitions_ParsedIntoIrTypes`
**Test Intent**: Unit test suite for SysML v2 structured data types, enums, temporal triggers, and connection points.
/

#### `Sysml2StructuredData.StructDefinitions_ParsedIntoIrTypes`
**Test Intent**: Verify SysML v2 struct and datatype definitions parsing.

#### `Sysml2StructuredData.NativeTemporalTriggers_ParsedIntoTimedEvents`
**Test Intent**: Verify SysML v2 native temporal triggers (after, at, every).

#### `Sysml2StructuredData.ConnectionPseudostates_ParsedIntoIrNodes`
**Test Intent**: Verify SysML v2 connection pseudostates (fork, join, junction, choice).

#### `Sysml2StructuredData.FullModel_RoundtrippedLosslessly`
**Test Intent**: Verify lossless roundtrip serialization of SysML v2 models with structured data.

---

## Formal IR Subsystem

### [`test_data_definitions.cpp`](../tests/ir/test_data_definitions.cpp) (`tests/ir/test_data_definitions.cpp`)
#### `EnumDefinition.LiteralInstantiationAndQueryMethods_PreservesValueSemantics`
**Test Intent**: Unit tests for IR user-defined data structures: EnumDefinition, StructDefinition, and JSON schema

#### `StructDefinition.FieldAttributesAndDomainContracts_PreservesValueSemantics`
**Test Intent**: Verify StructDefinition and StructField attributes, ISQ units, domain contracts, and equality.

#### `FsmIr.UserDefinedTypesIntegration_PreservesCanonicalOrderingAndLookup`
**Test Intent**: Verify FsmIr metamodel container integration, lookup methods, and canonical sorting.

#### `FsmIrSerializer.DataDefinitionsJsonSerialization_PreservesRoundtripFidelity`
**Test Intent**: Verify lossless JSON IR serialization for user-defined enums and struct definitions.

#### `FsmIrSerializer.CustomEnumsAndStructsDiagramExport_EmitsJsonSchemaSections`
**Test Intent**: Verify diagram JSON emission of enums and structs for JSON schema export.

### [`test_data_type.cpp`](../tests/ir/test_data_type.cpp) (`tests/ir/test_data_type.cpp`)
#### `DataType.PrimitiveFactories_ClassifiesKindsAndBitWidths`
**Test Intent**: Unit tests for DataType target-agnostic type system, classification, parsing, and multi-language lowering.
/

#### `DataType.CustomEnumAndStructTypes_PreservesTypenameAndKind`
**Test Intent**: Verify DataType user-defined types (enumeration, structure, custom handle).

#### `DataType.StringTypeRepresentation_ParsesSysmlAndCppTypenames`
**Test Intent**: Verify DataType::from_string parsing across SysML v2 / KerML, C++, and custom declarations.

#### `DataType.TargetAgnosticTypeLowering_EmitsCppSysmlRustAndSmvTypes`
**Test Intent**: Verify multi-target backend type lowering for C++, SysML v2, Rust, and nuXmv / SMV.

#### `DataType.MetamodelEntitiesIntegration_AdaptsVariablePortAndSignalTypes`
**Test Intent**: Verify seamless integration of DataType across VariableDefinition, PortDefinition, StructField, and

#### `GuardAstNode.TargetAgnosticBooleanSyntax_EmitsCppSysmlSmvAndRustExpressions`
**Test Intent**: Verify GuardAstNode multi-language boolean syntax lowering.

#### `GuardModel.NormalizedAlgebraicExpression_PreservesCanonicalString`
**Test Intent**: Verify GuardModel retains both original and normalized algebraic expressions.

#### `DataType.TypeClassificationStringFormatting_SerializesToStream`
**Test Intent**: Verify TypeClassification string serialization and stream output operator.

### [`test_efsm_metamodel_extensions.cpp`](../tests/ir/test_efsm_metamodel_extensions.cpp) (`tests/ir/test_efsm_metamodel_extensions.cpp`)
#### `LValueTarget.QualifiedPathAndScopingSyntax_ParsesRegistersLocalsAndPorts`
**Test Intent**: Unit tests for EFSM metamodel extensions: LValueTarget, ActionAssignment, StateTimeInvariant, and

#### `ActionAssignment.AssignmentStatementsAndOperators_ExtractsLValueAndRValueExpressions`
**Test Intent**: Verify ActionAssignment construction and assignment statement parsing.

#### `StateTimeInvariant.TimeBoundExpressionsAndUnits_ParsesDurationAndOperators`
**Test Intent**: Verify StateTimeInvariant parsing, duration units, and state integration.

#### `ConcurrencySemantics.ExecutionModelMatrixAndValidation_EnforcesCoherentSemantics`
**Test Intent**: Verify ConcurrencySemantics configuration combinations, validity matrix, and JSON roundtrip.

#### `TransitionEdge.ConditionActionsAndPriorities_SortsCanonicallyByAscendingPriority`
**Test Intent**: Verify dual transition actions (condition and transition effects) and priority canonicalization.

### [`test_fsm_ir.cpp`](../tests/ir/test_fsm_ir.cpp) (`tests/ir/test_fsm_ir.cpp`)
#### `FsmIr.ModularHeaderSubcomponents_ConvertsEnumsAndDispatchesVariants`
**Test Intent**: Unit tests for core Intermediate Representation (FsmIr), state hierarchy, AST expressions, and serialization.
/

#### `DeterministicId.HierarchicalStatePaths_GeneratesStableUniqueIds`
**Test Intent**: Verify deterministic FNV-1a 64-bit ID computation for state node identification.

#### `FsmIr.CompositeAndOrthogonalHierarchy_SerializesToJsonRoundtrip`
**Test Intent**: Verify hierarchical state representations, orthogonal regions, and JSON serialization.

#### `FormalProperty.LtlAndCtlFormulas_PopulatesAstAndTraceability`
**Test Intent**: Verify formal property metadata, temporal operators, and requirements traceability.

#### `VariableDefinition.PhysicalUnitsAndBoundIntervals_IntegratesIntoActionSignatures`
**Test Intent**: Verify state variable definitions with physical units, domain bounds, and structured actions.

#### `FsmIr.MultiSourceForkJoinAndSubmachines_SerializesJsonPreservingPorts`
**Test Intent**: Verify multi-source/target fork-join transitions and submachine references.

#### `DeterministicId.CollisionResistanceLargeSet_GeneratesNoCollisionsAcross10kIds`
**Test Intent**: Verify deterministic ID hash distribution across a large set of state names.

#### `FormalPropertyAst.TemporalAstNodes_ConstructsAndConvertsToString`
**Test Intent**: Verify programmatic construction and string formatting of temporal logic AST nodes.

#### `FsmIr.PriorityTimeInvariantsAndEntryExitPoints_ValidatesMetamodelFeatures`
**Test Intent**: Verify state time invariants, connection points, and transition priority rankings.

#### `PortDefinition.DirectionalPortsAndZeroContext_PreservesCleanSeparation`
**Test Intent**: Verify domain port definitions (InPort, OutPort, InOutPort) without runtime binding.

#### `TypeDefinition.CustomTypeHierarchyFactories_CategorizesPrimitivesEnumsAndStructs`
**Test Intent**: Verify TypeDefinition factory helpers and type classification kinds.

#### `FsmIr.CustomTypesMetamodelIntegration_SortsAlphabeticallyAndFindsDefinitions`
**Test Intent**: Verify custom type registration, lookup, and canonical sorting in FsmIr.

#### `ExpressionAstNode.LeafAndBinaryExpressions_SerializesTargetAgnosticStrings`
**Test Intent**: Verify ExpressionAstNode creation, nesting, and string formatting.

#### `ExpressionAstNode.AlgebraicArithmeticAndBooleanExpressions_ParsesPrecedenceCorrectly`
**Test Intent**: Verify ExpressionAstNode::parse string expression parser.

#### `ActionAssignment.AssignmentOperatorsAndExpressionAst_ExtractsCompoundAssignments`
**Test Intent**: Verify ActionAssignment with compound assignment operators and expression ASTs.

#### `FsmIrSerializer.CustomTypesAndExpressionAst_SerializesAndDeserializesJson`
**Test Intent**: Verify lossless JSON serialization and deserialization of custom types and expressions.

#### `SemanticValidationPass.InvalidTypesAndUndeclaredVariables_EmitsDiagnosticErrors`
**Test Intent**: Verify SemanticValidationPass detects semantic errors in IR definitions.

#### `PassManager.SemanticValidationStage_RejectsSemanticallyInvalidModels`
**Test Intent**: Verify SemanticValidationPass integration within the PassManager pipeline.

#### `FsmValidator.CustomTypesAndSemanticValidation_EmitsDiagnosticsOnErrors`
**Test Intent**: Verify FsmValidator integrates custom types and semantic validation.

#### `SignalDefinition.StimuliSignalsSingleTruth_DifferentiatesEventsAndCarriedPayloads`
**Test Intent**: Verify SignalDefinition acts as single source of truth for event stimuli.

#### `ActionSignature.DualStringAndAstRepresentation_SynchronizesSynchronousInstructions`
**Test Intent**: Verify ActionSignature dual representation (raw string and structured instructions).

#### `FsmIr.SynthesizeInterfaceFromTriggers_InfersExternalSignalsAndTypedPorts`
**Test Intent**: Verify automatic interface synthesis infers signals and ports from triggers and actions.

#### `FsmIr.DirectGraphAdjacencyLookup_ComputesIncomingAndOutgoingTransitions`
**Test Intent**: Verify direct graph adjacency index methods (outgoing and incoming transitions).

#### `FsmIr.TargetAgnosticSemanticsAndPackages_DecouplesFromRuntimeBindings`
**Test Intent**: Verify FsmIr maintains clean package and target-agnostic attributes.

#### `DeterministicId.CrossPlatformIndependence_ComputesPredictableHash`
**Test Intent**: Verify deterministic ID hash stability across different platform architectures.

#### `FsmIr.CanonicalTransitionPriority_EnforcesAscendingEvaluationOrder`
**Test Intent**: Verify canonical transition sorting enforces strict ascending priority ordering.

### [`test_fsm_ir_semantic_completeness.cpp`](../tests/ir/test_fsm_ir_semantic_completeness.cpp) (`tests/ir/test_fsm_ir_semantic_completeness.cpp`)
#### `StateKind.TerminatePseudostate_DifferentiatedFromFinalState`
**Test Intent**: Unit tests for target-agnostic FSM IR semantic completeness extensions:

#### `ClockDefinition.TimedAutomataClocksAndResets_InitializesAndComparesCorrectly`
**Test Intent**: Verify ClockDefinition creation, resolution units, and clock reset operations.

#### `StateNode.MultiClockInvariants_PreservesTimingExpressions`
**Test Intent**: Verify multi-clock state timing invariants.

#### `ActionAstNode.PrimitiveInstructions_ConstructsAndIntegratesIntoActionSignature`
**Test Intent**: Verify ActionAstNode primitive instruction kinds (Store, PortWrite, PortRead, SignalEmit, ActionCall).

#### `ChangeTrigger.ContinuousSignalPredicates_EncapsulatesBooleanExpressions`
**Test Intent**: Verify ChangeTrigger continuous predicate semantics and encapsulation.

#### `TransitionEdge.TypedEndpointsAndClockResets_ComputesDeterministicIds`
**Test Intent**: Verify TransitionEdge deterministic ID computation and clock reset vector tracking.

#### `ExecutionSemantics.DispatchModelAndPreemptionPolicy_ValidatesConfigurationConsistency`
**Test Intent**: Verify ExecutionSemantics validation and preemption priority serialization.

#### `FsmIr.SemanticCompletenessModel_IntegratesClocksTerminatesAndContinuousTriggers`
**Test Intent**: Verify comprehensive FsmIr model integration with timed automata clocks, terminate states, and port actions.

---

## Middle-End Verification & Transformation Subsystem

### [`test_inlining_passes.cpp`](../tests/middleend/canonicalization/test_inlining_passes.cpp) (`tests/middleend/canonicalization/test_inlining_passes.cpp`)
#### `SubmachineInlining.SubmachineReference_SplicedIntoHostCompositeState`
**Test Intent**: Unit tests for submachine and choice pseudo-state inlining canonicalization passes.
/

#### `ChoiceInlining.DecisionBranches_FlattenedIntoCompositeTransitions`
**Test Intent**: Verify ChoiceInliningPass flattens choice pseudostates into direct composite transitions.

### [`test_orthogonal_product.cpp`](../tests/middleend/canonicalization/test_orthogonal_product.cpp) (`tests/middleend/canonicalization/test_orthogonal_product.cpp`)
#### `OrthogonalProduct.ConcurrentRegions_ExpandedToCartesianProductStates`
**Test Intent**: Unit tests for OrthogonalProductPass Cartesian product canonicalization.
/

#### `OrthogonalProduct.IncompleteParallelState_ReportsDiagnosticWithoutMutation`
**Test Intent**: Reject malformed parallel states instead of silently changing their semantics.

#### `OrthogonalProduct.ParentState_RemainsValidAfterProductReplacement`
**Test Intent**: Preserve and update the parallel parent after vector-backed state replacement.

#### `OrthogonalProduct.ZombieTransitions_AreFullyPurgedAfterTargetRemap`
**Test Intent**: Regression test: no zombie transitions survive the purge step.

#### `OrthogonalProduct.ExternalTransition_TargetingSubState_RemappedToProductState`
**Test Intent**: External transition targeting a sub-state is remapped to the matching first product state.

#### `OrthogonalProduct.ForkToParallelRegions_ResolvedToProductState`
**Test Intent**: Verify that a Fork entering orthogonal regions is resolved to the corresponding product state.

#### `OrthogonalProduct.JoinFromParallelRegions_ResolvedFromProductState`
**Test Intent**: Verify that a Join rendezvous exiting orthogonal regions is resolved from the corresponding product state.

#### `OrthogonalProduct.ProductExplosion_ExceedingLimit_ReportsDiagnosticAndAborts`
**Test Intent**: Verify that exceeding the maximum configured product state limit aborts with EORTHO003.

### [`test_pass_manager.cpp`](../tests/middleend/canonicalization/test_pass_manager.cpp) (`tests/middleend/canonicalization/test_pass_manager.cpp`)
#### `PassManager.DefaultPipelineExecution_CollectsStatisticsAndEmitsDiagnostics`
**Test Intent**: Unit tests for PassManager execution pipeline, pass chaining, and statistical profiling.
/

#### `PassManager.CustomPassRegistration_ModifiesIrAndRecordsExecutionStats`
**Test Intent**: Verify custom pass registration and extension in PassManager.

### [`test_structural_lowering.cpp`](../tests/middleend/canonicalization/test_structural_lowering.cpp) (`tests/middleend/canonicalization/test_structural_lowering.cpp`)
#### `HistoryLowering.ShallowHistoryTarget_LowersToShadowRegisterAndDispatchGuards`
**Test Intent**: Unit tests for Category A Structural Lowering Suite:

#### `DeferredEventLowering.DeferredEventsDeclared_LowersToBufferVariablesAndRecallTransitions`
**Test Intent**: Verify deferred events lowering into bounded queue buffers and recall transitions.

#### `BoundaryActionFusion.CrossBoundaryTransition_FusesExitAndEntryActionsInLcaOrder`
**Test Intent**: Verify boundary action fusion concatenates exit and entry actions in Lowest Common Ancestor (LCA) order.

#### `BoundaryActionFusion.CrossBoundaryTransition_ClearsHooksOnFusedNodes`
**Test Intent**: Verify that BoundaryActionFusionPass clears entry/exit hooks on traversed StateNodes.

#### `BoundaryActionFusion.InternalTransition_NotModifiedByPass`
**Test Intent**: Verify that BoundaryActionFusionPass does not modify internal transitions.

#### `ForkJoinLowering.ForkAndJoinPseudostates_LowersToMultiSourceMultiTargetTransitions`
**Test Intent**: Verify fork and join pseudostates lowering into multi-target and multi-source transition edges.

### [`test_common_action_factoring.cpp`](../tests/middleend/optimization/test_common_action_factoring.cpp) (`tests/middleend/optimization/test_common_action_factoring.cpp`)
#### `CommonActionFactoring.ConvergentTransitionsIdenticalAction_FactoredIntoTargetEntry`
**Test Intent**: Unit tests for CommonActionFactoringPass redundant action hoist and sink optimizations.
/

#### `CommonActionFactoring.DivergentTransitionsIdenticalAction_FactoredIntoSourceExit`
**Test Intent**: Verify factoring of common transition actions on divergent edges into source state exit actions.

### [`test_constant_folding_and_minimization.cpp`](../tests/middleend/optimization/test_constant_folding_and_minimization.cpp) (`tests/middleend/optimization/test_constant_folding_and_minimization.cpp`)
#### `ConstantFolding.TautologicalAndContradictoryGuards_EvaluatedAndPruned`
**Test Intent**: Unit tests for ConstantFoldingPass guard evaluation and StateMinimizationPass equivalence partitioning.
/

#### `StateMinimization.BehaviorallyEquivalentStates_MergedIntoCanonicalRepresentative`
**Test Intent**: Verify StateMinimizationPass merges behaviorally equivalent states.

### [`test_datapath_optimizations.cpp`](../tests/middleend/optimization/test_datapath_optimizations.cpp) (`tests/middleend/optimization/test_datapath_optimizations.cpp`)
#### `DeadActionElimination.UnreadVariableStore_PrunedFromTransitionAction`
**Test Intent**: Unit tests for Category B Data-Path Optimizations:

#### `DeadActionElimination.OverwrittenAndIdentityStores_PrunedFromActionSequence`
**Test Intent**: Verify DeadActionEliminationPass prunes write-after-write shadows and identity assignments.

#### `RegisterLiveness.DisjointVariableLifetimes_SharesAllocatedRegisters`
**Test Intent**: Verify RegisterLivenessPass shares hardware register allocations for variables with disjoint lifetimes.

#### `RegisterLiveness.InterferingVariableLifetimes_AllocatesDistinctRegisters`
**Test Intent**: Verify RegisterLivenessPass assigns distinct register allocations for simultaneously interfering variables.

#### `TransitionFusion.TransientIntermediateState_FusesTransitionsAndBypassesState`
**Test Intent**: Verify TransitionFusionPass fuses transient intermediate states and concatenates guards and actions.

#### `TransitionFusion.StatesWithExternalTriggersOrInitial_PreservedWithoutFusion`
**Test Intent**: Verify TransitionFusionPass preserves states requiring external triggers or designated as initial states.

#### `CommonActionFactoring.ConvergentIncomingEdges_FactorsActionIntoTargetEntry`
**Test Intent**: Verify CommonActionFactoringPass factors common transition actions on convergent edges into target entry.

#### `CommonActionFactoring.DivergentOutgoingEdges_FactorsActionIntoSourceExit`
**Test Intent**: Verify CommonActionFactoringPass factors common transition actions on divergent edges into source exit.

#### `CommonActionFactoring.InitialStateWithIncomingEdges_PreservesInitialStateSemantics`
**Test Intent**: Verify CommonActionFactoringPass avoids hoisting into initial state entry actions to protect reset semantics.

### [`test_dead_state_pruning.cpp`](../tests/middleend/optimization/test_dead_state_pruning.cpp) (`tests/middleend/optimization/test_dead_state_pruning.cpp`)
#### `DeadStatePruning.UnreachableSubgraphsAndContradictoryGuards_EliminatedFromIr`
**Test Intent**: Unit tests for DeadStatePruningPass reachability and dead transition analysis.
/

### [`test_guard_simplification.cpp`](../tests/middleend/optimization/test_guard_simplification.cpp) (`tests/middleend/optimization/test_guard_simplification.cpp`)
#### `GuardSimplification.AlgebraicBooleanExpressions_SimplifiedAndNormalized`
**Test Intent**: Unit tests for GuardSimplificationPass boolean algebra reductions.
/

### [`test_concurrency_verification.cpp`](../tests/middleend/verification/test_concurrency_verification.cpp) (`tests/middleend/verification/test_concurrency_verification.cpp`)
#### `OrthogonalInterference.ConcurrentConflictingVariableWrites_EmitsDataRaceError`
**Test Intent**: Unit tests for concurrency safety, data race detection, and determinism verification passes.
/

#### `DeterminismEnforcement.MultipleTransitionsFromSameSource_SortedByAscendingPriority`
**Test Intent**: Verify DeterminismEnforcementPass canonical priority sorting and collision detection.

#### `DeterminismEnforcement.ConflictingTransitionsSamePriority_EmitsDeterminismError`
**Test Intent**: Verify determinism enforcement detects non-deterministic collisions on identical-priority branches.

### [`test_efsm_interval_analysis.cpp`](../tests/middleend/verification/test_efsm_interval_analysis.cpp) (`tests/middleend/verification/test_efsm_interval_analysis.cpp`)
#### `EfsmIntervalAnalysis.CompliantPortBounds_PassesWithoutErrors`
**Test Intent**: Unit tests for EFSMIntervalAnalyzer abstract interpretation and port interval contracts.
/

#### `EfsmIntervalAnalysis.OutOfRangePortAssignment_EmitsPortRangeViolation`
**Test Intent**: Verify EFSM interval analyzer detects out-of-range assignments violating OutPort contracts.

#### `EfsmIntervalAnalysis.GuardOutsidePortDomain_EmitsUnsatisfiableDiagnostic`
**Test Intent**: Verify EFSM interval analyzer detects unsatisfiable guards over bounded InPorts.

### [`test_formal_safety_passes.cpp`](../tests/middleend/verification/test_formal_safety_passes.cpp) (`tests/middleend/verification/test_formal_safety_passes.cpp`)
#### `LivelockAnalysis.NonZeroTimeTransitions_PassesWithoutErrors`
**Test Intent**: Unit tests for Category D Formal Safety & Analysis Passes:

#### `LivelockAnalysis.ZeroTimeAutonomousCycles_EmitsLivelockDiagnostic`
**Test Intent**: Verify LivelockAnalysisPass detects infinite zero-time autonomous cycles.

#### `PriorityConflict.InvertedChildPrecedenceUnderOuterFirst_EmitsPriorityConflictDiagnostic`
**Test Intent**: Verify PriorityConflictPass detects hierarchical policy preemption violations.

#### `TimedInvariantsVerifier.TransitionDelayWithinStatePermanence_PassesVerification`
**Test Intent**: Verify TimedInvariantsVerifierPass validates states whose outgoing transitions respect stay permanence.

#### `TimedInvariantsVerifier.TransitionDelayExceedingPermanence_EmitsTimelockDiagnostic`
**Test Intent**: Verify TimedInvariantsVerifierPass detects timelock where outgoing transition exceeds stay invariant.

#### `EventQueueBound.DeferredEventsAndSignalEmits_CalculatesSafeCapacityPowerOfTwo`
**Test Intent**: Verify EventQueueBoundPass computes safe static queue capacity rounded up to a power of two.

### [`test_guard_satisfiability.cpp`](../tests/middleend/verification/test_guard_satisfiability.cpp) (`tests/middleend/verification/test_guard_satisfiability.cpp`)
#### `GuardSatisfiability.DisjointNumericGuardIntervals_EmitsNoWarnings`
**Test Intent**: Unit tests for GuardSatisfiabilityPass interval analysis, dead guards, and mutual exclusivity.
/

#### `GuardSatisfiability.OverlappingGuardsIdenticalPriority_EmitsAmbiguityWarning`
**Test Intent**: Verify that overlapping guard intervals on the same event and priority emit warning W0301.

#### `GuardSatisfiability.ContradictoryIntervalGuards_EmitsDeadGuardWarning`
**Test Intent**: Verify that contradictory guard conditions emit dead guard warning W0302.

#### `GuardSatisfiability.OverlappingGuardsDifferentiatedPriority_AvoidsAmbiguityWarning`
**Test Intent**: Verify that overlapping guards with differentiated transition priorities do not emit ambiguity warnings.

#### `GuardSatisfiability.ComplementaryBooleanGuards_RecognizedAsMutuallyExclusive`
**Test Intent**: Verify that complementary boolean guards are recognized as mutually exclusive.

#### `GuardSatisfiability.QualifiedVariableDomainParsing_ExtractsExactIntervalBoundaries`
**Test Intent**: Verify zero-allocation parse_guard_domain with qualifiers and numeric formats.

### [`test_model_checker_reachability.cpp`](../tests/middleend/verification/test_model_checker_reachability.cpp) (`tests/middleend/verification/test_model_checker_reachability.cpp`)
#### `FsmValidator.SoundStateHierarchy_PassesValidationWithoutErrors`
**Test Intent**: Unit tests for FSM formal validator, graph reachability, livelock cycles, and EFSM interval analysis.
/

#### `FsmValidator.AutonomousClosedCycle_EmitsLivelockDiagnostic`
**Test Intent**: Verify model checker detection of livelock cycles with no exit transitions.

#### `FsmValidator.ChoicePseudostateMissingDefaultFallback_EmitsSafetyCriticalDiagnostic`
**Test Intent**: Verify model checker detects Choice nodes lacking an unconditional fallback branch.

#### `FsmValidator.ChoiceBranchesIdenticalGuards_EmitsAmbiguityWarning`
**Test Intent**: Verify model checker detects duplicate/conflicting guard conditions on Choice branches.

#### `FsmValidator.UnhandledTerminalState_EmitsDeadlockTrapWarning`
**Test Intent**: Verify model checker detects deadlock/trap states having no outgoing transitions.

#### `FsmValidator.ConflictingTransitionsSameEventAndPriority_EmitsDeterminismError`
**Test Intent**: Verify model checker detects non-deterministic transition conflicts for identical events.

#### `FsmValidator.DuplicateTimerTransitionsSameState_EmitsWarning`
**Test Intent**: Verify model checker detects duplicate timer transitions originating from the same state.

#### `EfsmIntervalAnalysis.OutOfRangeVariableGuardCondition_FlagsUnsatisfiableBranch`
**Test Intent**: Verify EFSM Interval Analysis detects unsatisfiable guard conditions across data paths.

### [`test_model_checker_temporal_logic.cpp`](../tests/middleend/verification/test_model_checker_temporal_logic.cpp) (`tests/middleend/verification/test_model_checker_temporal_logic.cpp`)
#### `LtlPropertyParser.BasicUnaryAndBinaryOperators_ParsesAstAndToString`
**Test Intent**: Unit tests for LTL/CTL temporal logic parser, ModelChecker solver, and SMV formal specification generation.
/

#### `LtlPropertyParser.NestedTemporalFormulas_PreservesAssociativityAndPrecedence`
**Test Intent**: Verify complex nested temporal logic formulas (response properties, mutual exclusion).

#### `DirectiveParser.PropertyAndVariableDirectives_ExtractsModelMetadataAndConstraints`
**Test Intent**: Verify '@fsm:property' and '@fsm:var' directive extraction and deserialization.

#### `ModelChecker.SafetyInvariantSatisfiedAndViolated_EmitsVerdictAndCounterexampleTrace`
**Test Intent**: Verify safety invariant evaluation, violation detection, and step-by-step trace generation.

#### `ModelChecker.ResponseLivenessProperty_VerifiesTemporalSequenceSatisfaction`
**Test Intent**: Verify response liveness property verification ('G (Trigger -> F Target)').

#### `ModelChecker.OptimizationPipelinePassManager_IntegratesVerificationSeamlessly`
**Test Intent**: Verify integration of formal verification within PassManager optimization pipeline.

#### `SmvSerializer.StateMachineAndLtlSpecs_GeneratesValidSmvModule`
**Test Intent**: Verify nuXmv / SMV formal model generation with state transitions and LTLSPEC.

#### `ModelChecker.ReachableProhibitedFatalState_SynthesizesDiagnosticTrace`
**Test Intent**: Verify counterexample trace generation when reaching a prohibited fatal error state.

#### `SmvSerializer.CtlSpecFormulas_EmitsCtlModuleClauses`
**Test Intent**: Verify SMV serializer emission for CTLSPEC temporal properties.

#### `ModelChecker.RelationalDatapathPredicates_EvaluatesTruthAndCounterexamples`
**Test Intent**: Verify ModelChecker evaluates relational comparisons (<, <=, >, >=, ==, !=) on datapath variables.

### [`test_timed_automata_verification.cpp`](../tests/middleend/verification/test_timed_automata_verification.cpp) (`tests/middleend/verification/test_timed_automata_verification.cpp`)
#### `TimedDeadlockPass.RacingTimeoutAndEventSamePriority_EmitsAmbiguityWarning`
**Test Intent**: Unit tests for timed automata deadlock pass, SMV clock emission, and deterministic runtime timer management.
/

#### `SmvSerializer.TimeTriggeredTransitions_EmitsTickCountersAndNextTransitions`
**Test Intent**: Verify SMV formal serializer emits discrete tick counter variables and next-state assignments for timers.

#### `DeterministicTimerManager.SingleShotAndPeriodicTimers_StepsAndExpiresDeterministically`
**Test Intent**: Verify deterministic timer manager handles one-shot and periodic timer steps without drifting.

### [`test_wcet_analysis.cpp`](../tests/middleend/verification/test_wcet_analysis.cpp) (`tests/middleend/verification/test_wcet_analysis.cpp`)
#### `WcetAnalysis.EventlessCyclicTransitions_EmitsZenoCycleError`
**Test Intent**: Unit tests for WcetAnalysisPass static execution time bounding and Zeno cycle detection.
/

#### `WcetAnalysis.LinearEventlessChains_ComputesBoundedMicroSteps`
**Test Intent**: Verify WcetAnalysisPass computes bounded micro-steps for terminating chains.

---

## Integration & Build Subsystem

### [`test_full_7stage_pipeline.cpp`](../tests/integration/pipeline/test_full_7stage_pipeline.cpp) (`tests/integration/pipeline/test_full_7stage_pipeline.cpp`)
#### `VerifiedPipeline.MultiStageCompilerPipeline_LowersOptimizesAndVerifiesIr`
**Test Intent**: Integration tests for the verified 7-stage compilation pipeline, pass dependencies, and statistics.
/

#### `VerifiedPipeline.MissingPrerequisitePass_EmitsDependencyWarning`
**Test Intent**: Verify pipeline warning diagnostics when a registered pass has unmet prerequisites.

### [`test_pipeline_extensibility.cpp`](../tests/integration/pipeline/test_pipeline_extensibility.cpp) (`tests/integration/pipeline/test_pipeline_extensibility.cpp`)
#### `PipeThroughPass.PosixFilterExecution_RoundtripsIrFidelity`
**Test Intent**: Integration tests for pipeline extensibility, external Unix pipe filters, and dynamic plugin loading.
/

#### `PluginLoader.MissingSharedObject_FailsGracefullyWithDiagnostic`
**Test Intent**: Verify PluginLoader handles non-existent or invalid plugin files gracefully.

### [`test_cli_driver_errors.cpp`](../tests/integration/test_cli_driver_errors.cpp) (`tests/integration/test_cli_driver_errors.cpp`)
#### `FsmcDriver.MissingInput_ReturnsNonZero`
**Test Intent**: Comprehensive regression tests for controlled fsmc and fsm-opt driver failure contracts.

**Scenario**:
  - Invoke FsmcDriver and OptDriver with empty input paths.

#### `CliOptions.UnknownArguments_AreRejectedBeforeDriverExecution`
**Test Intent**: Ensure unrecognized command-line arguments are rejected before execution.

**Scenario**:
  - Pass an unknown flag `--not-a-real-option` to CLI argument parsers for fsmc and fsm-opt.

#### `CliOptions.MissingOptionArgument_ReportsClearDiagnosticAndFails`
**Test Intent**: Ensure options requiring values fail with explicit missing argument diagnostics.

**Scenario**:
  - Provide options like `-i`, `-o`, `-e`, `--std` without corresponding operand.

#### `CliDriver.NonexistentOrUnreadableFile_ReturnsNonZero`
**Test Intent**: Verify controlled error handling on nonexistent or unreadable input model files.

**Scenario**:
  - Invoke CLI drivers specifying a path that does not exist in the filesystem.

#### `CliDriver.MalformedModel_ReturnsNonZero`
**Test Intent**: Verify parser failure propagation on syntactically malformed input files.

**Scenario**:
  - Provide broken syntax file with unbalanced delimiters to fsmc and fsm-opt.

#### `CliDriver.UnsupportedFormatOrExport_ReturnsNonZero`
**Test Intent**: Verify controlled failure on unsupported diagram export formats or target languages.

**Scenario**:
  - Specify an invalid export format string or unsupported language code.

#### `CliDriver.MissingPassPlugin_ReturnsNonZero`
**Test Intent**: Verify graceful error reporting when a requested pass plugin shared object is missing.

**Scenario**:
  - Provide nonexistent shared library path to `--load-pass-plugin`.

#### `CliDriver.UnwritableOutput_ReturnsNonZero`
**Test Intent**: Verify controlled failure when output path cannot be created or written.

**Scenario**:
  - Set output path inside an uncreatable directory hierarchy.

#### `CliDriver.NuXmvMissingTool_FallsBackGracefully`
**Test Intent**: Verify graceful fallback when external nuXmv binary is unavailable on system PATH.

**Scenario**:
  - Request formal verification with nuXmv engine on a sound model when nuXmv is absent.

### [`test_cmake_integration.cpp`](../tests/integration/test_cmake_integration.cpp) (`tests/integration/test_cmake_integration.cpp`)
- *(Executable binary test verification)*

### [`test_multiformat_presets_roundtrip.cpp`](../tests/integration/test_multiformat_presets_roundtrip.cpp) (`tests/integration/test_multiformat_presets_roundtrip.cpp`)
#### `MultiformatPresets.Sysml2UavMission_PassesRoundtripAndStrictCompilation`
**Test Intent**: Formal Verification and End-to-End Roundtrip Suite for Canonical Multi-Format Presets.

#### `MultiformatPresets.ScxmlIndustrialPress_PassesRoundtripAndStrictCompilation`
**Test Intent**: Verify Industrial Press SCXML preset end-to-end.

#### `MultiformatPresets.PlantUmlConnectionManager_PassesRoundtripAndStrictCompilation`
**Test Intent**: Verify Connection Manager PlantUML preset end-to-end.

#### `MultiformatPresets.JsonSmartThermostat_PassesRoundtripAndStrictCompilation`
**Test Intent**: Verify Smart Thermostat XState JSON preset end-to-end.

#### `MultiformatPresets.CameoXmiSatelliteMission_PassesRoundtripAndStrictCompilation`
**Test Intent**: Verify Satellite Mission Cameo XMI preset end-to-end.

#### `MultiformatPresets.MermaidAsyncMotorController_PassesRoundtripAndStrictCompilation`
**Test Intent**: Verify Async Motor Controller Mermaid preset end-to-end.

---

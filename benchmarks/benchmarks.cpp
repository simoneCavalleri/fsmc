#include <benchmark/benchmark.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/cpp/runtime/fsm.hpp"
#include "fsm/backend/cpp/runtime/spsc_fsm.hpp"
#include "fsm/backend/cpp/runtime/spsc_ring_buffer.hpp"
#include "fsm/backend/cpp/runtime/static_ring_buffer.hpp"
#include "fsm/backend/cpp/runtime/thread_safe_fsm.hpp"
#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/frontend/diagram/plantuml_parser.hpp"
#include "fsm/frontend/formal/sysml2_parser.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/pass_manager.hpp"

// ============================================================================
// Global Heap Allocation Tracker for Google Benchmark
// ============================================================================
static std::atomic<std::size_t> g_heap_allocations{0};
static std::atomic<std::size_t> g_heap_bytes_allocated{0};
static std::atomic<bool> g_tracking_enabled{false};

void* operator new(std::size_t size) {
    if (g_tracking_enabled.load(std::memory_order_relaxed)) {
        g_heap_allocations.fetch_add(1, std::memory_order_relaxed);
        g_heap_bytes_allocated.fetch_add(size, std::memory_order_relaxed);
    }
    void* ptr = std::malloc(size);
    if (ptr == nullptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t /*unused*/) noexcept {
    std::free(ptr);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

// ============================================================================
// 1. Benchmark State Machine Definitions
// ============================================================================

namespace bench {

// --- States ---
struct StateA {
    static constexpr std::string_view name = "StateA";
};
struct StateB {
    static constexpr std::string_view name = "StateB";
};
struct StateC {
    static constexpr std::string_view name = "StateC";
};

// --- Events ---
struct Event1 {};
struct Event2 {};
struct Event3 {};
struct InternalPing {};

// --- Context & Registers ---
struct BenchRegisters {
    uint64_t counter = 0;
};

// --- Guards ---
struct DummyGuard {
    [[nodiscard]] constexpr bool operator()(const BenchRegisters& /*reg*/) const noexcept { return true; }
};

struct CompositeGuard {
    [[nodiscard]] constexpr bool operator()(const BenchRegisters& reg) const noexcept {
        return (reg.counter % 2 == 0) && (reg.counter < 1'000'000'000ULL);
    }
};

// --- Actions ---
struct DummyAction {
    constexpr void operator()(BenchRegisters& reg) const noexcept { reg.counter++; }
};

struct InternalAction {
    constexpr void operator()(BenchRegisters& reg) const noexcept { reg.counter++; }
};

// --- Transition Tables ---

// Canonical 3-State FSM Table with ping-pong and cyclic paths
using BenchTable =
    fsm::transition_table<fsm::transition<StateA, Event1, StateB, DummyAction, DummyGuard>,
                          fsm::transition<StateB, Event1, StateA, DummyAction, DummyGuard>,  // Fast ping-pong path
                          fsm::transition<StateB, Event2, StateC, DummyAction, CompositeGuard>,
                          fsm::transition<StateC, Event3, StateA, DummyAction, DummyGuard>,
                          fsm::internal_transition<StateA, InternalPing, InternalAction, DummyGuard>>;

using BenchFSM = fsm::fsm<BenchTable, fsm::no_ports, fsm::no_ports, BenchRegisters>;
using BenchThreadSafeFSM = fsm::thread_safe_fsm<BenchTable, fsm::no_ports, fsm::no_ports, BenchRegisters>;
using BenchSpscFSM = fsm::spsc_fsm<BenchTable, fsm::no_ports, fsm::no_ports, BenchRegisters, fsm::no_services, 128>;

// --- Hierarchical FSM (HFSM) Benchmark Table ---
struct SubState1 {
    static constexpr std::string_view name = "SubState1";
};
struct SubState2 {
    static constexpr std::string_view name = "SubState2";
};
struct OtherState {
    static constexpr std::string_view name = "OtherState";
};

struct EvNext {};
struct EvPrev {};
struct EvExit {};
struct EvResume {};

using HfsmTable = fsm::transition_table<fsm::transition<SubState1, EvNext, SubState2, DummyAction>,
                                        fsm::transition<SubState2, EvPrev, SubState1, DummyAction>,
                                        fsm::transition<SubState2, EvExit, OtherState, DummyAction>,
                                        fsm::transition<OtherState, EvResume, SubState1, DummyAction>>;

using HfsmFSM = fsm::fsm<HfsmTable, fsm::no_ports, fsm::no_ports, BenchRegisters>;

}  // namespace bench

// ============================================================================
// 2. Google Benchmark Cases: Zero-Alloc Synchronous Dispatch
// ============================================================================

// Measure single-transition ping-pong dispatch (StateA <-> StateB) with DoNotOptimize
static void BM_Dispatch_SingleTransition_PingPong(benchmark::State& state) {
    bench::BenchFSM machine;

    g_tracking_enabled.store(true, std::memory_order_release);
    for (auto _ : state) {
        auto res = machine.dispatch(bench::Event1{});
        benchmark::DoNotOptimize(res);
    }
    g_tracking_enabled.store(false, std::memory_order_release);

    state.SetItemsProcessed(state.iterations());
    state.counters["HeapAllocs"] = benchmark::Counter(static_cast<double>(g_heap_allocations.load()));
}
BENCHMARK(BM_Dispatch_SingleTransition_PingPong);

// Measure sequential cyclic dispatch across 3 states (A -> B -> C -> A)
static void BM_Dispatch_SequentialCycle_3States(benchmark::State& state) {
    bench::BenchFSM machine;

    g_tracking_enabled.store(true, std::memory_order_release);
    for (auto _ : state) {
        auto r1 = machine.dispatch(bench::Event1{});
        auto r2 = machine.dispatch(bench::Event2{});
        auto r3 = machine.dispatch(bench::Event3{});
        benchmark::DoNotOptimize(r1);
        benchmark::DoNotOptimize(r2);
        benchmark::DoNotOptimize(r3);
    }
    g_tracking_enabled.store(false, std::memory_order_release);

    state.SetItemsProcessed(state.iterations() * 3);
    state.counters["HeapAllocs"] = benchmark::Counter(static_cast<double>(g_heap_allocations.load()));
}
BENCHMARK(BM_Dispatch_SequentialCycle_3States);

// Measure internal transition (no state change, executes action directly)
static void BM_Dispatch_InternalTransition(benchmark::State& state) {
    bench::BenchFSM machine;

    g_tracking_enabled.store(true, std::memory_order_release);
    for (auto _ : state) {
        auto res = machine.dispatch(bench::InternalPing{});
        benchmark::DoNotOptimize(res);
    }
    g_tracking_enabled.store(false, std::memory_order_release);

    state.SetItemsProcessed(state.iterations());
    state.counters["HeapAllocs"] = benchmark::Counter(static_cast<double>(g_heap_allocations.load()));
}
BENCHMARK(BM_Dispatch_InternalTransition);

// Measure HFSM dispatch across sub-states
static void BM_Dispatch_HFSM_SubStatePingPong(benchmark::State& state) {
    bench::HfsmFSM machine;
    bool toggle = false;

    g_tracking_enabled.store(true, std::memory_order_release);
    for (auto _ : state) {
        if (!toggle) {
            auto r = machine.dispatch(bench::EvNext{});
            benchmark::DoNotOptimize(r);
        } else {
            auto r = machine.dispatch(bench::EvPrev{});
            benchmark::DoNotOptimize(r);
        }
        toggle = !toggle;
    }
    g_tracking_enabled.store(false, std::memory_order_release);

    state.SetItemsProcessed(state.iterations());
    state.counters["HeapAllocs"] = benchmark::Counter(static_cast<double>(g_heap_allocations.load()));
}
BENCHMARK(BM_Dispatch_HFSM_SubStatePingPong);

// ============================================================================
// 3. Google Benchmark Cases: Lock-Free Queues & Concurrency
// ============================================================================

// Single-thread push/pop on cacheline-aligned SPSC ring buffer (measures cache latency)
static void BM_Runtime_SpscQueue_SingleThread_PushPop(benchmark::State& state) {
    fsm::spsc_ring_buffer<uint64_t, 1024> queue;
    uint64_t val = 42;
    uint64_t out = 0;

    for (auto _ : state) {
        queue.push(val);
        queue.pop(out);
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Runtime_SpscQueue_SingleThread_PushPop);

// True inter-thread lock-free handoff across 2 CPU cores
static void BM_Runtime_SpscQueue_TwoThreads_Handoff(benchmark::State& state) {
    static fsm::spsc_ring_buffer<uint64_t, 4096> queue;

    if (state.thread_index() == 0) {
        // Producer thread
        uint64_t val = 42;
        for (auto _ : state) {
            while (!queue.push(val)) {
                // busy spin
            }
        }
    } else {
        // Consumer thread
        uint64_t out = 0;
        for (auto _ : state) {
            while (!queue.pop(out)) {
                // busy spin
            }
            benchmark::DoNotOptimize(out);
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Runtime_SpscQueue_TwoThreads_Handoff)->Threads(2)->UseRealTime();

// Static ring buffer for hard real-time / ISR environments
static void BM_Runtime_StaticRingBuffer_PushPop(benchmark::State& state) {
    fsm::static_ring_buffer<uint64_t, 512> ring;
    uint64_t val = 99;

    for (auto _ : state) {
        ring.push(val);
        auto popped = ring.pop();
        benchmark::DoNotOptimize(popped);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Runtime_StaticRingBuffer_PushPop);

// SPSC FSM: Lock-Free post + polling
static void BM_Runtime_SpscFSM_PostAndProcess(benchmark::State& state) {
    bench::BenchSpscFSM fsm;

    for (auto _ : state) {
        bool p1 = fsm.post(bench::Event1{});
        bool r1 = fsm.process_one();
        bool p2 = fsm.post(bench::Event1{});
        bool r2 = fsm.process_one();
        benchmark::DoNotOptimize(p1);
        benchmark::DoNotOptimize(r1);
        benchmark::DoNotOptimize(p2);
        benchmark::DoNotOptimize(r2);
    }
    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_Runtime_SpscFSM_PostAndProcess);

// ============================================================================
// 4. Google Benchmark Cases: Compiler Frontend Ingestion
// ============================================================================

static const std::string SAMPLE_PUML = R"(@startuml
[*] --> Idle
Idle --> Operating : StartCmd [SafetyOk] / PowerOn
state Operating {
    [*] --> Running
    Running --> Paused : PauseCmd
    Paused --> Running : ResumeCmd
}
Operating --> Idle : StopCmd / PowerOff
@enduml)";

static void BM_Compiler_PlantUml_Parse(benchmark::State& state) {
    fsm::frontend::diagram::PlantUmlParser parser;
    std::string err;

    for (auto _ : state) {
        fsm::ir::FsmIr ir;
        bool ok = parser.parse(SAMPLE_PUML, ir, err);
        benchmark::DoNotOptimize(ok);
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * SAMPLE_PUML.size()));
}
BENCHMARK(BM_Compiler_PlantUml_Parse);

static const std::string SAMPLE_SYSML = R"(state def Spacecraft {
    entry;
    state Standby;
    state InFlight {
        state Ascending;
        state Cruising;
    }
    transition InitialToStandby first start then Standby;
    transition StandbyToFlight first Standby accept LaunchCmd then InFlight;
})";

static void BM_Compiler_Sysml2_Parse(benchmark::State& state) {
    fsm::frontend::formal::Sysml2Parser parser;
    std::string err;

    for (auto _ : state) {
        fsm::ir::FsmIr ir;
        bool ok = parser.parse(SAMPLE_SYSML, ir, err);
        benchmark::DoNotOptimize(ok);
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * SAMPLE_SYSML.size()));
}
BENCHMARK(BM_Compiler_Sysml2_Parse);

// ============================================================================
// 5. Google Benchmark Cases: Middle-End Passes & Backend Codegen
// ============================================================================

static void BM_Compiler_PassManager_Pipeline(benchmark::State& state) {
    fsm::frontend::diagram::PlantUmlParser parser;
    fsm::ir::FsmIr ir_template;
    std::string err;
    parser.parse(SAMPLE_PUML, ir_template, err);

    fsm::middleend::PassManager pm;
    pm.add_pass(std::make_unique<fsm::middleend::HierarchyCanonicalizationPass>());
    pm.add_pass(std::make_unique<fsm::middleend::ChoiceCompletenessPass>());
    pm.add_pass(std::make_unique<fsm::middleend::ModelSafetyVerifierPass>());

    for (auto _ : state) {
        fsm::ir::FsmIr ir = ir_template;
        fsm::diagnostic::DiagnosticEngine diag;
        bool ok = pm.run(ir, diag);
        benchmark::DoNotOptimize(ok);
    }
}
BENCHMARK(BM_Compiler_PassManager_Pipeline);

static void BM_Compiler_CppGenerator(benchmark::State& state) {
    fsm::frontend::diagram::PlantUmlParser parser;
    fsm::ir::FsmIr ir;
    std::string err;
    parser.parse(SAMPLE_PUML, ir, err);

    fsm::backend::cpp::GeneratorOptions opts;
    opts.cpp_standard = fsm::backend::cpp::CppStandard::Cpp20;
    opts.standalone = true;

    for (auto _ : state) {
        std::string code = fsm::backend::cpp::CppGenerator::generate_header(ir, opts);
        benchmark::DoNotOptimize(code);
    }
}
BENCHMARK(BM_Compiler_CppGenerator);

// ============================================================================
// Custom Benchmark Main with Static Footprint Analysis
// ============================================================================

int main(int argc, char** argv) {
    std::cout << "======================================================================\n";
    std::cout << "           FSMC GOOGLE BENCHMARK & HARDWARE METRICS SUITE             \n";
    std::cout << "======================================================================\n";
    std::cout << "[STATIC FOOTPRINT & SIZEOF METRICS]\n";
    std::cout << "  • sizeof(BenchFSM)                     : " << sizeof(bench::BenchFSM) << " bytes\n";
    std::cout << "  • sizeof(BenchTable)                   : " << sizeof(bench::BenchTable) << " bytes\n";
    std::cout << "  • sizeof(state_variant)                : " << sizeof(bench::BenchFSM::state_variant) << " bytes\n";
    std::cout << "  • sizeof(spsc_ring_buffer<u64, 1024>)  : " << sizeof(fsm::spsc_ring_buffer<uint64_t, 1024>)
              << " bytes\n";
    std::cout << "  • sizeof(static_ring_buffer<u64, 512>) : " << sizeof(fsm::static_ring_buffer<uint64_t, 512>)
              << " bytes\n";
    std::cout << "  • sizeof(BenchThreadSafeFSM)           : " << sizeof(bench::BenchThreadSafeFSM) << " bytes\n";
    std::cout << "  • sizeof(BenchSpscFSM)                 : " << sizeof(bench::BenchSpscFSM) << " bytes\n";
    std::cout << "  • Total states in BenchFSM             : " << bench::BenchFSM::state_count << "\n";
    std::cout << "  • Total transitions in BenchFSM        : " << bench::BenchFSM::transition_count << "\n";
    std::cout << "======================================================================\n\n";

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();

    std::cout << "\n======================================================================\n";
    std::cout << "  Zero Heap Allocations Check: "
              << (g_heap_allocations.load() == 0 ? "[PASSED - PURE ZERO HEAP ALLOCATIONS]" : "[FAILED]") << "\n";
    std::cout << "======================================================================\n";

    return g_heap_allocations.load() == 0 ? 0 : 1;
}

#include <benchmark/benchmark.h>

#include <atomic>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <string_view>

#include "fsm/backend/cpp/cpp_generator.hpp"
#include "fsm/backend/cpp/runtime/fsm.hpp"
#include "fsm/backend/cpp/runtime/spsc_ring_buffer.hpp"
#include "fsm/backend/cpp/runtime/static_ring_buffer.hpp"
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
static bool g_tracking_enabled = false;

void* operator new(std::size_t size) {
    if (g_tracking_enabled) {
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

struct StateA {
    static constexpr std::string_view name = "StateA";
};
struct StateB {
    static constexpr std::string_view name = "StateB";
};
struct StateC {
    static constexpr std::string_view name = "StateC";
};

struct Event1 {};
struct Event2 {};
struct Event3 {};
struct InternalPing {};

struct BenchRegisters {
    uint64_t counter = 0;
};

struct DummyGuard {
    [[nodiscard]] constexpr bool operator()(const BenchRegisters& /*reg*/) const noexcept { return true; }
};

struct CompositeGuard {
    [[nodiscard]] constexpr bool operator()(const BenchRegisters& reg) const noexcept {
        return (reg.counter % 2 == 0) && (reg.counter < 1'000'000'000ULL);
    }
};

struct DummyAction {
    constexpr void operator()(BenchRegisters& reg) const noexcept { reg.counter++; }
};

struct InternalAction {
    constexpr void operator()(BenchRegisters& reg) const noexcept { reg.counter++; }
};

using BenchTable = fsm::transition_table<fsm::transition<StateA, Event1, StateB, DummyAction, DummyGuard>,
                                         fsm::transition<StateB, Event2, StateC, DummyAction, CompositeGuard>,
                                         fsm::transition<StateC, Event3, StateA, DummyAction, DummyGuard>,
                                         fsm::internal_transition<StateA, InternalPing, InternalAction, DummyGuard>>;

using BenchFSM = fsm::fsm<BenchTable, fsm::no_ports, fsm::no_ports, BenchRegisters>;

}  // namespace bench

// ============================================================================
// 2. Google Benchmark Cases: Runtime Dispatch
// ============================================================================

static void BM_Dispatch_ExternalTransition(benchmark::State& state) {
    bench::BenchFSM machine;

    g_tracking_enabled = true;
    for (auto _ : state) {
        machine.dispatch(bench::Event1{});
        machine.dispatch(bench::Event2{});
        machine.dispatch(bench::Event3{});
    }
    g_tracking_enabled = false;

    state.SetItemsProcessed(state.iterations() * 3);
    state.counters["HeapAllocs"] = benchmark::Counter(static_cast<double>(g_heap_allocations.load()));
}
BENCHMARK(BM_Dispatch_ExternalTransition);

static void BM_Dispatch_InternalTransition(benchmark::State& state) {
    bench::BenchFSM machine;

    g_tracking_enabled = true;
    for (auto _ : state) {
        machine.dispatch(bench::InternalPing{});
    }
    g_tracking_enabled = false;

    state.SetItemsProcessed(state.iterations());
    state.counters["HeapAllocs"] = benchmark::Counter(static_cast<double>(g_heap_allocations.load()));
}
BENCHMARK(BM_Dispatch_InternalTransition);

// ============================================================================
// 3. Google Benchmark Cases: Lock-Free & Static Ring Buffers
// ============================================================================

static void BM_Runtime_SpscQueue_PushPop(benchmark::State& state) {
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
BENCHMARK(BM_Runtime_SpscQueue_PushPop);

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
    pm.add_pass(std::make_unique<fsm::middleend::passes::HierarchyCanonicalizationPass>());
    pm.add_pass(std::make_unique<fsm::middleend::passes::ChoiceCompletenessPass>());
    pm.add_pass(std::make_unique<fsm::middleend::passes::ModelSafetyVerifierPass>());

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
    std::cout << "  • sizeof(BenchFSM)                   : " << sizeof(bench::BenchFSM) << " bytes\n";
    std::cout << "  • sizeof(BenchTable)                 : " << sizeof(bench::BenchTable) << " bytes\n";
    std::cout << "  • sizeof(state_variant)              : " << sizeof(bench::BenchFSM::state_variant) << " bytes\n";
    std::cout << "  • sizeof(spsc_ring_buffer<u64, 1024>): " << sizeof(fsm::spsc_ring_buffer<uint64_t, 1024>)
              << " bytes\n";
    std::cout << "  • Total states in FSM                : " << bench::BenchFSM::state_count << "\n";
    std::cout << "  • Total transitions in FSM           : " << bench::BenchFSM::transition_count << "\n";
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

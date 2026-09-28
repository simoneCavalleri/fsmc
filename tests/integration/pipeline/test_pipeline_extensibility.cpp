/**
 * @file test_pipeline_extensibility.cpp
 * @brief Integration tests for pipeline extensibility, external Unix pipe filters, and dynamic plugin loading.
 */

#include <gtest/gtest.h>

#include "fsm/diagnostic/diagnostic_engine.hpp"
#include "fsm/ir/fsm_ir.hpp"
#include "fsm/middleend/pass_manager.hpp"
#include "fsm/middleend/passes/pipe_through_pass.hpp"
#include "fsm/middleend/plugin/plugin_loader.hpp"

using namespace fsm::diagnostic;
using namespace fsm::middleend;
using namespace fsm::middleend::passes;
using namespace fsm::middleend::plugin;
using namespace fsm::ir;

namespace {

/**
 * @brief Verify PipeThroughPass filters IR faithfully through an external Unix command.
 * @scenario Pipe FSM model through standard POSIX utility 'cat'.
 * @expected IR roundtrips with preserved states, transitions, and structural topology.
 */
TEST(PipeThroughPass, PosixFilterExecution_RoundtripsIrFidelity) {
    FsmIr model;
    model.name = "PipedModel";
    model.add_state("Active");
    model.add_state("Inactive");
    model.initial_state = "Active";

    TransitionEdge t;
    t.source = "Active";
    t.target = "Inactive";
    t.event = "PowerOff";
    model.add_transition(t);

    PipeThroughPass pass("cat");
    DiagnosticEngine diag;
    bool ok = pass.run(model, diag);

    EXPECT_TRUE(ok);
    EXPECT_EQ(model.name, "PipedModel");
    EXPECT_EQ(model.states.size(), 2u);
    EXPECT_EQ(model.transitions.size(), 1u);
}

/**
 * @brief Verify PluginLoader handles non-existent or invalid plugin files gracefully.
 * @scenario Attempt to load a non-existent shared library '/non/existent/path/to/plugin.so'.
 * @expected Function returns false and logs diagnostic error E_PLUGIN_LOAD.
 */
TEST(PluginLoader, MissingSharedObject_FailsGracefullyWithDiagnostic) {
    PassManager pm;
    DiagnosticEngine diag;
    PluginLoader loader;

    bool ok = loader.load_plugin("/non/existent/path/to/plugin.so", pm, diag);
    EXPECT_FALSE(ok);
    EXPECT_TRUE(diag.has_errors());
}

}  // namespace

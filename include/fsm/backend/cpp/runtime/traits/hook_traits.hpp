#pragma once

#include "fsm/backend/cpp/runtime/traits/action_traits.hpp"
#include "fsm/backend/cpp/runtime/traits/guard_traits.hpp"
#include "fsm/backend/cpp/runtime/traits/lifecycle_traits.hpp"

namespace fsm {

// ============================================================================
// Detection Idiom & Hook Invocations (on_enter, on_exit, guard, action)
// Partitioned Domain Model: InPorts (const &), OutPorts (&), Registers (&), Services (&)
//
// Note: This umbrella header aggregates modular sub-headers:
// - lifecycle_traits.hpp (on_enter, on_exit)
// - guard_traits.hpp     (guard fallbacks & call_guard)
// - action_traits.hpp    (action fallbacks & call_action)
// ============================================================================

}  // namespace fsm

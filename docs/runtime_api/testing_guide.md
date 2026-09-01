# Unit Testing Guide

This guide demonstrates how to write robust, deterministic unit tests for `fsmc` state machines using **GoogleTest** and **Catch2**.

Because `fsmc` state machines are stack-allocated, zero-heap, and side-effect isolated via the 4-domain datapath (`InPorts`, `OutPorts`, `Registers`, `Services`), unit testing follows the classic, clean **Arrange-Act-Assert (AAA)** pattern without requiring complex test fixtures.

---

## 1. GoogleTest Recipes

### Complete Unit Test Suite

```cpp
#include <gtest/gtest.h>
#include "fsm/backend/cpp/runtime/fsm.hpp"

// ----------------------------------------------------------------------------
// Domain Definitions Under Test
// ----------------------------------------------------------------------------
struct Idle    { static constexpr std::string_view name = "Idle";    };
struct Running { static constexpr std::string_view name = "Running"; };

struct EvStart {};
struct EvStop  {};

struct DroneInPorts   { float battery_percent = 100.0f; };
struct DroneOutPorts  { bool motor_enable = false;      };
struct DroneRegisters { std::uint32_t start_count = 0;  };

struct BatteryCheckGuard {
    bool operator()(const DroneInPorts& in) const noexcept {
        return in.battery_percent >= 20.0f;
    }
};

struct ArmAction {
    void operator()(DroneOutPorts& out, DroneRegisters& reg) const noexcept {
        out.motor_enable = true;
        reg.start_count += 1;
    }
};

using DroneTable = fsm::transition_table<
    fsm::row<Idle,    EvStart, Running>::when<BatteryCheckGuard>::then<ArmAction>,
    fsm::row<Running, EvStop,  Idle>
>;

using DroneFSM = fsm::fsm<DroneTable, DroneInPorts, DroneOutPorts, DroneRegisters>;

// ----------------------------------------------------------------------------
// Unit Tests
// ----------------------------------------------------------------------------

TEST(DroneFsmTest, InitialStateIsIdle) {
    // Arrange & Act
    DroneFSM sm;

    // Assert
    EXPECT_TRUE(sm.is_in<Idle>());
    EXPECT_EQ(sm.current_state_name(), "Idle");
    EXPECT_EQ(sm.registers().start_count, 0u);
}

TEST(DroneFsmTest, SuccessfulTransitionArmsMotorsAndIncrementsCounter) {
    // Arrange
    DroneFSM sm;
    DroneInPorts in{.battery_percent = 85.0f};
    DroneOutPorts out{};

    // Act
    fsm::dispatch_result res = sm.dispatch(EvStart{}, in, out);

    // Assert
    EXPECT_TRUE(res.is_success());
    EXPECT_TRUE(sm.is_in<Running>());
    EXPECT_TRUE(out.motor_enable);
    EXPECT_EQ(sm.registers().start_count, 1u);
}

TEST(DroneFsmTest, LowBatteryRejectsStartTransition) {
    // Arrange
    DroneFSM sm;
    DroneInPorts in{.battery_percent = 12.0f}; // Below 20% threshold
    DroneOutPorts out{};

    // Act
    fsm::dispatch_result res = sm.dispatch(EvStart{}, in, out);

    // Assert: Transition rejected by guard, state unchanged, no side effects
    EXPECT_TRUE(res.is_guard_rejected());
    EXPECT_FALSE(res.is_success());
    EXPECT_TRUE(sm.is_in<Idle>());
    EXPECT_FALSE(out.motor_enable);
    EXPECT_EQ(sm.registers().start_count, 0u);
}

TEST(DroneFsmTest, UnhandledEventIsReported) {
    // Arrange
    DroneFSM sm; // In Idle

    // Act: EvStop has no transition defined from Idle
    fsm::dispatch_result res = sm.dispatch(EvStop{});

    // Assert
    EXPECT_TRUE(res.is_unhandled());
    EXPECT_TRUE(sm.is_in<Idle>());
}
```

---

## 2. Catch2 Recipes

```cpp
#include <catch2/catch_test_macros.hpp>
#include "fsm/backend/cpp/runtime/fsm.hpp"

TEST_CASE("Drone FSM State Transitions", "[fsm][drone]") {
    DroneFSM sm;
    DroneInPorts in{.battery_percent = 90.0f};
    DroneOutPorts out{};

    SECTION("Successful transition from Idle to Running") {
        auto res = sm.dispatch(EvStart{}, in, out);
        REQUIRE(res.is_success());
        REQUIRE(sm.is_in<Running>());
        REQUIRE(out.motor_enable == true);
        REQUIRE(sm.registers().start_count == 1u);
    }

    SECTION("Rejected transition on low battery") {
        in.battery_percent = 10.0f;
        auto res = sm.dispatch(EvStart{}, in, out);
        REQUIRE(res.is_guard_rejected());
        REQUIRE(sm.is_in<Idle>());
        REQUIRE(out.motor_enable == false);
        REQUIRE(sm.registers().start_count == 0u);
    }

    SECTION("Cycle tick step evaluation") {
        fsm::step_result step_res = sm.step(in, out);
        REQUIRE(step_res.is_steady());
    }
}
```

---

## 3. Mocking External Services

When state actions interact with hardware or external RPC through `Services`, you can inject mock service objects directly in tests:

```cpp
struct IMotorHardware {
    virtual ~IMotorHardware() = default;
    virtual void write_pwm(uint16_t channel, float duty) = 0;
};

struct DroneServices {
    IMotorHardware* hardware = nullptr;
};

// In Unit Test:
class MockMotorHardware : public IMotorHardware {
public:
    int write_count = 0;
    float last_duty = 0.0f;
    void write_pwm(uint16_t, float duty) override {
        write_count++;
        last_duty = duty;
    }
};

TEST(DroneServiceTest, ActionCallsServiceDriver) {
    MockMotorHardware mock_hw;
    DroneServices srv{&mock_hw};

    DroneFSM sm(DroneRegisters{}, srv);
    DroneInPorts in{.battery_percent = 100.0f};
    DroneOutPorts out{};

    sm.dispatch(EvStart{}, in, out);
    // Verify hardware mock interaction
}
```

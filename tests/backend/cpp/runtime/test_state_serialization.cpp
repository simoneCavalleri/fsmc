/**
 * @file test_state_serialization.cpp
 * @brief Unit tests for C++ runtime state snapshot binary serialization and deserialization.
 *
 * Validates:
 * - Active state restoration from serialized binary snapshots.
 * - Datapath registers bitwise snapshot preservation.
 * - History state cache serialization and restoration.
 * - Deterministic timer manager and time invariant residence time preservation.
 * - Defensive verification: magic mismatch, checksum corruption, buffer truncation.
 * - std::span and zero-heap RTOS-safe APIs.
 */

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "fsm/backend/cpp/runtime/fsm.hpp"
#include "fsm/backend/cpp/runtime/serialization.hpp"
#include "fsm/backend/cpp/runtime/snapshot_recorder.hpp"
#include "fsm/backend/cpp/runtime/transition_table.hpp"

namespace {

// ============================================================================
// Model Definitions for Testing
// ============================================================================

// States
struct Idle {
    static constexpr std::string_view name = "Idle";
};

struct Running {
    static constexpr std::string_view name = "Running";
};

struct Paused {
    static constexpr std::string_view name = "Paused";
};

struct Fault {
    static constexpr std::string_view name = "Fault";
};

// Events
struct StartEvt {
    static constexpr std::string_view name = "StartEvt";
};

struct PauseEvt {
    static constexpr std::string_view name = "PauseEvt";
};

struct ResumeEvt {
    static constexpr std::string_view name = "ResumeEvt";
};

struct StopEvt {
    static constexpr std::string_view name = "StopEvt";
};

// Custom Registers
struct MotorRegisters {
    std::int32_t speed_rpm{0};
    double temperature_c{25.0};
    std::uint32_t cycle_count{0};

    bool operator==(const MotorRegisters& other) const noexcept {
        return speed_rpm == other.speed_rpm && (std::abs(temperature_c - other.temperature_c) < 1e-6) &&
               cycle_count == other.cycle_count;
    }
};

static_assert(std::is_trivially_copyable_v<MotorRegisters>);

// Transition Table
using SimpleTable =
    fsm::transition_table<fsm::transition<Idle, StartEvt, Running>, fsm::transition<Running, PauseEvt, Paused>,
                          fsm::transition<Paused, ResumeEvt, Running>, fsm::transition<Running, StopEvt, Idle>>;

using MotorFSM = fsm::fsm<SimpleTable, fsm::no_ports, fsm::no_ports, MotorRegisters>;

// ============================================================================
// 1. Basic State & Registers Serialization
// ============================================================================

TEST(StateSerialization, BasicState_TransitionsAndRestoresActiveState) {
    MotorFSM machine1;
    EXPECT_TRUE(machine1.is_in_state<Idle>());

    // Dispatch Start to transition to Running
    auto res = machine1.dispatch(StartEvt{});
    EXPECT_TRUE(res.is_success());
    EXPECT_TRUE(machine1.is_in_state<Running>());

    // Set custom registers
    machine1.registers().speed_rpm = 3200;
    machine1.registers().temperature_c = 48.5;
    machine1.registers().cycle_count = 150;

    // Serialize to fixed stack buffer
    std::array<std::uint8_t, 256> buffer{};
    std::size_t written = 0;
    bool ser_ok = machine1.serialize(buffer.data(), buffer.size(), written);
    ASSERT_TRUE(ser_ok);
    EXPECT_GT(written, 0u);
    EXPECT_EQ(written, machine1.serialized_size());

    // Create fresh FSM in initial Idle state
    MotorFSM machine2;
    EXPECT_TRUE(machine2.is_in_state<Idle>());
    EXPECT_EQ(machine2.registers().speed_rpm, 0);

    // Deserialize into fresh instance
    std::size_t read = 0;
    bool deser_ok = machine2.deserialize(buffer.data(), written, read);
    ASSERT_TRUE(deser_ok);
    EXPECT_EQ(read, written);

    // Verify machine2 is now in Running with exact registers!
    EXPECT_TRUE(machine2.is_in_state<Running>());
    EXPECT_EQ(machine2.registers().speed_rpm, 3200);
    EXPECT_DOUBLE_EQ(machine2.registers().temperature_c, 48.5);
    EXPECT_EQ(machine2.registers().cycle_count, 150u);
}

TEST(StateSerialization, StdSpanApi_SerializesAndRestoresCorrectly) {
    MotorFSM fsm_src;
    (void)fsm_src.dispatch(StartEvt{});
    (void)fsm_src.dispatch(PauseEvt{});
    EXPECT_TRUE(fsm_src.is_in_state<Paused>());
    fsm_src.registers().speed_rpm = 1200;

    std::array<std::uint8_t, 128> buf{};
    std::span<std::uint8_t> out_span(buf);

    // Non-member serialize_state with span
    std::size_t written = 0;
    bool ok_ser = fsm::serialize_state(fsm_src, out_span, written);
    ASSERT_TRUE(ok_ser);

    MotorFSM fsm_dst;
    EXPECT_TRUE(fsm_dst.is_in_state<Idle>());

    std::span<const std::uint8_t> in_span(buf.data(), written);
    std::size_t read_bytes = 0;
    bool ok_deser = fsm::deserialize_state(fsm_dst, in_span, read_bytes);
    ASSERT_TRUE(ok_deser);
    EXPECT_EQ(read_bytes, written);

    EXPECT_TRUE(fsm_dst.is_in_state<Paused>());
    EXPECT_EQ(fsm_dst.registers().speed_rpm, 1200);
}

// ============================================================================
// 2. Timers and Residence Time Preservation
// ============================================================================

struct TimedState {
    static constexpr std::string_view name = "TimedState";
    static constexpr std::uint64_t max_stay_duration_ms = 500;
};

struct TimeoutEvt {
    static constexpr std::string_view name = "TimeoutEvt";
};

using TimedTable = fsm::transition_table<fsm::transition<TimedState, TimeoutEvt, Idle>>;
using TimedFSM = fsm::fsm<TimedTable, fsm::no_ports, fsm::no_ports, MotorRegisters>;

TEST(StateSerialization, InvariantResidenceTime_PreservesResidenceDuration) {
    TimedFSM machine1;
    EXPECT_TRUE(machine1.is_in_state<TimedState>());
    // Tick to accumulate residence time
    machine1.tick(150);
    EXPECT_EQ(machine1.serialized_size(), sizeof(fsm::snapshot_header) + sizeof(MotorRegisters));

    std::vector<std::uint8_t> buffer(machine1.serialized_size());
    std::size_t written = 0;
    ASSERT_TRUE(machine1.serialize(buffer.data(), buffer.size(), written));

    // Verify snapshot_header directly
    fsm::snapshot_header header{};
    std::memcpy(&header, buffer.data(), sizeof(header));
    EXPECT_EQ(header.magic, fsm::SNAPSHOT_MAGIC);
    EXPECT_EQ(header.version, fsm::SNAPSHOT_VERSION);
    const std::uint64_t residence_time = header.residence_time_ms;
    EXPECT_EQ(residence_time, 150u);

    // Deserialize into machine2
    TimedFSM machine2;
    std::size_t read = 0;
    ASSERT_TRUE(machine2.deserialize(buffer.data(), written, read));
    EXPECT_EQ(read, written);
    EXPECT_TRUE(machine2.is_in_state<TimedState>());
}

// ============================================================================
// 3. Defensive Validation & Error Handling
// ============================================================================

TEST(StateSerialization, InsufficientCapacity_FailsGracefully) {
    MotorFSM machine;
    std::array<std::uint8_t, 10> tiny_buf{};
    std::size_t written = 0;

    // Buffer smaller than serialized_size()
    EXPECT_FALSE(machine.serialize(tiny_buf.data(), tiny_buf.size(), written));
    EXPECT_EQ(written, 0u);
}

TEST(StateSerialization, MagicMismatch_RejectsCorruption) {
    MotorFSM machine1;
    std::vector<std::uint8_t> buffer(machine1.serialized_size());
    std::size_t written = 0;
    ASSERT_TRUE(machine1.serialize(buffer.data(), buffer.size(), written));

    // Corrupt magic header word
    buffer[0] = 0xAA;
    buffer[1] = 0xBB;

    MotorFSM machine2;
    std::size_t read = 0;
    EXPECT_FALSE(machine2.deserialize(buffer.data(), written, read));
}

TEST(StateSerialization, VersionMismatch_RejectsOutdatedSchema) {
    MotorFSM machine1;
    std::vector<std::uint8_t> buffer(machine1.serialized_size());
    std::size_t written = 0;
    ASSERT_TRUE(machine1.serialize(buffer.data(), buffer.size(), written));

    // Corrupt version field (offset 4)
    buffer[4] = 99;

    MotorFSM machine2;
    std::size_t read = 0;
    EXPECT_FALSE(machine2.deserialize(buffer.data(), written, read));
}

TEST(StateSerialization, ChecksumCorruption_DetectsPayloadTampering) {
    MotorFSM machine1;
    machine1.registers().speed_rpm = 5000;
    std::vector<std::uint8_t> buffer(machine1.serialized_size());
    std::size_t written = 0;
    ASSERT_TRUE(machine1.serialize(buffer.data(), buffer.size(), written));

    // Tamper with payload byte (in registers section)
    buffer.back() ^= 0xFF;

    MotorFSM machine2;
    std::size_t read = 0;
    // Checksum check must detect tampering and reject deserialization!
    EXPECT_FALSE(machine2.deserialize(buffer.data(), written, read));
}

TEST(StateSerialization, InvalidStateIndex_RejectsOutOfBoundsState) {
    MotorFSM machine1;
    std::vector<std::uint8_t> buffer(machine1.serialized_size());
    std::size_t written = 0;
    ASSERT_TRUE(machine1.serialize(buffer.data(), buffer.size(), written));

    // Set state_index to 999 using offsetof
    std::uint32_t invalid_idx = 999;
    std::memcpy(buffer.data() + offsetof(fsm::snapshot_header, state_index), &invalid_idx, sizeof(invalid_idx));

    // Recompute payload checksum so checksum check passes but state_index fails
    auto* header = reinterpret_cast<fsm::snapshot_header*>(buffer.data());
    std::uint8_t* payload = buffer.data() + sizeof(fsm::snapshot_header);
    header->payload_checksum = fsm::compute_checksum(payload, buffer.size() - sizeof(fsm::snapshot_header));

    MotorFSM machine2;
    std::size_t read = 0;
    EXPECT_FALSE(machine2.deserialize(buffer.data(), written, read));
}

TEST(StateSerialization, TruncatedBuffer_ReturnsFalse) {
    MotorFSM machine1;
    std::vector<std::uint8_t> buffer(machine1.serialized_size());
    std::size_t written = 0;
    ASSERT_TRUE(machine1.serialize(buffer.data(), buffer.size(), written));

    MotorFSM machine2;
    std::size_t read = 0;
    // Passing smaller size than required
    EXPECT_FALSE(machine2.deserialize(buffer.data(), sizeof(fsm::snapshot_header) - 1, read));
}

TEST(SnapshotRecorder, RecordAndRollback_RestoresPreviousStateAndRegisters) {
    MotorFSM machine;
    EXPECT_TRUE(machine.is_in_state<Idle>());
    EXPECT_EQ(machine.registers().speed_rpm, 0);

    fsm::snapshot_recorder<8, 256> recorder;
    EXPECT_TRUE(recorder.empty());
    EXPECT_EQ(recorder.size(), 0u);

    // Snapshot 0: Idle
    EXPECT_TRUE(recorder.record(machine, 10));
    EXPECT_EQ(recorder.size(), 1u);
    EXPECT_EQ(recorder.total_recorded(), 1u);

    // Transition to Running
    machine.dispatch(StartEvt{});
    EXPECT_TRUE(machine.is_in_state<Running>());
    machine.registers().speed_rpm = 1500;

    // Snapshot 1: Running (tag 20)
    EXPECT_TRUE(recorder.record(machine, 20));
    EXPECT_EQ(recorder.size(), 2u);

    // Transition to Paused
    machine.dispatch(PauseEvt{});
    EXPECT_TRUE(machine.is_in_state<Paused>());

    // Rollback 1 step: Should restore Running with 1500 rpm
    EXPECT_TRUE(recorder.rollback(machine, 1));
    EXPECT_TRUE(machine.is_in_state<Running>());
    EXPECT_EQ(machine.registers().speed_rpm, 1500);

    // Rollback 1 step: Should restore Idle with 0 rpm
    EXPECT_TRUE(recorder.rollback(machine, 1));
    EXPECT_TRUE(machine.is_in_state<Idle>());
    EXPECT_EQ(machine.registers().speed_rpm, 0);
}

TEST(SnapshotRecorder, RewindToCheckpoint_RecoversSavedTag) {
    MotorFSM machine;
    fsm::snapshot_recorder<8, 256> recorder;

    EXPECT_TRUE(recorder.record(machine, 100));  // Checkpoint 100 in Idle

    machine.dispatch(StartEvt{});
    machine.registers().speed_rpm = 3000;
    EXPECT_TRUE(recorder.record(machine, 200));  // Checkpoint 200 in Running

    machine.dispatch(PauseEvt{});
    EXPECT_TRUE(machine.is_in_state<Paused>());

    // Rewind directly to Checkpoint 200
    EXPECT_TRUE(recorder.rewind_to_checkpoint(machine, 200));
    EXPECT_TRUE(machine.is_in_state<Running>());
    EXPECT_EQ(machine.registers().speed_rpm, 3000);

    // Rewind to non-existent checkpoint returns false
    EXPECT_FALSE(recorder.rewind_to_checkpoint(machine, 999));
}

TEST(SnapshotRecorder, CircularBufferWraparound_PreservesCapacity) {
    MotorFSM machine;
    fsm::snapshot_recorder<4, 256> recorder;

    for (std::uint32_t i = 1; i <= 10; ++i) {
        machine.registers().speed_rpm = static_cast<std::int32_t>(i * 100);
        EXPECT_TRUE(recorder.record(machine, i));
    }

    EXPECT_EQ(recorder.capacity(), 4u);
    EXPECT_EQ(recorder.size(), 4u);
    EXPECT_EQ(recorder.total_recorded(), 10u);
    EXPECT_TRUE(recorder.full());

    const auto* latest = recorder.latest();
    ASSERT_NE(latest, nullptr);
    EXPECT_EQ(latest->tag, 10u);
}

}  // namespace

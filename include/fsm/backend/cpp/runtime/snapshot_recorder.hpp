/**
 * @file snapshot_recorder.hpp
 * @brief Zero-heap circular snapshot recorder and time-travel rollback manager for C++ FSMs.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include "fsm/backend/cpp/runtime/serialization.hpp"

namespace fsm {

/**
 * @struct snapshot_entry
 * @brief Fixed-size zero-heap snapshot entry stored in snapshot_recorder.
 */
template <std::size_t MaxSnapshotSize = 256>
struct snapshot_entry {
    std::array<std::uint8_t, MaxSnapshotSize> data{};
    std::size_t size{0};
    std::uint32_t tag{0};
    std::uint64_t timestamp_us{0};
    std::uint64_t step_count{0};
    std::uint32_t checksum{0};

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        if (size == 0 || size > MaxSnapshotSize) return false;
        return compute_checksum(data.data(), size) == checksum;
    }
};

/**
 * @class snapshot_recorder
 * @brief Zero-heap circular ring buffer for runtime snapshot execution trace recording and rollback.
 *
 * Designed for real-time safety-critical digital twin monitoring, hardware-in-the-loop (HIL)
 * diagnostics, time-travel debugging, and automatic rollback on fault detection.
 *
 * @tparam Capacity Maximum number of snapshots preserved in the circular ring buffer.
 * @tparam MaxSnapshotSize Maximum buffer capacity in bytes per snapshot (default 256).
 */
template <std::size_t Capacity, std::size_t MaxSnapshotSize = 256>
class snapshot_recorder {
    static_assert(Capacity > 0, "snapshot_recorder Capacity must be strictly positive");
    static_assert(MaxSnapshotSize >= sizeof(snapshot_header), "MaxSnapshotSize must accommodate snapshot_header");

  public:
    using entry_type = snapshot_entry<MaxSnapshotSize>;

    constexpr snapshot_recorder() noexcept = default;

    /**
     * @brief Records a snapshot of the current state of the given FSM machine into the ring buffer.
     * @tparam FSM State machine type providing serialize() method.
     * @param machine FSM instance to capture.
     * @param tag User-defined checkpoint tag/id.
     * @param timestamp_us Optional timestamp in microseconds.
     * @return True if serialized and recorded successfully, false otherwise.
     */
    template <typename FSM>
    bool record(const FSM& machine, std::uint32_t tag = 0, std::uint64_t timestamp_us = 0) noexcept {
        std::size_t slot = head_ % Capacity;
        entry_type& entry = buffer_[slot];

        std::size_t written = 0;
        if (!machine.serialize(entry.data.data(), MaxSnapshotSize, written)) {
            return false;
        }

        entry.size = written;
        entry.tag = tag;
        entry.timestamp_us = timestamp_us;
        entry.step_count = total_recorded_;
        entry.checksum = compute_checksum(entry.data.data(), written);

        head_ = (head_ + 1) % Capacity;
        if (count_ < Capacity) {
            ++count_;
        }
        ++total_recorded_;
        return true;
    }

    /**
     * @brief Rolls back the state machine by the specified number of steps (default 1 step).
     * @tparam FSM State machine type providing deserialize() method.
     * @param machine FSM instance to restore.
     * @param steps Number of steps to unwind backwards (must be <= size()).
     * @return True if successfully restored, false otherwise.
     */
    template <typename FSM>
    bool rollback(FSM& machine, std::size_t steps = 1) noexcept {
        if (steps == 0 || steps > count_) {
            return false;
        }

        // Calculate target slot
        std::size_t target_idx = (head_ + Capacity - steps) % Capacity;
        const entry_type& entry = buffer_[target_idx];

        if (!entry.is_valid()) {
            return false;
        }

        std::size_t read = 0;
        if (!machine.deserialize(entry.data.data(), entry.size, read)) {
            return false;
        }

        head_ = target_idx;
        count_ -= steps;
        return true;
    }

    /**
     * @brief Rewinds to the most recently recorded checkpoint matching the given tag.
     * @tparam FSM State machine type providing deserialize() method.
     * @param machine FSM instance to restore.
     * @param tag Checkpoint tag identifier to search for.
     * @return True if checkpoint was found and restored, false otherwise.
     */
    template <typename FSM>
    bool rewind_to_checkpoint(FSM& machine, std::uint32_t tag) noexcept {
        for (std::size_t step = 1; step <= count_; ++step) {
            std::size_t idx = (head_ + Capacity - step) % Capacity;
            if (buffer_[idx].tag == tag && buffer_[idx].is_valid()) {
                return rollback(machine, step);
            }
        }
        return false;
    }

    /**
     * @brief Returns the most recently recorded snapshot entry, if any.
     */
    [[nodiscard]] const entry_type* latest() const noexcept {
        if (count_ == 0) return nullptr;
        std::size_t idx = (head_ + Capacity - 1) % Capacity;
        return &buffer_[idx];
    }

    /**
     * @brief Returns the number of snapshots currently stored in the ring buffer.
     */
    [[nodiscard]] constexpr std::size_t size() const noexcept { return count_; }

    /**
     * @brief Returns the maximum capacity of the ring buffer.
     */
    [[nodiscard]] constexpr std::size_t capacity() const noexcept { return Capacity; }

    /**
     * @brief Checks if the buffer is empty.
     */
    [[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }

    /**
     * @brief Checks if the buffer is full.
     */
    [[nodiscard]] constexpr bool full() const noexcept { return count_ == Capacity; }

    /**
     * @brief Returns total number of record invocations since initialization.
     */
    [[nodiscard]] constexpr std::uint64_t total_recorded() const noexcept { return total_recorded_; }

    /**
     * @brief Clears all snapshots in the recorder.
     */
    void clear() noexcept {
        head_ = 0;
        count_ = 0;
    }

  private:
    std::array<entry_type, Capacity> buffer_{};
    std::size_t head_{0};
    std::size_t count_{0};
    std::uint64_t total_recorded_{0};
};

}  // namespace fsm

#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace fsm {

#ifndef FSMC_CACHELINE_SIZE
#define FSMC_CACHELINE_SIZE 64
#endif

/**
 * @brief Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer.
 *
 * Provides wait-free, zero-allocation circular FIFO event queue designed for
 * embedded hard real-time systems and hardware Interrupt Service Routines (ISR).
 * Cacheline aligned to prevent false sharing across multi-core processors.
 *
 * @tparam T Element value type.
 * @tparam Capacity Total capacity (must be a power of 2).
 */
template <typename T, std::size_t Capacity>
class spsc_ring_buffer {
    static_assert(Capacity > 0, "Capacity must be greater than 0");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2 for fast modulo indexing");

  public:
    spsc_ring_buffer() = default;

    ~spsc_ring_buffer() {
        T discard;
        while (pop(discard)) {
        }
    }

    spsc_ring_buffer(const spsc_ring_buffer&) = delete;
    spsc_ring_buffer& operator=(const spsc_ring_buffer&) = delete;
    spsc_ring_buffer(spsc_ring_buffer&&) = delete;
    spsc_ring_buffer& operator=(spsc_ring_buffer&&) = delete;

    // Enqueue an element (Producer thread / ISR)
    template <typename... Args>
    bool emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);

        if ((head - tail) >= Capacity) {
            return false;  // Full
        }

        new (get_slot(head)) T(std::forward<Args>(args)...);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    bool push(const T& item) noexcept(std::is_nothrow_copy_constructible_v<T>) { return emplace(item); }

    bool push(T&& item) noexcept(std::is_nothrow_move_constructible_v<T>) { return emplace(std::move(item)); }

    // Dequeue an element (Consumer thread / FSM worker)
    bool pop(T& out_item) noexcept(std::is_nothrow_move_assignable_v<T>) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);

        if (tail == head) {
            return false;  // Empty
        }

        T* slot = get_slot(tail);
        out_item = std::move(*slot);
        slot->~T();
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::optional<T> pop() noexcept(std::is_nothrow_move_constructible_v<T>) {
        T item;
        if (pop(item)) {
            return item;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool full() const noexcept {
        return (head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_relaxed)) >= Capacity;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        return head >= tail ? (head - tail) : 0;
    }

    [[nodiscard]] constexpr std::size_t capacity() const noexcept { return Capacity; }

  private:
    static constexpr std::size_t IndexMask = Capacity - 1;

    T* get_slot(std::size_t idx) noexcept { return reinterpret_cast<T*>(&storage_[(idx & IndexMask) * sizeof(T)]); }

    // Separate head and tail into distinct cachelines to prevent false sharing
    alignas(FSMC_CACHELINE_SIZE) std::atomic<std::size_t> head_{0};
    alignas(FSMC_CACHELINE_SIZE) std::atomic<std::size_t> tail_{0};
    alignas(alignof(T)) alignas(FSMC_CACHELINE_SIZE) char storage_[Capacity * sizeof(T)];
};

}  // namespace fsm

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace fsm {

// Cache line size constant to prevent false sharing without compiler-specific ABI interference-size warnings
inline constexpr std::size_t cache_line_size = 64;

/**
 * @brief Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer.
 *
 * @tparam T Value type stored in the queue.
 * @tparam Capacity Power-of-two queue capacity.
 */
template <typename T, std::size_t Capacity = 1024>
class spsc_ring_buffer {
    static_assert((Capacity > 1) && ((Capacity & (Capacity - 1)) == 0),
                  "spsc_ring_buffer Capacity must be a power of two");
    static_assert(std::is_default_constructible_v<T>,
                  "spsc_ring_buffer<T> requires T to be default-constructible "
                  "(needed by pop()/the destructor's drain loop)");

  public:
    using value_type = T;
    using size_type = std::size_t;

    spsc_ring_buffer() = default;

    ~spsc_ring_buffer() {
        T item;
        while (pop(item)) {
            // Drain remaining elements invoking destructors
        }
    }

    // Non-copyable, non-movable
    spsc_ring_buffer(const spsc_ring_buffer&) = delete;
    spsc_ring_buffer& operator=(const spsc_ring_buffer&) = delete;
    spsc_ring_buffer(spsc_ring_buffer&&) = delete;
    spsc_ring_buffer& operator=(spsc_ring_buffer&&) = delete;

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

    // Aligned byte storage for placement-new.
    T* get_slot(std::size_t index) noexcept {
        return reinterpret_cast<T*>(raw_storage_.data() + (index & IndexMask) * sizeof(T));
    }

    alignas(cache_line_size) std::atomic<std::size_t> head_{0};
    alignas(cache_line_size) std::atomic<std::size_t> tail_{0};
    alignas(alignof(T)) std::array<std::byte, sizeof(T) * Capacity> raw_storage_;
};

}  // namespace fsm

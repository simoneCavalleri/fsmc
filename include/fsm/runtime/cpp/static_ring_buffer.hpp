#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace fsm {

/**
 * @brief Overflow policy when pushing into a saturated bounded ring buffer.
 */
enum class OverflowPolicy : std::uint8_t {
    DropOldest,        ///< Discards the oldest element at the head to accommodate the newest element
    DropIncoming,      ///< Rejects the incoming element, leaving the existing buffer unmodified
    AssertOnOverflow   ///< Triggers an assertion in debug/safety builds if the queue overflows
};

// Zero-allocation, static capacity ring buffer designed for hard real-time / embedded systems
template <typename T, std::size_t Capacity>
class static_ring_buffer {
    static_assert(Capacity > 0, "static_ring_buffer Capacity must be greater than 0");

  public:
    using value_type = T;
    using size_type = std::size_t;

    constexpr static_ring_buffer() noexcept = default;

    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr bool full() const noexcept { return size_ == Capacity; }
    [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
    [[nodiscard]] constexpr size_type capacity() const noexcept { return Capacity; }

    constexpr bool push(const T& value, OverflowPolicy policy = OverflowPolicy::DropIncoming) noexcept {
        if (full()) {
            if (policy == OverflowPolicy::DropIncoming) {
                return false;
            }
            if (policy == OverflowPolicy::AssertOnOverflow) {
                assert(!full() && "static_ring_buffer overflow with AssertOnOverflow policy");
                return false;
            }
            if (policy == OverflowPolicy::DropOldest) {
                // Advance head to drop oldest item
                head_ = (head_ + 1) % Capacity;
                --size_;
            }
        }
        buffer_[tail_] = value;
        tail_ = (tail_ + 1) % Capacity;
        ++size_;
        return true;
    }

    constexpr bool push(T&& value, OverflowPolicy policy = OverflowPolicy::DropIncoming) noexcept {
        if (full()) {
            if (policy == OverflowPolicy::DropIncoming) {
                return false;
            }
            if (policy == OverflowPolicy::AssertOnOverflow) {
                assert(!full() && "static_ring_buffer overflow with AssertOnOverflow policy");
                return false;
            }
            if (policy == OverflowPolicy::DropOldest) {
                // Advance head to drop oldest item
                head_ = (head_ + 1) % Capacity;
                --size_;
            }
        }
        buffer_[tail_] = std::move(value);
        tail_ = (tail_ + 1) % Capacity;
        ++size_;
        return true;
    }

    constexpr std::optional<T> pop() noexcept {
        if (empty()) {
            return std::nullopt;
        }
        T item = std::move(buffer_[head_]);
        head_ = (head_ + 1) % Capacity;
        --size_;
        return item;
    }

    [[nodiscard]] constexpr const T* peek() const noexcept {
        if (empty()) {
            return nullptr;
        }
        return &buffer_[head_];
    }

    [[nodiscard]] constexpr T* peek() noexcept {
        if (empty()) {
            return nullptr;
        }
        return &buffer_[head_];
    }

    constexpr void clear() noexcept {
        head_ = 0;
        tail_ = 0;
        size_ = 0;
    }

  private:
    std::array<T, Capacity> buffer_{};
    size_type head_{0};
    size_type tail_{0};
    size_type size_{0};
};

}  // namespace fsm

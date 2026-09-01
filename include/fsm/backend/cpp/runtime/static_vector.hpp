#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

namespace fsm {

/**
 * @brief Fixed-capacity, zero-heap inline vector (similar to std::inplace_vector in C++26 / boost::static_vector).
 *
 * All storage is allocated inline within the struct on the stack, guaranteeing:
 * - 0 dynamic memory allocations (malloc/new)
 * - 0 heap fragmentation
 * - Deterministic O(1) push_back, pop_back, and indexed access
 * - Safe for hard real-time, embedded systems, MISRA C++, and ISR contexts
 *
 * @tparam T The element type stored in the vector.
 * @tparam Capacity Maximum number of elements the inline storage can hold.
 */
template <typename T, std::size_t Capacity>
class static_vector {
    static_assert(Capacity > 0, "static_vector Capacity must be greater than 0");

  public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = T*;
    using const_iterator = const T*;

    constexpr static_vector() noexcept = default;

    constexpr static_vector(const static_vector& other) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        for (std::size_t i = 0; i < other.size_; ++i) {
            data_[i] = other.data_[i];
        }
        size_ = other.size_;
    }

    constexpr static_vector& operator=(const static_vector& other) noexcept(std::is_nothrow_copy_assignable_v<T>) {
        if (this != &other) {
            for (std::size_t i = 0; i < other.size_; ++i) {
                data_[i] = other.data_[i];
            }
            size_ = other.size_;
        }
        return *this;
    }

    constexpr static_vector(static_vector&& other) noexcept(std::is_nothrow_move_constructible_v<T>) {
        for (std::size_t i = 0; i < other.size_; ++i) {
            data_[i] = std::move(other.data_[i]);
        }
        size_ = other.size_;
        other.size_ = 0;
    }

    constexpr static_vector& operator=(static_vector&& other) noexcept(std::is_nothrow_move_assignable_v<T>) {
        if (this != &other) {
            for (std::size_t i = 0; i < other.size_; ++i) {
                data_[i] = std::move(other.data_[i]);
            }
            size_ = other.size_;
            other.size_ = 0;
        }
        return *this;
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr bool full() const noexcept { return size_ == Capacity; }
    [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
    [[nodiscard]] constexpr size_type max_size() const noexcept { return Capacity; }
    [[nodiscard]] constexpr size_type capacity() const noexcept { return Capacity; }

    constexpr void clear() noexcept { size_ = 0; }

    constexpr bool push_back(const T& value) noexcept {
        if (full()) {
            return false;
        }
        data_[size_++] = value;
        return true;
    }

    constexpr bool push_back(T&& value) noexcept {
        if (full()) {
            return false;
        }
        data_[size_++] = std::move(value);
        return true;
    }

    template <typename... Args>
    constexpr bool emplace_back(Args&&... args) noexcept {
        if (full()) {
            return false;
        }
        data_[size_++] = T(std::forward<Args>(args)...);
        return true;
    }

    constexpr void pop_back() noexcept {
        if (size_ > 0) {
            --size_;
            data_[size_] = T{};
        }
    }

    [[nodiscard]] constexpr reference operator[](size_type index) noexcept {
        assert(index < size_ && "static_vector index out of range");
        return data_[index];
    }

    [[nodiscard]] constexpr const_reference operator[](size_type index) const noexcept {
        assert(index < size_ && "static_vector index out of range");
        return data_[index];
    }

    [[nodiscard]] constexpr reference front() noexcept {
        assert(size_ > 0 && "static_vector front() on empty vector");
        return data_[0];
    }

    [[nodiscard]] constexpr const_reference front() const noexcept {
        assert(size_ > 0 && "static_vector front() on empty vector");
        return data_[0];
    }

    [[nodiscard]] constexpr reference back() noexcept {
        assert(size_ > 0 && "static_vector back() on empty vector");
        return data_[size_ - 1];
    }

    [[nodiscard]] constexpr const_reference back() const noexcept {
        assert(size_ > 0 && "static_vector back() on empty vector");
        return data_[size_ - 1];
    }

    [[nodiscard]] constexpr pointer data() noexcept { return data_.data(); }
    [[nodiscard]] constexpr const_pointer data() const noexcept { return data_.data(); }

    [[nodiscard]] constexpr iterator begin() noexcept { return data_.data(); }
    [[nodiscard]] constexpr const_iterator begin() const noexcept { return data_.data(); }
    [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return data_.data(); }

    [[nodiscard]] constexpr iterator end() noexcept { return data_.data() + size_; }
    [[nodiscard]] constexpr const_iterator end() const noexcept { return data_.data() + size_; }
    [[nodiscard]] constexpr const_iterator cend() const noexcept { return data_.data() + size_; }

    constexpr iterator erase(const_iterator pos) noexcept {
        auto idx = static_cast<size_type>(pos - data_.data());
        if (idx >= size_) {
            return end();
        }
        for (size_type i = idx; i + 1 < size_; ++i) {
            data_[i] = std::move(data_[i + 1]);
        }
        --size_;
        data_[size_] = T{};
        return data_.data() + idx;
    }

  private:
    std::array<T, Capacity> data_{};
    size_type size_{0};
};

}  // namespace fsm

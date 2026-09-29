#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#if __cplusplus >= 202002L || (defined(__cpp_lib_span) && __cpp_lib_span >= 202002L)
#include <span>
#endif
#include <string_view>
#include <type_traits>
#include <variant>

namespace fsm {

/**
 * @brief Binary Snapshot Magic Header ('F','S','M','C').
 */
inline constexpr std::uint32_t SNAPSHOT_MAGIC = 0x46534D43;

/**
 * @brief Current Binary Snapshot Schema Version.
 */
inline constexpr std::uint16_t SNAPSHOT_VERSION = 1;

#pragma pack(push, 1)
/**
 * @struct snapshot_header
 * @brief Fixed-size header preceding every FSM serialized binary snapshot.
 */
struct snapshot_header {
    std::uint32_t magic{SNAPSHOT_MAGIC};      ///< Magic identification word (0x46534D43)
    std::uint16_t version{SNAPSHOT_VERSION};  ///< Schema revision
    std::uint16_t flags{0};                   ///< Reserved feature flags
    std::uint64_t residence_time_ms{0};       ///< State residence duration in milliseconds
    std::uint32_t state_index{0};             ///< Active state index in std::variant
    std::uint32_t history_count{0};           ///< Number of serialized history entries
    std::uint32_t timer_count{0};             ///< Number of serialized active timers
    std::uint32_t registers_size{0};          ///< Size in bytes of trivially copyable registers payload
    std::uint32_t payload_checksum{0};        ///< 32-bit FNV-1a checksum of the payload following this header
};

/**
 * @struct snapshot_timer_entry
 * @brief Packed layout of a serialized timer entry.
 */
struct snapshot_timer_entry {
    std::uint32_t timer_id{0};     ///< Deterministic timer identifier
    std::uint64_t interval_ms{0};  ///< Timer period / timeout duration
    std::uint64_t elapsed_ms{0};   ///< Elapsed time towards expiration
    std::uint8_t periodic{0};      ///< 1 if auto-restarting, 0 if one-shot
};
#pragma pack(pop)

/**
 * @brief Computes a deterministic 32-bit FNV-1a checksum over a contiguous memory buffer.
 */
inline constexpr std::uint32_t compute_checksum(const std::uint8_t* data, std::size_t len) noexcept {
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < len; ++i) {
        hash ^= static_cast<std::uint32_t>(data[i]);
        hash *= 16777619u;
    }
    return hash;
}

namespace detail {

/**
 * @brief Compile-time recursive setter for std::variant alternative by numerical index.
 */
template <typename Variant, std::size_t Index = 0>
bool set_variant_index(Variant& var, std::size_t target_index) {
    if constexpr (Index < std::variant_size_v<Variant>) {
        if (Index == target_index) {
            var.template emplace<Index>();
            return true;
        }
        return set_variant_index<Variant, Index + 1>(var, target_index);
    } else {
        return false;
    }
}

}  // namespace detail

/**
 * @brief Standalone non-member serialization helper taking a raw memory buffer.
 */
template <typename FSM>
bool serialize_state(const FSM& machine, std::uint8_t* dest, std::size_t capacity,
                     std::size_t& bytes_written) noexcept {
    return machine.serialize(dest, capacity, bytes_written);
}

template <typename FSM>
bool serialize_state(const FSM& machine, std::uint8_t* dest, std::size_t capacity) noexcept {
    std::size_t written = 0;
    return machine.serialize(dest, capacity, written);
}

/**
 * @brief Standalone non-member deserialization helper taking a raw memory buffer.
 */
template <typename FSM>
bool deserialize_state(FSM& machine, const std::uint8_t* src, std::size_t size, std::size_t& bytes_read) noexcept {
    return machine.deserialize(src, size, bytes_read);
}

template <typename FSM>
bool deserialize_state(FSM& machine, const std::uint8_t* src, std::size_t size) noexcept {
    std::size_t read = 0;
    return machine.deserialize(src, size, read);
}

#if __cplusplus >= 202002L || (defined(__cpp_lib_span) && __cpp_lib_span >= 202002L)
/**
 * @brief Standalone non-member serialization helper taking a destination span.
 */
template <typename FSM>
bool serialize_state(const FSM& machine, std::span<std::uint8_t> buffer, std::size_t& bytes_written) noexcept {
    return machine.serialize(buffer, bytes_written);
}

template <typename FSM>
bool serialize_state(const FSM& machine, std::span<std::uint8_t> buffer) noexcept {
    std::size_t written = 0;
    return machine.serialize(buffer, written);
}

/**
 * @brief Standalone non-member deserialization helper taking a source span.
 */
template <typename FSM>
bool deserialize_state(FSM& machine, std::span<const std::uint8_t> buffer, std::size_t& bytes_read) noexcept {
    return machine.deserialize(buffer, bytes_read);
}

template <typename FSM>
bool deserialize_state(FSM& machine, std::span<const std::uint8_t> buffer) noexcept {
    std::size_t read = 0;
    return machine.deserialize(buffer, read);
}
#endif

}  // namespace fsm

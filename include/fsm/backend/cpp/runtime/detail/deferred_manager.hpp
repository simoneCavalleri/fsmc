#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
#include <variant>

#include "fsm/backend/cpp/runtime/static_vector.hpp"
#include "fsm/backend/cpp/runtime/traits/type_list.hpp"

namespace fsm::detail {

template <typename Table, std::size_t DeferredCapacity, bool HasDeferred>
class deferred_manager;

// Specialization for state machines WITH deferred events
template <typename Table, std::size_t DeferredCapacity>
class deferred_manager<Table, DeferredCapacity, true> {
  public:
    static constexpr std::size_t max_deferred_capacity = DeferredCapacity;
    using event_variant = typename Table::event_variant;

    template <typename Event>
    bool defer_event(const Event& event) {
        return deferred_queue_.push_back(event_variant{event});
    }

    [[nodiscard]] std::size_t deferred_count() const noexcept { return deferred_queue_.size(); }

    void clear_deferred_events() noexcept { deferred_queue_.clear(); }

    [[nodiscard]] std::size_t serialized_payload_size() const noexcept {
        if (deferred_queue_.empty()) {
            return 0;
        }
        std::size_t sz = sizeof(std::uint32_t);
        for (const auto& ev_var : deferred_queue_) {
            sz += sizeof(std::uint32_t) * 2U;
            sz += std::visit(
                [](const auto& ev) noexcept -> std::size_t {
                    using EvT = std::decay_t<decltype(ev)>;
                    if constexpr (std::is_trivially_copyable_v<EvT>) {
                        return sizeof(EvT);
                    } else {
                        return 0U;
                    }
                },
                ev_var);
        }
        return sz;
    }

    std::size_t serialize_payload(std::uint8_t* dest) const noexcept {
        if (deferred_queue_.empty()) {
            return 0;
        }
        std::uint8_t* curr = dest;
        const auto count = static_cast<std::uint32_t>(deferred_queue_.size());
        std::memcpy(curr, &count, sizeof(count));
        curr += sizeof(count);
        for (const auto& ev_var : deferred_queue_) {
            const auto idx = static_cast<std::uint32_t>(ev_var.index());
            std::memcpy(curr, &idx, sizeof(idx));
            curr += sizeof(idx);
            std::visit(
                [&curr](const auto& ev) noexcept {
                    using EvT = std::decay_t<decltype(ev)>;
                    if constexpr (std::is_trivially_copyable_v<EvT>) {
                        const auto ev_sz = static_cast<std::uint32_t>(sizeof(EvT));
                        std::memcpy(curr, &ev_sz, sizeof(ev_sz));
                        curr += sizeof(ev_sz);
                        std::memcpy(curr, &ev, sizeof(EvT));
                        curr += sizeof(EvT);
                    } else {
                        const std::uint32_t ev_sz = 0U;
                        std::memcpy(curr, &ev_sz, sizeof(ev_sz));
                        curr += sizeof(ev_sz);
                    }
                },
                ev_var);
        }
        return static_cast<std::size_t>(curr - dest);
    }

    [[nodiscard]] bool validate_payload(const std::uint8_t*& curr, const std::uint8_t* end) const noexcept {
        if (static_cast<std::size_t>(end - curr) < sizeof(std::uint32_t)) {
            return false;
        }
        std::uint32_t count = 0U;
        std::memcpy(&count, curr, sizeof(count));
        curr += sizeof(count);
        if (count > max_deferred_capacity) {
            return false;
        }
        constexpr std::size_t num_alts = std::variant_size_v<event_variant>;
        for (std::uint32_t i = 0U; i < count; ++i) {
            if (static_cast<std::size_t>(end - curr) < sizeof(std::uint32_t) * 2U) {
                return false;
            }
            std::uint32_t idx = 0U;
            std::uint32_t ev_sz = 0U;
            std::memcpy(&idx, curr, sizeof(idx));
            curr += sizeof(idx);
            std::memcpy(&ev_sz, curr, sizeof(ev_sz));
            curr += sizeof(ev_sz);
            if (idx >= num_alts || static_cast<std::size_t>(end - curr) < ev_sz) {
                return false;
            }
            if (!check_expected_alt_size(idx, ev_sz, std::make_index_sequence<num_alts>{})) {
                return false;
            }
            curr += ev_sz;
        }
        return true;
    }

    bool deserialize_payload(const std::uint8_t*& curr, const std::uint8_t* end) noexcept {
        deferred_queue_.clear();
        if (static_cast<std::size_t>(end - curr) < sizeof(std::uint32_t)) {
            return false;
        }
        std::uint32_t count = 0U;
        std::memcpy(&count, curr, sizeof(count));
        curr += sizeof(count);
        if (count > max_deferred_capacity) {
            return false;
        }
        constexpr std::size_t num_alts = std::variant_size_v<event_variant>;
        for (std::uint32_t i = 0U; i < count; ++i) {
            std::uint32_t idx = 0U;
            std::uint32_t ev_sz = 0U;
            std::memcpy(&idx, curr, sizeof(idx));
            curr += sizeof(idx);
            std::memcpy(&ev_sz, curr, sizeof(ev_sz));
            curr += sizeof(ev_sz);
            event_variant ev_var{};
            if (!restore_alt(idx, curr, ev_sz, ev_var, std::make_index_sequence<num_alts>{})) {
                return false;
            }
            curr += ev_sz;
            if (!deferred_queue_.push_back(std::move(ev_var))) {
                return false;
            }
        }
        return true;
    }

    template <typename DispatchDirectFn>
    void process_deferred_queue(DispatchDirectFn&& dispatch_direct_fn) {
        if (deferred_queue_.empty() || is_replaying_deferred_) {
            return;
        }
        is_replaying_deferred_ = true;

        bool any_handled = true;
        while (any_handled && !deferred_queue_.empty()) {
            any_handled = false;
            for (auto it = deferred_queue_.begin(); it != deferred_queue_.end();) {
                bool handled = std::visit(
                    [&dispatch_direct_fn](const auto& ev) -> bool { return dispatch_direct_fn(ev).is_success(); }, *it);
                if (handled) {
                    it = deferred_queue_.erase(it);
                    any_handled = true;
                    break;
                }
                ++it;
            }
        }

        is_replaying_deferred_ = false;
    }

  private:
    template <std::size_t... Is>
    static bool check_expected_alt_size(std::uint32_t idx, std::uint32_t ev_sz,
                                        std::index_sequence<Is...> /*seq*/) noexcept {
        bool ok = false;
        (void)((idx == Is ? (ok = (ev_sz == expected_alt_size<Is>()), true) : false) || ...);
        return ok;
    }

    template <std::size_t I>
    static constexpr std::uint32_t expected_alt_size() noexcept {
        using AltT = std::variant_alternative_t<I, event_variant>;
        if constexpr (std::is_trivially_copyable_v<AltT>) {
            return static_cast<std::uint32_t>(sizeof(AltT));
        } else {
            return 0U;
        }
    }

    template <std::size_t I>
    static bool restore_single_alt(const std::uint8_t* data, std::uint32_t ev_sz, event_variant& out) noexcept {
        using AltT = std::variant_alternative_t<I, event_variant>;
        if constexpr (std::is_default_constructible_v<AltT>) {
            AltT val{};
            if constexpr (std::is_trivially_copyable_v<AltT>) {
                if (ev_sz != sizeof(AltT)) {
                    return false;
                }
                std::memcpy(&val, data, sizeof(AltT));
            } else {
                (void)data;
                if (ev_sz != 0U) {
                    return false;
                }
            }
            out.template emplace<I>(std::move(val));
            return true;
        } else {
            (void)data;
            (void)ev_sz;
            (void)out;
            return false;
        }
    }

    template <std::size_t... Is>
    static bool restore_alt(std::uint32_t idx, const std::uint8_t* data, std::uint32_t ev_sz, event_variant& out,
                            std::index_sequence<Is...> /*seq*/) noexcept {
        bool ok = false;
        (void)((idx == Is ? (ok = restore_single_alt<Is>(data, ev_sz, out), true) : false) || ...);
        return ok;
    }

    static_vector<event_variant, max_deferred_capacity> deferred_queue_{};
    bool is_replaying_deferred_{false};
};

// Specialization for state machines WITHOUT deferred events (Zero-overhead, 0 bytes)
template <typename Table, std::size_t DeferredCapacity>
class deferred_manager<Table, DeferredCapacity, false> {
  public:
    static constexpr std::size_t max_deferred_capacity = 0;

    template <typename Event>
    bool defer_event(const Event& /*event*/) noexcept {
        return false;
    }
    [[nodiscard]] std::size_t deferred_count() const noexcept { return 0; }
    void clear_deferred_events() noexcept {}
    [[nodiscard]] std::size_t serialized_payload_size() const noexcept { return 0; }
    std::size_t serialize_payload(std::uint8_t* /*dest*/) const noexcept { return 0; }
    [[nodiscard]] bool validate_payload(const std::uint8_t*& /*curr*/, const std::uint8_t* /*end*/) const noexcept {
        return false;
    }
    bool deserialize_payload(const std::uint8_t*& /*curr*/, const std::uint8_t* /*end*/) noexcept { return false; }

    template <typename DispatchDirectFn>
    void process_deferred_queue(DispatchDirectFn&& /*dispatch_direct_fn*/) noexcept {}
};

}  // namespace fsm::detail

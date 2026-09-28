#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

#include "fsm/backend/cpp/runtime/traits/lifecycle_traits.hpp"
#include "fsm/backend/cpp/runtime/traits/observer_traits.hpp"
#include "fsm/backend/cpp/runtime/traits/reflection.hpp"
#include "fsm/backend/cpp/runtime/traits/type_list.hpp"

namespace fsm {

namespace detail {

template <typename StateList>
struct any_state_has_time_invariant : std::false_type {};

template <typename... States>
struct any_state_has_time_invariant<type_list<States...>> : std::bool_constant<(has_time_invariant_v<States> || ...)> {
};

template <typename Table, typename = void>
struct table_has_time_invariants : std::false_type {};

template <typename Table>
struct table_has_time_invariants<Table, std::void_t<typename Table::states>>
    : any_state_has_time_invariant<typename Table::states> {};

template <typename Table>
inline constexpr bool table_has_time_invariants_v = table_has_time_invariants<Table>::value;

template <typename Table, bool HasInvariants = table_has_time_invariants_v<Table>>
class invariant_manager;

// Active invariant manager when at least one state declares time invariant / permanence bound
template <typename Table>
class invariant_manager<Table, true> {
  public:
    void reset() noexcept {
        residence_time_ms_ = 0;
        last_violation_.reset();
    }

    void advance_time(std::uint64_t delta_ms) noexcept { residence_time_ms_ += delta_ms; }

    [[nodiscard]] std::uint64_t state_residence_time() const noexcept { return residence_time_ms_; }

    [[nodiscard]] bool has_invariant_violation() const noexcept { return last_violation_.has_value(); }

    [[nodiscard]] bool is_invariant_satisfied() const noexcept { return !last_violation_.has_value(); }

    [[nodiscard]] const std::optional<invariant_violation_info>& last_invariant_violation() const noexcept {
        return last_violation_;
    }

    void clear_invariant_violation() noexcept { last_violation_.reset(); }

    void set_invariant_violation_handler(std::function<void(const invariant_violation_info&)> handler) {
        violation_handler_ = std::move(handler);
    }

    template <typename CurrentStateVariant>
    bool check_invariants(const CurrentStateVariant& state_var) {
        return std::visit(
            [this](const auto& st) {
                using S = std::decay_t<decltype(st)>;
                if constexpr (has_time_invariant_v<S>) {
                    if (residence_time_ms_ > state_max_stay_ms_v<S>) {
                        invariant_violation_info info{::fsm::get_state_name(st), residence_time_ms_,
                                                      state_max_stay_ms_v<S>};
                        last_violation_ = info;
                        if (violation_handler_) {
                            violation_handler_(info);
                        }
                        return false;
                    }
                }
                last_violation_.reset();
                return true;
            },
            state_var);
    }

  private:
    std::uint64_t residence_time_ms_{0};
    std::optional<invariant_violation_info> last_violation_{std::nullopt};
    std::function<void(const invariant_violation_info&)> violation_handler_{};
};

// Zero-overhead empty invariant manager when NO states declare time invariants (0 bytes with [[no_unique_address]])
template <typename Table>
class invariant_manager<Table, false> {
  public:
    constexpr void reset() noexcept {}
    constexpr void advance_time(std::uint64_t /*delta_ms*/) noexcept {}
    [[nodiscard]] constexpr std::uint64_t state_residence_time() const noexcept { return 0; }
    [[nodiscard]] constexpr bool has_invariant_violation() const noexcept { return false; }
    [[nodiscard]] constexpr bool is_invariant_satisfied() const noexcept { return true; }
    [[nodiscard]] const std::optional<invariant_violation_info>& last_invariant_violation() const noexcept {
        static const std::optional<invariant_violation_info> empty_violation{std::nullopt};
        return empty_violation;
    }
    constexpr void clear_invariant_violation() noexcept {}
    void set_invariant_violation_handler(std::function<void(const invariant_violation_info&)> /*handler*/) noexcept {}

    template <typename CurrentStateVariant>
    constexpr bool check_invariants(const CurrentStateVariant& /*state_var*/) const noexcept {
        return true;
    }
};

}  // namespace detail
}  // namespace fsm

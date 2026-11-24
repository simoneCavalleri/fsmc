/**
 * @file service_definition.hpp
 * @brief Strongly-typed external service method signatures (for sys-math, HAL, and zero-vtable contracts).
 */

#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/ir/data_type.hpp"

namespace fsm::ir {

/**
 * @struct ServiceParameter
 * @brief Typed formal parameter of an external service method.
 */
struct ServiceParameter {
    std::string name;  ///< Formal parameter name (e.g. "setpoint", "measured")
    DataType type;     ///< Canonical IR parameter data type

    ServiceParameter() = default;
    ServiceParameter(std::string param_name, DataType param_type)
        : name(std::move(param_name)), type(std::move(param_type)) {}

    bool operator==(const ServiceParameter& other) const noexcept = default;
};

/**
 * @struct ServiceMethodSignature
 * @brief Explicit typed signature of an external service invoked by FSM actions (sys-math / HAL).
 */
struct ServiceMethodSignature {
    std::string name;                               ///< Method identifier (e.g. "pid_step", "hal_set_pwm")
    std::vector<ServiceParameter> parameters;       ///< Ordered typed parameter list
    DataType return_type{PrimitiveTypeKind::Void};  ///< Return type (PrimitiveTypeKind::Void when void)
    bool is_pure{false};                            ///< True for side-effect-free pure math functions
    bool is_noexcept{true};                         ///< True when guaranteed not to throw (real-time/freestanding)
    std::string description;                        ///< Optional documentation string

    ServiceMethodSignature() = default;
    ServiceMethodSignature(std::string method_name, std::vector<ServiceParameter> params = {},
                           DataType ret_type = DataType(PrimitiveTypeKind::Void), bool pure = false, bool no_exc = true,
                           std::string desc = "")
        : name(std::move(method_name)),
          parameters(std::move(params)),
          return_type(std::move(ret_type)),
          is_pure(pure),
          is_noexcept(no_exc),
          description(std::move(desc)) {}

    [[nodiscard]] bool is_void_return() const noexcept {
        return return_type.is_primitive() && return_type.primitive() == PrimitiveTypeKind::Void;
    }

    bool operator==(const ServiceMethodSignature& other) const noexcept = default;
};

}  // namespace fsm::ir

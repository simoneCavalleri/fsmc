/**
 * @file mcdc_harness_generator.hpp
 * @brief Safety-critical MC/DC (Modified Condition / Decision Coverage) test harness synthesizer.
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fsm/ir/fsm_ir.hpp"

namespace fsm::backend::verification {
using ir::FsmIr;

/**
 * @struct McdcTestVector
 * @brief Test Vector for a single MC/DC evaluation.
 */
struct McdcTestVector {
    std::map<std::string, bool> condition_values;  ///< Assigned boolean values for atomic conditions
    bool decision_outcome{false};                  ///< Overall guard evaluation result
    std::string condition_tested;                  ///< Condition whose independent effect is proven by this vector pair
};

/**
 * @struct McdcIndependencePair
 * @brief Pair of test vectors proving independent effect of a condition for MC/DC (high-integrity testing).
 */
struct McdcIndependencePair {
    std::string condition_name;   ///< Name of the toggled condition
    McdcTestVector vector_true;   ///< Test vector where condition evaluates to TRUE
    McdcTestVector vector_false;  ///< Test vector where condition evaluates to FALSE
};

/**
 * @class BoolExpr
 * @brief Simple AST interface for evaluating boolean expressions during MC/DC synthesis.
 */
class BoolExpr {
  public:
    virtual ~BoolExpr() = default;
    [[nodiscard]] virtual bool evaluate(const std::map<std::string, bool>& env) const = 0;
    virtual void collect_variables(std::vector<std::string>& vars) const = 0;
};

/**
 * @class VarExpr
 * @brief Atomic variable terminal leaf in a boolean expression.
 */
class VarExpr : public BoolExpr {
  public:
    explicit VarExpr(std::string name) : name_(std::move(name)) {}
    [[nodiscard]] bool evaluate(const std::map<std::string, bool>& env) const override {
        auto it = env.find(name_);
        return (it != env.end()) ? it->second : false;
    }
    void collect_variables(std::vector<std::string>& vars) const override {
        if (std::find(vars.begin(), vars.end(), name_) == vars.end()) {
            vars.push_back(name_);
        }
    }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

  private:
    std::string name_;
};

/**
 * @class NotExpr
 * @brief Negation unary operator node in a boolean expression.
 */
class NotExpr : public BoolExpr {
  public:
    explicit NotExpr(std::unique_ptr<BoolExpr> sub) : sub_(std::move(sub)) {}
    [[nodiscard]] bool evaluate(const std::map<std::string, bool>& env) const override { return !sub_->evaluate(env); }
    void collect_variables(std::vector<std::string>& vars) const override { sub_->collect_variables(vars); }

  private:
    std::unique_ptr<BoolExpr> sub_;
};

/**
 * @class BinaryExpr
 * @brief Binary AND/OR operator node in a boolean expression.
 */
class BinaryExpr : public BoolExpr {
  public:
    enum Op { And, Or };
    BinaryExpr(Op op, std::unique_ptr<BoolExpr> left, std::unique_ptr<BoolExpr> right)
        : op_(op), left_(std::move(left)), right_(std::move(right)) {}

    [[nodiscard]] bool evaluate(const std::map<std::string, bool>& env) const override {
        if (op_ == And) {
            return left_->evaluate(env) && right_->evaluate(env);
        }
        return left_->evaluate(env) || right_->evaluate(env);
    }

    void collect_variables(std::vector<std::string>& vars) const override {
        left_->collect_variables(vars);
        right_->collect_variables(vars);
    }

  private:
    Op op_;
    std::unique_ptr<BoolExpr> left_;
    std::unique_ptr<BoolExpr> right_;
};

/**
 * @class McdcHarnessGenerator
 * @brief Safety-Critical Test Harness & MC/DC Test Suite Synthesizer.
 *
 * Automatically generates Modified Condition / Decision Coverage (MC/DC) test suites
 * with independence pairs for composite transition guards in high-integrity software.
 */
class McdcHarnessGenerator {
  public:
    /**
     * @brief Synthesizes complete GoogleTest unit test file verifying MC/DC coverage.
     */
    static std::string generate_gtest_harness(const FsmIr& ir);

    /**
     * @brief Computes the MC/DC independence pairs for a boolean AST and list of conditions.
     */
    static std::vector<McdcIndependencePair> compute_mcdc_pairs(const BoolExpr& expr,
                                                                const std::vector<std::string>& conditions);

  private:
    static std::unique_ptr<BoolExpr> parse_simple_expr(const std::string& expr_str,
                                                       const std::vector<std::string>& atomic_conditions);

    static std::string trim(std::string_view s);
};

}  // namespace fsm::backend::verification

#pragma once

#include <cstddef>

namespace aofve {

enum class OptionType { call, put };

struct MarketData {
    double spot{};
    double rate{};
    double dividend_yield{};
};

struct OptionSpec {
    double strike{};
    double maturity{};
    OptionType type{OptionType::call};
};

struct PricingRequest {
    OptionSpec option;
    MarketData market;
    double volatility{};
};

struct TreeConfig {
    std::size_t steps{400};
    bool even_odd_smoothing{true};
};

struct FiniteDifferenceConfig {
    std::size_t space_steps{400};
    std::size_t time_steps{400};
    double s_max_multiplier{4.0};
    double relaxation{1.2};
    double tolerance{1e-9};
    std::size_t max_iterations{10'000};
};

struct SolverResult {
    double value{};
    std::size_t iterations{};
    bool converged{};
};

}  // namespace aofve

#pragma once

#include "aofve/types.hpp"

#include <cstddef>

namespace aofve {

struct ImpliedVolatilityConfig {
    double minimum_volatility{1e-7};
    double initial_maximum_volatility{0.5};
    double maximum_volatility{8.0};
    double price_tolerance{1e-8};
    double volatility_tolerance{1e-8};
    std::size_t max_iterations{100};
    TreeConfig tree{};
};

struct ImpliedVolatilityResult {
    double volatility{};
    double price_residual{};
    double bracket_width{};
    std::size_t iterations{};
    bool converged{};
};

[[nodiscard]] ImpliedVolatilityResult american_implied_volatility(
    double market_price,
    const OptionSpec& option,
    const MarketData& market,
    const ImpliedVolatilityConfig& config = {});

}  // namespace aofve

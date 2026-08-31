#pragma once

#include "aofve/types.hpp"

#include <span>
#include <vector>

namespace aofve {

[[nodiscard]] double intrinsic_value(const OptionSpec& option, double spot) noexcept;

[[nodiscard]] double european_black_scholes(
    const OptionSpec& option,
    const MarketData& market,
    double volatility);

[[nodiscard]] double american_binomial(
    const OptionSpec& option,
    const MarketData& market,
    double volatility,
    const TreeConfig& config = {});

[[nodiscard]] SolverResult american_finite_difference(
    const OptionSpec& option,
    const MarketData& market,
    double volatility,
    const FiniteDifferenceConfig& config = {});

[[nodiscard]] std::vector<double> price_batch(
    std::span<const PricingRequest> requests,
    const TreeConfig& config = {},
    std::size_t workers = 1);

}  // namespace aofve

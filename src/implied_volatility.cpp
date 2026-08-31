#include "aofve/implied_volatility.hpp"

#include "aofve/pricing.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace aofve {

ImpliedVolatilityResult american_implied_volatility(
    double market_price,
    const OptionSpec& option,
    const MarketData& market,
    const ImpliedVolatilityConfig& config) {
    if (!std::isfinite(market_price) || market_price < 0.0) {
        throw std::invalid_argument("market price must be finite and non-negative");
    }
    if (!(config.minimum_volatility >= 0.0) ||
        !(config.initial_maximum_volatility > config.minimum_volatility) ||
        !(config.maximum_volatility >= config.initial_maximum_volatility) ||
        !(config.price_tolerance > 0.0) || !(config.volatility_tolerance > 0.0) ||
        config.max_iterations == 0) {
        throw std::invalid_argument("invalid implied-volatility configuration");
    }
    const double intrinsic = intrinsic_value(option, market.spot);
    if (market_price + config.price_tolerance < intrinsic) {
        throw std::invalid_argument("market price is below the American intrinsic-value bound");
    }
    if (option.maturity == 0.0) {
        if (std::abs(market_price - intrinsic) <= config.price_tolerance) {
            return {0.0, intrinsic - market_price, 0.0, 0, true};
        }
        throw std::invalid_argument("a non-intrinsic price has no implied volatility at expiry");
    }

    const auto residual = [&](double volatility) {
        return american_binomial(option, market, volatility, config.tree) - market_price;
    };

    double lower = config.minimum_volatility;
    double f_lower = residual(lower);
    if (std::abs(f_lower) <= config.price_tolerance) {
        return {lower, f_lower, 0.0, 0, true};
    }
    if (f_lower > 0.0) {
        throw std::invalid_argument("market price is below the model price at minimum volatility");
    }

    double upper = config.initial_maximum_volatility;
    double f_upper = residual(upper);
    while (f_upper < 0.0 && upper < config.maximum_volatility) {
        upper = std::min(config.maximum_volatility, 2.0 * upper);
        f_upper = residual(upper);
    }
    if (f_upper < 0.0) {
        throw std::invalid_argument("market price cannot be bracketed below maximum volatility");
    }
    if (std::abs(f_upper) <= config.price_tolerance) {
        return {upper, f_upper, upper - lower, 0, true};
    }

    double candidate = 0.5 * (lower + upper);
    double f_candidate = residual(candidate);
    for (std::size_t iteration = 1; iteration <= config.max_iterations; ++iteration) {
        const double denominator = f_upper - f_lower;
        double secant = denominator == 0.0
            ? 0.5 * (lower + upper)
            : upper - f_upper * (upper - lower) / denominator;

        // Keep the interpolation away from the bracket edges. Bisection is the
        // deterministic fallback when the secant step becomes unreliable.
        const double guard = 0.1 * (upper - lower);
        if (!std::isfinite(secant) || secant <= lower + guard || secant >= upper - guard) {
            secant = 0.5 * (lower + upper);
        }
        candidate = secant;
        f_candidate = residual(candidate);

        if (std::abs(f_candidate) <= config.price_tolerance ||
            upper - lower <= config.volatility_tolerance) {
            return {candidate, f_candidate, upper - lower, iteration, true};
        }
        if (f_candidate < 0.0) {
            lower = candidate;
            f_lower = f_candidate;
        } else {
            upper = candidate;
            f_upper = f_candidate;
        }
    }
    return {candidate, f_candidate, upper - lower, config.max_iterations, false};
}

}  // namespace aofve

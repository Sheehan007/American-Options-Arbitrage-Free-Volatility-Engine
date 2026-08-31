#pragma once

#include "aofve/svi.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace aofve {

enum class ArbitrageKind {
    strike_monotonicity,
    strike_convexity,
    negative_density,
    butterfly,
    calendar
};

struct ArbitrageViolation {
    ArbitrageKind kind{};
    double maturity{};
    double coordinate{};
    double value{};
    std::string message;
};

struct ArbitrageReport {
    bool arbitrage_free{true};
    double minimum_margin{};
    std::vector<ArbitrageViolation> violations;
};

[[nodiscard]] ArbitrageReport check_call_price_arbitrage(
    std::span<const double> strikes,
    std::span<const double> call_prices,
    double tolerance = 1e-10);

[[nodiscard]] ArbitrageReport check_svi_butterfly_arbitrage(
    const SVIParameters& parameters,
    double log_moneyness_min = -3.0,
    double log_moneyness_max = 3.0,
    std::size_t grid_points = 301,
    double tolerance = 1e-10);

[[nodiscard]] ArbitrageReport check_calendar_arbitrage(
    const SVISurface& surface,
    double log_moneyness_min = -2.0,
    double log_moneyness_max = 2.0,
    std::size_t grid_points = 201,
    double tolerance = 1e-10);

[[nodiscard]] ArbitrageReport diagnose_svi_surface(
    const SVISurface& surface,
    double log_moneyness_min = -2.0,
    double log_moneyness_max = 2.0,
    std::size_t grid_points = 201,
    double tolerance = 1e-10);

}  // namespace aofve

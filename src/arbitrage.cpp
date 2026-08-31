#include "aofve/arbitrage.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace aofve {
namespace {

void append_report(ArbitrageReport& destination, const ArbitrageReport& source) {
    destination.arbitrage_free = destination.arbitrage_free && source.arbitrage_free;
    destination.minimum_margin = std::min(destination.minimum_margin, source.minimum_margin);
    destination.violations.insert(
        destination.violations.end(), source.violations.begin(), source.violations.end());
}

}  // namespace

ArbitrageReport check_call_price_arbitrage(
    std::span<const double> strikes,
    std::span<const double> call_prices,
    double tolerance) {
    if (strikes.size() != call_prices.size() || strikes.size() < 2 || tolerance < 0.0) {
        throw std::invalid_argument("call-price checks need matching arrays of at least two points");
    }
    ArbitrageReport report;
    report.minimum_margin = std::numeric_limits<double>::infinity();
    std::vector<double> slopes;
    slopes.reserve(strikes.size() - 1);
    for (std::size_t index = 0; index < strikes.size(); ++index) {
        if (!std::isfinite(strikes[index]) || !std::isfinite(call_prices[index]) ||
            strikes[index] <= 0.0 || call_prices[index] < 0.0 ||
            (index > 0 && strikes[index] <= strikes[index - 1])) {
            throw std::invalid_argument("strikes must increase and all call observations must be finite and valid");
        }
        if (index == 0) {
            continue;
        }
        const double slope = (call_prices[index] - call_prices[index - 1]) /
            (strikes[index] - strikes[index - 1]);
        slopes.push_back(slope);
        const double monotonicity_margin = call_prices[index - 1] - call_prices[index];
        report.minimum_margin = std::min(report.minimum_margin, monotonicity_margin);
        if (monotonicity_margin < -tolerance) {
            report.arbitrage_free = false;
            report.violations.push_back({
                ArbitrageKind::strike_monotonicity,
                0.0,
                0.5 * (strikes[index - 1] + strikes[index]),
                monotonicity_margin,
                "call price increases with strike"});
        }
    }
    for (std::size_t index = 1; index < slopes.size(); ++index) {
        const double convexity_margin = slopes[index] - slopes[index - 1];
        report.minimum_margin = std::min(report.minimum_margin, convexity_margin);
        if (convexity_margin < -tolerance) {
            report.arbitrage_free = false;
            report.violations.push_back({
                ArbitrageKind::strike_convexity,
                0.0,
                strikes[index],
                convexity_margin,
                "call-price slope decreases, implying negative risk-neutral density"});
            report.violations.push_back({
                ArbitrageKind::negative_density,
                0.0,
                strikes[index],
                convexity_margin,
                "finite-difference Breeden-Litzenberger density is negative"});
        }
    }
    if (!std::isfinite(report.minimum_margin)) {
        report.minimum_margin = 0.0;
    }
    return report;
}

ArbitrageReport check_svi_butterfly_arbitrage(
    const SVIParameters& parameters,
    double log_moneyness_min,
    double log_moneyness_max,
    std::size_t grid_points,
    double tolerance) {
    if (!parameters.is_admissible(tolerance) || grid_points < 2 ||
        !(log_moneyness_max > log_moneyness_min) || tolerance < 0.0) {
        throw std::invalid_argument("invalid SVI butterfly-check inputs");
    }
    ArbitrageReport report;
    report.minimum_margin = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < grid_points; ++index) {
        const double fraction = static_cast<double>(index) / static_cast<double>(grid_points - 1);
        const double k = log_moneyness_min + fraction * (log_moneyness_max - log_moneyness_min);
        const double factor = svi_density_factor(parameters, k);
        const double density = svi_log_strike_density(parameters, k);
        report.minimum_margin = std::min({report.minimum_margin, factor, density});
        if (factor < -tolerance) {
            report.arbitrage_free = false;
            report.violations.push_back({
                ArbitrageKind::butterfly,
                0.0,
                k,
                factor,
                "SVI density factor is negative"});
        }
        if (density < -tolerance) {
            report.arbitrage_free = false;
            report.violations.push_back({
                ArbitrageKind::negative_density,
                0.0,
                k,
                density,
                "SVI-implied log-strike density is negative"});
        }
    }
    return report;
}

ArbitrageReport check_calendar_arbitrage(
    const SVISurface& surface,
    double log_moneyness_min,
    double log_moneyness_max,
    std::size_t grid_points,
    double tolerance) {
    if (grid_points < 2 || !(log_moneyness_max > log_moneyness_min) || tolerance < 0.0) {
        throw std::invalid_argument("invalid calendar-check inputs");
    }
    ArbitrageReport report;
    report.minimum_margin = std::numeric_limits<double>::infinity();
    const auto& maturities = surface.maturities();
    const auto& slices = surface.slices();
    if (maturities.size() < 2) {
        report.minimum_margin = 0.0;
        return report;
    }
    for (std::size_t maturity_index = 1; maturity_index < maturities.size(); ++maturity_index) {
        for (std::size_t grid_index = 0; grid_index < grid_points; ++grid_index) {
            const double fraction = static_cast<double>(grid_index) /
                static_cast<double>(grid_points - 1);
            const double k = log_moneyness_min + fraction *
                (log_moneyness_max - log_moneyness_min);
            const double margin = slices[maturity_index].total_variance(k) -
                slices[maturity_index - 1].total_variance(k);
            report.minimum_margin = std::min(report.minimum_margin, margin);
            if (margin < -tolerance) {
                report.arbitrage_free = false;
                report.violations.push_back({
                    ArbitrageKind::calendar,
                    maturities[maturity_index],
                    k,
                    margin,
                    "total variance decreases between adjacent maturities"});
            }
        }
    }
    return report;
}

ArbitrageReport diagnose_svi_surface(
    const SVISurface& surface,
    double log_moneyness_min,
    double log_moneyness_max,
    std::size_t grid_points,
    double tolerance) {
    ArbitrageReport report;
    report.minimum_margin = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < surface.slices().size(); ++index) {
        auto slice_report = check_svi_butterfly_arbitrage(
            surface.slices()[index], log_moneyness_min, log_moneyness_max, grid_points, tolerance);
        for (auto& violation : slice_report.violations) {
            violation.maturity = surface.maturities()[index];
        }
        append_report(report, slice_report);
    }
    append_report(
        report,
        check_calendar_arbitrage(
            surface, log_moneyness_min, log_moneyness_max, grid_points, tolerance));
    if (!std::isfinite(report.minimum_margin)) {
        report.minimum_margin = 0.0;
    }
    return report;
}

}  // namespace aofve

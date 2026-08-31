#include "aofve/aofve.hpp"

#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void check_close(double actual, double expected, double tolerance, const std::string& message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(
            message + ": actual=" + std::to_string(actual) +
            ", expected=" + std::to_string(expected));
    }
}

void test_black_scholes_reference() {
    const aofve::OptionSpec option{100.0, 1.0, aofve::OptionType::call};
    const aofve::MarketData market{100.0, 0.05, 0.02};
    check_close(aofve::european_black_scholes(option, market, 0.2), 9.2270055, 1e-6,
                "Black-Scholes dividend call reference");
}

void test_american_pricing_properties() {
    const aofve::MarketData market{100.0, 0.05, 0.0};
    const aofve::OptionSpec call{100.0, 1.0, aofve::OptionType::call};
    const aofve::OptionSpec put{100.0, 1.0, aofve::OptionType::put};
    const aofve::TreeConfig tree{600, true};
    const double call_price = aofve::american_binomial(call, market, 0.2, tree);
    const double european_call = aofve::european_black_scholes(call, market, 0.2);
    check_close(call_price, european_call, 0.015, "non-dividend American call equals European call");

    const double put_price = aofve::american_binomial(put, market, 0.2, tree);
    const double european_put = aofve::european_black_scholes(put, market, 0.2);
    check(put_price >= european_put, "American put must dominate its European counterpart");
    check(put_price >= aofve::intrinsic_value(put, market.spot),
          "American put must dominate intrinsic value");
}

void test_finite_difference_reference() {
    const aofve::MarketData market{95.0, 0.04, 0.015};
    const aofve::OptionSpec option{100.0, 1.25, aofve::OptionType::put};
    const double tree = aofve::american_binomial(option, market, 0.27, {700, true});
    aofve::FiniteDifferenceConfig config;
    config.space_steps = 350;
    config.time_steps = 350;
    const auto finite_difference = aofve::american_finite_difference(option, market, 0.27, config);
    check(finite_difference.converged, "projected SOR must converge on the reference case");
    check_close(finite_difference.value, tree, 0.08,
                "finite-difference and binomial American prices");
}

void test_implied_volatility_recovery() {
    const aofve::MarketData market{102.0, 0.035, 0.012};
    const aofve::OptionSpec option{105.0, 0.8, aofve::OptionType::put};
    aofve::ImpliedVolatilityConfig config;
    config.tree = {450, true};
    const double price = aofve::american_binomial(option, market, 0.31, config.tree);
    const auto result = aofve::american_implied_volatility(price, option, market, config);
    check(result.converged, "implied volatility solver must converge");
    check_close(result.volatility, 0.31, 2e-6, "implied volatility round trip");

    bool rejected = false;
    try {
        static_cast<void>(aofve::american_implied_volatility(0.1, option, market, config));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "implied volatility must reject a price below intrinsic value");
}

void test_batch_pricing() {
    std::vector<aofve::PricingRequest> requests;
    for (std::size_t index = 0; index < 12; ++index) {
        requests.push_back({
            {90.0 + static_cast<double>(index) * 2.0,
             0.5 + 0.05 * static_cast<double>(index),
             index % 2 == 0 ? aofve::OptionType::call : aofve::OptionType::put},
            {100.0, 0.03, 0.01},
            0.18 + 0.005 * static_cast<double>(index)});
    }
    const aofve::TreeConfig config{180, true};
    const auto prices = aofve::price_batch(requests, config, 3);
    check(prices.size() == requests.size(), "batch price count");
    for (std::size_t index = 0; index < requests.size(); ++index) {
        check_close(
            prices[index],
            aofve::american_binomial(
                requests[index].option, requests[index].market, requests[index].volatility, config),
            1e-13,
            "batch and scalar pricing must agree");
    }
}

void test_numerical_edge_cases() {
    const aofve::OptionSpec expired_put{100.0, 0.0, aofve::OptionType::put};
    const aofve::MarketData expired_market{82.0, 0.03, 0.01};
    check_close(
        aofve::american_binomial(expired_put, expired_market, 0.0),
        18.0,
        0.0,
        "expiry value must equal intrinsic value");

    const aofve::OptionSpec carry_call{100.0, 2.0, aofve::OptionType::call};
    const aofve::MarketData extreme_carry{100.0, 0.20, -0.10};
    const double fallback_price = aofve::american_binomial(
        carry_call, extreme_carry, 0.001, {50, true});
    check(std::isfinite(fallback_price), "extreme-carry tree fallback must stay finite");
    check(fallback_price >= aofve::intrinsic_value(carry_call, extreme_carry.spot),
          "extreme-carry result must respect the exercise obstacle");

    bool rejected_negative_volatility = false;
    try {
        static_cast<void>(aofve::american_binomial(carry_call, extreme_carry, -0.1));
    } catch (const std::invalid_argument&) {
        rejected_negative_volatility = true;
    }
    check(rejected_negative_volatility, "negative volatility must be rejected");

    const aofve::OptionSpec forward_value_call{100.0, 1.0, aofve::OptionType::call};
    const aofve::MarketData positive_rate{100.0, 0.05, 0.0};
    bool rejected_below_zero_volatility_price = false;
    try {
        static_cast<void>(aofve::american_implied_volatility(
            0.0, forward_value_call, positive_rate));
    } catch (const std::invalid_argument&) {
        rejected_below_zero_volatility_price = true;
    }
    check(rejected_below_zero_volatility_price,
          "IV solver must reject prices below the zero-volatility model boundary");

    const std::vector<aofve::PricingRequest> empty;
    check(aofve::price_batch(empty, {}, 0).empty(), "empty batch must return an empty result");
}

void test_svi_calibration() {
    const aofve::SVIParameters truth{0.025, 0.11, -0.35, 0.02, 0.24};
    std::vector<double> k;
    std::vector<double> variance;
    for (int index = -10; index <= 10; ++index) {
        k.push_back(0.08 * static_cast<double>(index));
        variance.push_back(truth.total_variance(k.back()));
    }
    aofve::SVICalibrationConfig config;
    config.max_iterations = 1'200;
    config.deterministic_restarts = 6;
    const auto result = aofve::calibrate_svi(k, variance, {}, config);
    check(result.parameters.is_admissible(), "calibrated SVI parameters must be admissible");
    check(result.rmse < 2e-4, "synthetic SVI fit RMSE");
    check(result.minimum_density_factor >= -1e-6, "calibrated SVI slice must be butterfly-free");
}

void test_arbitrage_diagnostics() {
    const std::vector<double> strikes{80.0, 90.0, 100.0, 110.0};
    const std::vector<double> valid_prices{22.0, 14.0, 8.0, 4.0};
    const auto valid = aofve::check_call_price_arbitrage(strikes, valid_prices);
    check(valid.arbitrage_free, "decreasing convex call prices should pass");

    const std::vector<double> invalid_prices{22.0, 17.0, 10.0, 7.0};
    const auto invalid = aofve::check_call_price_arbitrage(strikes, invalid_prices);
    check(!invalid.arbitrage_free, "concave call prices should fail");
    check(!invalid.violations.empty(), "arbitrage report should include violations");

    const aofve::SVIParameters first{0.025, 0.10, -0.30, 0.0, 0.25};
    const aofve::SVIParameters second{0.055, 0.11, -0.25, 0.0, 0.27};
    const aofve::SVISurface surface({0.5, 1.0}, {first, second});
    const auto report = aofve::diagnose_svi_surface(surface, -1.5, 1.5, 151);
    check(report.arbitrage_free, "reference SVI surface should pass diagnostics");

    const aofve::SVIParameters crossed{-0.005, 0.10, -0.30, 0.0, 0.25};
    const aofve::SVISurface crossed_surface({0.5, 1.0}, {first, crossed});
    check(!aofve::check_calendar_arbitrage(crossed_surface).arbitrage_free,
          "decreasing total variance must trigger a calendar violation");
}

void test_surface_calibration_calendar_repair() {
    const std::vector<double> maturities{0.5, 1.0};
    const aofve::SVIParameters short_slice{0.035, 0.09, -0.25, 0.0, 0.22};
    const aofve::SVIParameters long_slice{0.025, 0.09, -0.25, 0.0, 0.22};
    std::vector<std::vector<double>> k(2);
    std::vector<std::vector<double>> variance(2);
    for (int index = -8; index <= 8; ++index) {
        const double coordinate = 0.1 * static_cast<double>(index);
        for (std::size_t maturity = 0; maturity < 2; ++maturity) {
            k[maturity].push_back(coordinate);
        }
        variance[0].push_back(short_slice.total_variance(coordinate));
        variance[1].push_back(long_slice.total_variance(coordinate));
    }
    aofve::SVISurfaceCalibrationConfig config;
    config.slice.max_iterations = 700;
    config.slice.deterministic_restarts = 3;
    const auto result = aofve::calibrate_svi_surface(maturities, k, variance, config);
    check(result.calendar_shifts[1] > 0.0, "calendar repair must shift the crossed slice");
    check(aofve::check_calendar_arbitrage(result.surface, -2.0, 2.0, 161, 1e-8).arbitrage_free,
          "calibrated surface must be calendar monotone on the enforcement grid");
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"Black-Scholes reference", test_black_scholes_reference},
        {"American pricing properties", test_american_pricing_properties},
        {"finite-difference reference", test_finite_difference_reference},
        {"implied volatility recovery", test_implied_volatility_recovery},
        {"batch pricing", test_batch_pricing},
        {"numerical edge cases", test_numerical_edge_cases},
        {"SVI calibration", test_svi_calibration},
        {"arbitrage diagnostics", test_arbitrage_diagnostics},
        {"surface calendar repair", test_surface_calibration_calendar_repair},
    };

    std::size_t failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << tests.size() - failures << "/" << tests.size() << " tests passed\n";
    return failures == 0 ? 0 : 1;
}

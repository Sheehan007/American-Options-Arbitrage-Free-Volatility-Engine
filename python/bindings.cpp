#include "aofve/aofve.hpp"

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <stdexcept>

namespace py = pybind11;

PYBIND11_MODULE(_aofve, module) {
    module.doc() = "American option pricing and arbitrage-free SVI calibration";

    py::enum_<aofve::OptionType>(module, "OptionType")
        .value("CALL", aofve::OptionType::call)
        .value("PUT", aofve::OptionType::put);

    py::enum_<aofve::ArbitrageKind>(module, "ArbitrageKind")
        .value("STRIKE_MONOTONICITY", aofve::ArbitrageKind::strike_monotonicity)
        .value("STRIKE_CONVEXITY", aofve::ArbitrageKind::strike_convexity)
        .value("NEGATIVE_DENSITY", aofve::ArbitrageKind::negative_density)
        .value("BUTTERFLY", aofve::ArbitrageKind::butterfly)
        .value("CALENDAR", aofve::ArbitrageKind::calendar);

    py::class_<aofve::MarketData>(module, "MarketData")
        .def(py::init<double, double, double>(),
             py::arg("spot"), py::arg("rate"), py::arg("dividend_yield") = 0.0)
        .def_readwrite("spot", &aofve::MarketData::spot)
        .def_readwrite("rate", &aofve::MarketData::rate)
        .def_readwrite("dividend_yield", &aofve::MarketData::dividend_yield);

    py::class_<aofve::OptionSpec>(module, "OptionSpec")
        .def(py::init<double, double, aofve::OptionType>(),
             py::arg("strike"), py::arg("maturity"), py::arg("option_type"))
        .def_readwrite("strike", &aofve::OptionSpec::strike)
        .def_readwrite("maturity", &aofve::OptionSpec::maturity)
        .def_readwrite("option_type", &aofve::OptionSpec::type);

    py::class_<aofve::TreeConfig>(module, "TreeConfig")
        .def(py::init<>())
        .def_readwrite("steps", &aofve::TreeConfig::steps)
        .def_readwrite("even_odd_smoothing", &aofve::TreeConfig::even_odd_smoothing);

    py::class_<aofve::FiniteDifferenceConfig>(module, "FiniteDifferenceConfig")
        .def(py::init<>())
        .def_readwrite("space_steps", &aofve::FiniteDifferenceConfig::space_steps)
        .def_readwrite("time_steps", &aofve::FiniteDifferenceConfig::time_steps)
        .def_readwrite("s_max_multiplier", &aofve::FiniteDifferenceConfig::s_max_multiplier)
        .def_readwrite("relaxation", &aofve::FiniteDifferenceConfig::relaxation)
        .def_readwrite("tolerance", &aofve::FiniteDifferenceConfig::tolerance)
        .def_readwrite("max_iterations", &aofve::FiniteDifferenceConfig::max_iterations);

    py::class_<aofve::SolverResult>(module, "SolverResult")
        .def_readonly("value", &aofve::SolverResult::value)
        .def_readonly("iterations", &aofve::SolverResult::iterations)
        .def_readonly("converged", &aofve::SolverResult::converged);

    py::class_<aofve::ImpliedVolatilityConfig>(module, "ImpliedVolatilityConfig")
        .def(py::init<>())
        .def_readwrite("minimum_volatility", &aofve::ImpliedVolatilityConfig::minimum_volatility)
        .def_readwrite("initial_maximum_volatility", &aofve::ImpliedVolatilityConfig::initial_maximum_volatility)
        .def_readwrite("maximum_volatility", &aofve::ImpliedVolatilityConfig::maximum_volatility)
        .def_readwrite("price_tolerance", &aofve::ImpliedVolatilityConfig::price_tolerance)
        .def_readwrite("volatility_tolerance", &aofve::ImpliedVolatilityConfig::volatility_tolerance)
        .def_readwrite("max_iterations", &aofve::ImpliedVolatilityConfig::max_iterations)
        .def_readwrite("tree", &aofve::ImpliedVolatilityConfig::tree);

    py::class_<aofve::ImpliedVolatilityResult>(module, "ImpliedVolatilityResult")
        .def_readonly("volatility", &aofve::ImpliedVolatilityResult::volatility)
        .def_readonly("price_residual", &aofve::ImpliedVolatilityResult::price_residual)
        .def_readonly("bracket_width", &aofve::ImpliedVolatilityResult::bracket_width)
        .def_readonly("iterations", &aofve::ImpliedVolatilityResult::iterations)
        .def_readonly("converged", &aofve::ImpliedVolatilityResult::converged);

    py::class_<aofve::SVIParameters>(module, "SVIParameters")
        .def(py::init<double, double, double, double, double>(),
             py::arg("a"), py::arg("b"), py::arg("rho"), py::arg("m"), py::arg("sigma"))
        .def_readwrite("a", &aofve::SVIParameters::a)
        .def_readwrite("b", &aofve::SVIParameters::b)
        .def_readwrite("rho", &aofve::SVIParameters::rho)
        .def_readwrite("m", &aofve::SVIParameters::m)
        .def_readwrite("sigma", &aofve::SVIParameters::sigma)
        .def("total_variance", &aofve::SVIParameters::total_variance)
        .def("first_derivative", &aofve::SVIParameters::first_derivative)
        .def("second_derivative", &aofve::SVIParameters::second_derivative)
        .def("minimum_total_variance", &aofve::SVIParameters::minimum_total_variance)
        .def("is_admissible", &aofve::SVIParameters::is_admissible, py::arg("tolerance") = 0.0);

    py::class_<aofve::SVICalibrationConfig>(module, "SVICalibrationConfig")
        .def(py::init<>())
        .def_readwrite("max_iterations", &aofve::SVICalibrationConfig::max_iterations)
        .def_readwrite("tolerance", &aofve::SVICalibrationConfig::tolerance)
        .def_readwrite("variance_floor", &aofve::SVICalibrationConfig::variance_floor)
        .def_readwrite("butterfly_penalty", &aofve::SVICalibrationConfig::butterfly_penalty)
        .def_readwrite("butterfly_grid_min", &aofve::SVICalibrationConfig::butterfly_grid_min)
        .def_readwrite("butterfly_grid_max", &aofve::SVICalibrationConfig::butterfly_grid_max)
        .def_readwrite("butterfly_grid_points", &aofve::SVICalibrationConfig::butterfly_grid_points)
        .def_readwrite("deterministic_restarts", &aofve::SVICalibrationConfig::deterministic_restarts);

    py::class_<aofve::SVICalibrationResult>(module, "SVICalibrationResult")
        .def_readonly("parameters", &aofve::SVICalibrationResult::parameters)
        .def_readonly("rmse", &aofve::SVICalibrationResult::rmse)
        .def_readonly("objective", &aofve::SVICalibrationResult::objective)
        .def_readonly("minimum_density_factor", &aofve::SVICalibrationResult::minimum_density_factor)
        .def_readonly("iterations", &aofve::SVICalibrationResult::iterations)
        .def_readonly("converged", &aofve::SVICalibrationResult::converged);

    py::class_<aofve::SVISurface>(module, "SVISurface")
        .def(py::init<std::vector<double>, std::vector<aofve::SVIParameters>>(),
             py::arg("maturities"), py::arg("slices"))
        .def_property_readonly("maturities", &aofve::SVISurface::maturities)
        .def_property_readonly("slices", &aofve::SVISurface::slices)
        .def("total_variance", &aofve::SVISurface::total_variance)
        .def("implied_volatility", &aofve::SVISurface::implied_volatility);

    py::class_<aofve::SVISurfaceCalibrationConfig>(module, "SVISurfaceCalibrationConfig")
        .def(py::init<>())
        .def_readwrite("slice", &aofve::SVISurfaceCalibrationConfig::slice)
        .def_readwrite("enforce_calendar_monotonicity", &aofve::SVISurfaceCalibrationConfig::enforce_calendar_monotonicity)
        .def_readwrite("calendar_grid_min", &aofve::SVISurfaceCalibrationConfig::calendar_grid_min)
        .def_readwrite("calendar_grid_max", &aofve::SVISurfaceCalibrationConfig::calendar_grid_max)
        .def_readwrite("calendar_grid_points", &aofve::SVISurfaceCalibrationConfig::calendar_grid_points)
        .def_readwrite("calendar_tolerance", &aofve::SVISurfaceCalibrationConfig::calendar_tolerance);

    py::class_<aofve::SVISurfaceCalibrationResult>(module, "SVISurfaceCalibrationResult")
        .def_readonly("surface", &aofve::SVISurfaceCalibrationResult::surface)
        .def_readonly("slice_results", &aofve::SVISurfaceCalibrationResult::slice_results)
        .def_readonly("calendar_shifts", &aofve::SVISurfaceCalibrationResult::calendar_shifts);

    py::class_<aofve::ArbitrageViolation>(module, "ArbitrageViolation")
        .def_readonly("kind", &aofve::ArbitrageViolation::kind)
        .def_readonly("maturity", &aofve::ArbitrageViolation::maturity)
        .def_readonly("coordinate", &aofve::ArbitrageViolation::coordinate)
        .def_readonly("value", &aofve::ArbitrageViolation::value)
        .def_readonly("message", &aofve::ArbitrageViolation::message);

    py::class_<aofve::ArbitrageReport>(module, "ArbitrageReport")
        .def_readonly("arbitrage_free", &aofve::ArbitrageReport::arbitrage_free)
        .def_readonly("minimum_margin", &aofve::ArbitrageReport::minimum_margin)
        .def_readonly("violations", &aofve::ArbitrageReport::violations);

    module.def("european_black_scholes", &aofve::european_black_scholes,
               py::arg("option"), py::arg("market"), py::arg("volatility"));
    module.def("american_binomial", &aofve::american_binomial,
               py::arg("option"), py::arg("market"), py::arg("volatility"),
               py::arg("config") = aofve::TreeConfig{});
    module.def("american_finite_difference", &aofve::american_finite_difference,
               py::arg("option"), py::arg("market"), py::arg("volatility"),
               py::arg("config") = aofve::FiniteDifferenceConfig{});
    module.def("american_implied_volatility", &aofve::american_implied_volatility,
               py::arg("market_price"), py::arg("option"), py::arg("market"),
               py::arg("config") = aofve::ImpliedVolatilityConfig{});
    module.def("svi_density_factor", &aofve::svi_density_factor);
    module.def("svi_log_strike_density", &aofve::svi_log_strike_density);
    module.def(
        "calibrate_svi",
        [](const std::vector<double>& k,
           const std::vector<double>& variance,
           const std::vector<double>& weights,
           const aofve::SVICalibrationConfig& config) {
            py::gil_scoped_release release;
            return aofve::calibrate_svi(k, variance, weights, config);
        },
        py::arg("log_moneyness"), py::arg("total_variance"),
        py::arg("weights") = std::vector<double>{},
        py::arg("config") = aofve::SVICalibrationConfig{});
    module.def(
        "calibrate_svi_surface",
        [](const std::vector<double>& maturities,
           const std::vector<std::vector<double>>& k,
           const std::vector<std::vector<double>>& variance,
           const aofve::SVISurfaceCalibrationConfig& config) {
            py::gil_scoped_release release;
            return aofve::calibrate_svi_surface(maturities, k, variance, config);
        },
        py::arg("maturities"), py::arg("log_moneyness"), py::arg("total_variance"),
        py::arg("config") = aofve::SVISurfaceCalibrationConfig{});
    module.def("check_call_price_arbitrage", [](const std::vector<double>& strikes,
                                                 const std::vector<double>& prices,
                                                 double tolerance) {
        return aofve::check_call_price_arbitrage(strikes, prices, tolerance);
    }, py::arg("strikes"), py::arg("call_prices"), py::arg("tolerance") = 1e-10);
    module.def("check_svi_butterfly_arbitrage", &aofve::check_svi_butterfly_arbitrage,
               py::arg("parameters"), py::arg("log_moneyness_min") = -3.0,
               py::arg("log_moneyness_max") = 3.0, py::arg("grid_points") = 301,
               py::arg("tolerance") = 1e-10);
    module.def("check_calendar_arbitrage", &aofve::check_calendar_arbitrage,
               py::arg("surface"), py::arg("log_moneyness_min") = -2.0,
               py::arg("log_moneyness_max") = 2.0, py::arg("grid_points") = 201,
               py::arg("tolerance") = 1e-10);
    module.def("diagnose_svi_surface", &aofve::diagnose_svi_surface,
               py::arg("surface"), py::arg("log_moneyness_min") = -2.0,
               py::arg("log_moneyness_max") = 2.0, py::arg("grid_points") = 201,
               py::arg("tolerance") = 1e-10);

    module.def(
        "price_batch",
        [](py::array_t<double, py::array::c_style | py::array::forcecast> spots,
           py::array_t<double, py::array::c_style | py::array::forcecast> strikes,
           py::array_t<double, py::array::c_style | py::array::forcecast> maturities,
           py::array_t<double, py::array::c_style | py::array::forcecast> rates,
           py::array_t<double, py::array::c_style | py::array::forcecast> dividends,
           py::array_t<double, py::array::c_style | py::array::forcecast> volatilities,
           py::array_t<std::uint8_t, py::array::c_style | py::array::forcecast> is_calls,
           const aofve::TreeConfig& config,
           std::size_t workers) {
            const py::ssize_t count = spots.size();
            if (spots.ndim() != 1 || strikes.ndim() != 1 || maturities.ndim() != 1 ||
                rates.ndim() != 1 || dividends.ndim() != 1 || volatilities.ndim() != 1 ||
                is_calls.ndim() != 1 || strikes.size() != count || maturities.size() != count ||
                rates.size() != count || dividends.size() != count || volatilities.size() != count ||
                is_calls.size() != count) {
                throw std::invalid_argument("batch inputs must be one-dimensional arrays of equal length");
            }
            const auto spot_view = spots.unchecked<1>();
            const auto strike_view = strikes.unchecked<1>();
            const auto maturity_view = maturities.unchecked<1>();
            const auto rate_view = rates.unchecked<1>();
            const auto dividend_view = dividends.unchecked<1>();
            const auto volatility_view = volatilities.unchecked<1>();
            const auto call_view = is_calls.unchecked<1>();
            std::vector<aofve::PricingRequest> requests(static_cast<std::size_t>(count));
            for (py::ssize_t index = 0; index < count; ++index) {
                requests[static_cast<std::size_t>(index)] = {
                    {strike_view(index), maturity_view(index),
                     call_view(index) != 0 ? aofve::OptionType::call : aofve::OptionType::put},
                    {spot_view(index), rate_view(index), dividend_view(index)},
                    volatility_view(index)};
            }
            std::vector<double> prices;
            {
                py::gil_scoped_release release;
                prices = aofve::price_batch(requests, config, workers);
            }
            py::array_t<double> output(count);
            auto output_view = output.mutable_unchecked<1>();
            for (py::ssize_t index = 0; index < count; ++index) {
                output_view(index) = prices[static_cast<std::size_t>(index)];
            }
            return output;
        },
        py::arg("spots"), py::arg("strikes"), py::arg("maturities"),
        py::arg("rates"), py::arg("dividend_yields"), py::arg("volatilities"),
        py::arg("is_calls"), py::arg("config") = aofve::TreeConfig{},
        py::arg("workers") = 1);
}

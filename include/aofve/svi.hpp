#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace aofve {

struct SVIParameters {
    double a{};
    double b{};
    double rho{};
    double m{};
    double sigma{};

    [[nodiscard]] double total_variance(double log_moneyness) const noexcept;
    [[nodiscard]] double first_derivative(double log_moneyness) const noexcept;
    [[nodiscard]] double second_derivative(double log_moneyness) const noexcept;
    [[nodiscard]] double minimum_total_variance() const noexcept;
    [[nodiscard]] bool is_admissible(double tolerance = 0.0) const noexcept;
};

struct SVICalibrationConfig {
    std::size_t max_iterations{1'000};
    double tolerance{1e-10};
    double variance_floor{1e-10};
    double butterfly_penalty{10'000.0};
    double butterfly_grid_min{-3.0};
    double butterfly_grid_max{3.0};
    std::size_t butterfly_grid_points{121};
    std::size_t deterministic_restarts{5};
};

struct SVICalibrationResult {
    SVIParameters parameters;
    double rmse{};
    double objective{};
    double minimum_density_factor{};
    std::size_t iterations{};
    bool converged{};
};

[[nodiscard]] double svi_density_factor(const SVIParameters& parameters, double log_moneyness) noexcept;

[[nodiscard]] double svi_log_strike_density(
    const SVIParameters& parameters,
    double log_moneyness) noexcept;

[[nodiscard]] SVICalibrationResult calibrate_svi(
    std::span<const double> log_moneyness,
    std::span<const double> total_variance,
    std::span<const double> weights = {},
    const SVICalibrationConfig& config = {});

class SVISurface {
public:
    SVISurface() = default;
    SVISurface(std::vector<double> maturities, std::vector<SVIParameters> slices);

    [[nodiscard]] const std::vector<double>& maturities() const noexcept { return maturities_; }
    [[nodiscard]] const std::vector<SVIParameters>& slices() const noexcept { return slices_; }
    [[nodiscard]] double total_variance(double log_moneyness, double maturity) const;
    [[nodiscard]] double implied_volatility(double log_moneyness, double maturity) const;

private:
    std::vector<double> maturities_;
    std::vector<SVIParameters> slices_;
};

struct SVISurfaceCalibrationConfig {
    SVICalibrationConfig slice{};
    bool enforce_calendar_monotonicity{true};
    double calendar_grid_min{-2.0};
    double calendar_grid_max{2.0};
    std::size_t calendar_grid_points{161};
    double calendar_tolerance{1e-10};
};

struct SVISurfaceCalibrationResult {
    SVISurface surface;
    std::vector<SVICalibrationResult> slice_results;
    std::vector<double> calendar_shifts;
};

[[nodiscard]] SVISurfaceCalibrationResult calibrate_svi_surface(
    std::span<const double> maturities,
    const std::vector<std::vector<double>>& log_moneyness,
    const std::vector<std::vector<double>>& total_variance,
    const SVISurfaceCalibrationConfig& config = {});

}  // namespace aofve

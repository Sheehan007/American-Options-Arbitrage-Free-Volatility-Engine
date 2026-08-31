#include "aofve/svi.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace aofve {
namespace {

using Point = std::array<double, 5>;

struct Vertex {
    Point point{};
    double objective{};
};

double softplus(double value) noexcept {
    if (value > 30.0) {
        return value;
    }
    if (value < -30.0) {
        return std::exp(value);
    }
    return std::log1p(std::exp(value));
}

double inverse_softplus(double value) noexcept {
    value = std::max(value, 1e-14);
    if (value > 30.0) {
        return value;
    }
    return std::log(std::expm1(value));
}

SVIParameters decode(const Point& point, double variance_floor) noexcept {
    const double b = std::exp(std::clamp(point[1], -16.0, 2.0));
    const double rho = 0.999 * std::tanh(point[2]);
    const double m = std::clamp(point[3], -5.0, 5.0);
    const double sigma = std::exp(std::clamp(point[4], -10.0, 2.0));
    const double minimum_variance = variance_floor + softplus(point[0]);
    const double a = minimum_variance - b * sigma * std::sqrt(1.0 - rho * rho);
    return {a, b, rho, m, sigma};
}

double weighted_rmse(
    const SVIParameters& parameters,
    std::span<const double> log_moneyness,
    std::span<const double> total_variance,
    std::span<const double> weights) {
    double squared_error = 0.0;
    double weight_sum = 0.0;
    for (std::size_t index = 0; index < log_moneyness.size(); ++index) {
        const double weight = weights.empty() ? 1.0 : weights[index];
        const double residual = parameters.total_variance(log_moneyness[index]) - total_variance[index];
        squared_error += weight * residual * residual;
        weight_sum += weight;
    }
    return std::sqrt(squared_error / weight_sum);
}

double minimum_density_factor(
    const SVIParameters& parameters,
    double minimum,
    double maximum,
    std::size_t points) noexcept {
    double result = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < points; ++index) {
        const double fraction = points == 1
            ? 0.5
            : static_cast<double>(index) / static_cast<double>(points - 1);
        const double k = minimum + fraction * (maximum - minimum);
        result = std::min(result, svi_density_factor(parameters, k));
    }
    return result;
}

template <typename Objective>
std::pair<Vertex, std::pair<std::size_t, bool>> nelder_mead(
    const Point& start,
    const Point& scales,
    Objective&& objective,
    std::size_t max_iterations,
    double tolerance) {
    constexpr std::size_t dimensions = 5;
    std::array<Vertex, dimensions + 1> simplex{};
    simplex[0] = {start, objective(start)};
    for (std::size_t index = 0; index < dimensions; ++index) {
        Point point = start;
        point[index] += scales[index];
        simplex[index + 1] = {point, objective(point)};
    }

    auto add_scaled = [](const Point& first, const Point& second, double scale) {
        Point result{};
        for (std::size_t index = 0; index < dimensions; ++index) {
            result[index] = first[index] + scale * second[index];
        }
        return result;
    };
    auto difference = [](const Point& first, const Point& second) {
        Point result{};
        for (std::size_t index = 0; index < dimensions; ++index) {
            result[index] = first[index] - second[index];
        }
        return result;
    };

    for (std::size_t iteration = 0; iteration < max_iterations; ++iteration) {
        std::sort(simplex.begin(), simplex.end(), [](const Vertex& lhs, const Vertex& rhs) {
            return lhs.objective < rhs.objective;
        });

        double objective_spread = 0.0;
        double coordinate_spread = 0.0;
        for (std::size_t vertex = 1; vertex <= dimensions; ++vertex) {
            objective_spread = std::max(
                objective_spread,
                std::abs(simplex[vertex].objective - simplex[0].objective));
            for (std::size_t coordinate = 0; coordinate < dimensions; ++coordinate) {
                coordinate_spread = std::max(
                    coordinate_spread,
                    std::abs(simplex[vertex].point[coordinate] - simplex[0].point[coordinate]));
            }
        }
        if (objective_spread < tolerance && coordinate_spread < std::sqrt(tolerance)) {
            return {simplex[0], {iteration, true}};
        }

        Point centroid{};
        for (std::size_t vertex = 0; vertex < dimensions; ++vertex) {
            for (std::size_t coordinate = 0; coordinate < dimensions; ++coordinate) {
                centroid[coordinate] += simplex[vertex].point[coordinate] /
                    static_cast<double>(dimensions);
            }
        }

        const Point reflected_point = add_scaled(
            centroid, difference(centroid, simplex[dimensions].point), 1.0);
        const Vertex reflected{reflected_point, objective(reflected_point)};
        if (reflected.objective < simplex[0].objective) {
            const Point expanded_point = add_scaled(
                centroid, difference(reflected.point, centroid), 2.0);
            const Vertex expanded{expanded_point, objective(expanded_point)};
            simplex[dimensions] = expanded.objective < reflected.objective ? expanded : reflected;
            continue;
        }
        if (reflected.objective < simplex[dimensions - 1].objective) {
            simplex[dimensions] = reflected;
            continue;
        }

        const bool outside = reflected.objective < simplex[dimensions].objective;
        const Point contraction_target = outside ? reflected.point : simplex[dimensions].point;
        const Point contracted_point = add_scaled(
            centroid, difference(contraction_target, centroid), 0.5);
        const Vertex contracted{contracted_point, objective(contracted_point)};
        const double contraction_bound = outside
            ? reflected.objective
            : simplex[dimensions].objective;
        if (contracted.objective < contraction_bound) {
            simplex[dimensions] = contracted;
            continue;
        }

        for (std::size_t vertex = 1; vertex <= dimensions; ++vertex) {
            simplex[vertex].point = add_scaled(
                simplex[0].point,
                difference(simplex[vertex].point, simplex[0].point),
                0.5);
            simplex[vertex].objective = objective(simplex[vertex].point);
        }
    }

    std::sort(simplex.begin(), simplex.end(), [](const Vertex& lhs, const Vertex& rhs) {
        return lhs.objective < rhs.objective;
    });
    return {simplex[0], {max_iterations, false}};
}

}  // namespace

double SVIParameters::total_variance(double log_moneyness) const noexcept {
    const double centered = log_moneyness - m;
    return a + b * (rho * centered + std::sqrt(centered * centered + sigma * sigma));
}

double SVIParameters::first_derivative(double log_moneyness) const noexcept {
    const double centered = log_moneyness - m;
    return b * (rho + centered / std::sqrt(centered * centered + sigma * sigma));
}

double SVIParameters::second_derivative(double log_moneyness) const noexcept {
    const double centered = log_moneyness - m;
    const double denominator = std::pow(centered * centered + sigma * sigma, 1.5);
    return b * sigma * sigma / denominator;
}

double SVIParameters::minimum_total_variance() const noexcept {
    return a + b * sigma * std::sqrt(std::max(0.0, 1.0 - rho * rho));
}

bool SVIParameters::is_admissible(double tolerance) const noexcept {
    return std::isfinite(a) && std::isfinite(b) && std::isfinite(rho) &&
        std::isfinite(m) && std::isfinite(sigma) && b >= 0.0 &&
        std::abs(rho) < 1.0 && sigma > 0.0 && minimum_total_variance() >= -tolerance;
}

double svi_density_factor(const SVIParameters& parameters, double log_moneyness) noexcept {
    const double variance = parameters.total_variance(log_moneyness);
    if (!(variance > 0.0)) {
        return -std::numeric_limits<double>::infinity();
    }
    const double first = parameters.first_derivative(log_moneyness);
    const double second = parameters.second_derivative(log_moneyness);
    const double leading = 1.0 - log_moneyness * first / (2.0 * variance);
    return leading * leading -
        0.25 * first * first * (1.0 / variance + 0.25) + 0.5 * second;
}

double svi_log_strike_density(
    const SVIParameters& parameters,
    double log_moneyness) noexcept {
    const double variance = parameters.total_variance(log_moneyness);
    if (!(variance > 0.0)) {
        return -std::numeric_limits<double>::infinity();
    }
    const double root_variance = std::sqrt(variance);
    const double d2 = -log_moneyness / root_variance - 0.5 * root_variance;
    return svi_density_factor(parameters, log_moneyness) *
        std::exp(-0.5 * d2 * d2) /
        std::sqrt(2.0 * std::numbers::pi * variance);
}

SVICalibrationResult calibrate_svi(
    std::span<const double> log_moneyness,
    std::span<const double> total_variance,
    std::span<const double> weights,
    const SVICalibrationConfig& config) {
    if (log_moneyness.size() != total_variance.size() || log_moneyness.size() < 5) {
        throw std::invalid_argument("SVI calibration needs matching arrays with at least five observations");
    }
    if (!weights.empty() && weights.size() != log_moneyness.size()) {
        throw std::invalid_argument("SVI weights must be empty or match the observation count");
    }
    if (config.max_iterations == 0 || config.deterministic_restarts == 0 ||
        !(config.tolerance > 0.0) || !(config.variance_floor >= 0.0) ||
        !(config.butterfly_penalty >= 0.0) || config.butterfly_grid_points < 2 ||
        !(config.butterfly_grid_max > config.butterfly_grid_min)) {
        throw std::invalid_argument("invalid SVI calibration configuration");
    }
    for (std::size_t index = 0; index < log_moneyness.size(); ++index) {
        if (!std::isfinite(log_moneyness[index]) || !std::isfinite(total_variance[index]) ||
            total_variance[index] <= 0.0) {
            throw std::invalid_argument("SVI observations must be finite with positive total variance");
        }
        if (!weights.empty() && (!std::isfinite(weights[index]) || weights[index] <= 0.0)) {
            throw std::invalid_argument("SVI weights must be finite and positive");
        }
    }

    const auto [minimum_x, maximum_x] = std::minmax_element(
        log_moneyness.begin(), log_moneyness.end());
    const auto [minimum_w, maximum_w] = std::minmax_element(
        total_variance.begin(), total_variance.end());
    const auto minimum_index = static_cast<std::size_t>(
        std::distance(total_variance.begin(), minimum_w));
    const double x_range = std::max(*maximum_x - *minimum_x, 0.1);
    const double initial_b = std::max((*maximum_w - *minimum_w) / x_range, 0.02);
    const double initial_sigma = std::max(0.2 * x_range, 0.05);
    const double initial_minimum_variance = std::max(
        config.variance_floor + 1e-8,
        0.8 * *minimum_w);

    Point base{
        inverse_softplus(initial_minimum_variance - config.variance_floor),
        std::log(initial_b),
        0.0,
        log_moneyness[minimum_index],
        std::log(initial_sigma)};
    const Point scales{0.8, 0.35, 0.45, 0.15 * x_range, 0.35};

    const double weight_sum = weights.empty()
        ? static_cast<double>(log_moneyness.size())
        : std::accumulate(weights.begin(), weights.end(), 0.0);
    const auto objective = [&](const Point& point) {
        const SVIParameters parameters = decode(point, config.variance_floor);
        double value = 0.0;
        for (std::size_t index = 0; index < log_moneyness.size(); ++index) {
            const double weight = weights.empty() ? 1.0 : weights[index];
            const double residual =
                parameters.total_variance(log_moneyness[index]) - total_variance[index];
            value += weight * residual * residual;
        }
        value /= weight_sum;

        double butterfly_violation = 0.0;
        for (std::size_t index = 0; index < config.butterfly_grid_points; ++index) {
            const double fraction = static_cast<double>(index) /
                static_cast<double>(config.butterfly_grid_points - 1);
            const double k = config.butterfly_grid_min +
                fraction * (config.butterfly_grid_max - config.butterfly_grid_min);
            const double density_factor = svi_density_factor(parameters, k);
            if (density_factor < 0.0) {
                butterfly_violation += density_factor * density_factor;
            }
        }
        value += config.butterfly_penalty * butterfly_violation /
            static_cast<double>(config.butterfly_grid_points);
        if (std::abs(point[3]) > 5.0) {
            const double excess = std::abs(point[3]) - 5.0;
            value += excess * excess;
        }
        return std::isfinite(value) ? value : std::numeric_limits<double>::max();
    };

    Vertex best{};
    best.objective = std::numeric_limits<double>::infinity();
    std::size_t best_iterations = 0;
    bool best_converged = false;
    for (std::size_t restart = 0; restart < config.deterministic_restarts; ++restart) {
        Point start = base;
        if (restart > 0) {
            const double phase = static_cast<double>(restart);
            start[2] = (restart % 2 == 0 ? 0.45 : -0.45) * (1.0 + 0.1 * phase);
            start[3] += (restart % 2 == 0 ? 1.0 : -1.0) * 0.1 * phase * x_range;
            start[1] += 0.12 * static_cast<double>(static_cast<int>(restart % 3) - 1);
        }
        auto [candidate, metadata] = nelder_mead(
            start, scales, objective, config.max_iterations, config.tolerance);
        if (candidate.objective < best.objective) {
            best = candidate;
            best_iterations = metadata.first;
            best_converged = metadata.second;
        }
    }

    const SVIParameters parameters = decode(best.point, config.variance_floor);
    return {
        parameters,
        weighted_rmse(parameters, log_moneyness, total_variance, weights),
        best.objective,
        minimum_density_factor(
            parameters,
            config.butterfly_grid_min,
            config.butterfly_grid_max,
            config.butterfly_grid_points),
        best_iterations,
        best_converged};
}

SVISurface::SVISurface(
    std::vector<double> maturities,
    std::vector<SVIParameters> slices)
    : maturities_(std::move(maturities)), slices_(std::move(slices)) {
    if (maturities_.empty() || maturities_.size() != slices_.size()) {
        throw std::invalid_argument("an SVI surface needs equally sized, non-empty maturities and slices");
    }
    for (std::size_t index = 0; index < maturities_.size(); ++index) {
        if (!std::isfinite(maturities_[index]) || maturities_[index] <= 0.0 ||
            (index > 0 && maturities_[index] <= maturities_[index - 1])) {
            throw std::invalid_argument("SVI maturities must be finite, positive, and strictly increasing");
        }
        if (!slices_[index].is_admissible(1e-12)) {
            throw std::invalid_argument("SVI surface contains a non-admissible slice");
        }
    }
}

double SVISurface::total_variance(double log_moneyness, double maturity) const {
    if (!std::isfinite(log_moneyness) || !std::isfinite(maturity) || maturity < 0.0) {
        throw std::invalid_argument("surface coordinates must be finite and maturity non-negative");
    }
    if (maturity == 0.0) {
        return 0.0;
    }
    if (maturity <= maturities_.front()) {
        return slices_.front().total_variance(log_moneyness) * maturity / maturities_.front();
    }
    if (maturity >= maturities_.back()) {
        return slices_.back().total_variance(log_moneyness) * maturity / maturities_.back();
    }
    const auto upper = std::upper_bound(maturities_.begin(), maturities_.end(), maturity);
    const std::size_t upper_index = static_cast<std::size_t>(
        std::distance(maturities_.begin(), upper));
    const std::size_t lower_index = upper_index - 1;
    const double fraction = (maturity - maturities_[lower_index]) /
        (maturities_[upper_index] - maturities_[lower_index]);
    return (1.0 - fraction) * slices_[lower_index].total_variance(log_moneyness) +
        fraction * slices_[upper_index].total_variance(log_moneyness);
}

double SVISurface::implied_volatility(double log_moneyness, double maturity) const {
    if (!(maturity > 0.0)) {
        throw std::invalid_argument("implied volatility requires positive maturity");
    }
    return std::sqrt(std::max(0.0, total_variance(log_moneyness, maturity) / maturity));
}

SVISurfaceCalibrationResult calibrate_svi_surface(
    std::span<const double> maturities,
    const std::vector<std::vector<double>>& log_moneyness,
    const std::vector<std::vector<double>>& total_variance,
    const SVISurfaceCalibrationConfig& config) {
    if (maturities.empty() || maturities.size() != log_moneyness.size() ||
        maturities.size() != total_variance.size()) {
        throw std::invalid_argument("surface maturities and slice data must be equally sized and non-empty");
    }
    if (config.calendar_grid_points < 2 ||
        !(config.calendar_grid_max > config.calendar_grid_min) ||
        !(config.calendar_tolerance >= 0.0)) {
        throw std::invalid_argument("invalid calendar calibration configuration");
    }

    std::vector<std::size_t> order(maturities.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t lhs, std::size_t rhs) {
        return maturities[lhs] < maturities[rhs];
    });

    std::vector<double> sorted_maturities;
    std::vector<SVIParameters> slices;
    std::vector<SVICalibrationResult> results;
    std::vector<double> shifts(maturities.size(), 0.0);
    sorted_maturities.reserve(maturities.size());
    slices.reserve(maturities.size());
    results.reserve(maturities.size());

    for (std::size_t rank = 0; rank < order.size(); ++rank) {
        const std::size_t index = order[rank];
        if (!std::isfinite(maturities[index]) || maturities[index] <= 0.0 ||
            (rank > 0 && maturities[index] <= sorted_maturities.back())) {
            throw std::invalid_argument("surface maturities must be finite, positive, and unique");
        }
        auto result = calibrate_svi(
            log_moneyness[index], total_variance[index], {}, config.slice);
        double shift = 0.0;
        if (config.enforce_calendar_monotonicity && !slices.empty()) {
            for (std::size_t grid_index = 0; grid_index < config.calendar_grid_points; ++grid_index) {
                const double fraction = static_cast<double>(grid_index) /
                    static_cast<double>(config.calendar_grid_points - 1);
                const double k = config.calendar_grid_min + fraction *
                    (config.calendar_grid_max - config.calendar_grid_min);
                shift = std::max(
                    shift,
                    slices.back().total_variance(k) -
                        result.parameters.total_variance(k) + config.calendar_tolerance);
            }
            shift = std::max(shift, 0.0);
            result.parameters.a += shift;
            result.rmse = weighted_rmse(
                result.parameters, log_moneyness[index], total_variance[index], {});
            result.minimum_density_factor = minimum_density_factor(
                result.parameters,
                config.slice.butterfly_grid_min,
                config.slice.butterfly_grid_max,
                config.slice.butterfly_grid_points);
        }
        sorted_maturities.push_back(maturities[index]);
        slices.push_back(result.parameters);
        results.push_back(result);
        shifts[rank] = shift;
    }

    return {SVISurface(std::move(sorted_maturities), std::move(slices)),
            std::move(results), std::move(shifts)};
}

}  // namespace aofve

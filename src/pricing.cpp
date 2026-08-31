#include "aofve/pricing.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace aofve {
namespace {

constexpr double kSqrtTwo = 1.4142135623730950488;

double normal_cdf(double value) noexcept {
    return 0.5 * std::erfc(-value / kSqrtTwo);
}

void validate_inputs(const OptionSpec& option, const MarketData& market, double volatility) {
    if (!std::isfinite(option.strike) || option.strike <= 0.0) {
        throw std::invalid_argument("strike must be finite and positive");
    }
    if (!std::isfinite(option.maturity) || option.maturity < 0.0) {
        throw std::invalid_argument("maturity must be finite and non-negative");
    }
    if (!std::isfinite(market.spot) || market.spot <= 0.0) {
        throw std::invalid_argument("spot must be finite and positive");
    }
    if (!std::isfinite(market.rate) || !std::isfinite(market.dividend_yield)) {
        throw std::invalid_argument("rates must be finite");
    }
    if (!std::isfinite(volatility) || volatility < 0.0) {
        throw std::invalid_argument("volatility must be finite and non-negative");
    }
}

double deterministic_american(
    const OptionSpec& option,
    const MarketData& market,
    std::size_t steps) {
    double best = intrinsic_value(option, market.spot);
    for (std::size_t index = 1; index <= steps; ++index) {
        const double time = option.maturity * static_cast<double>(index) / static_cast<double>(steps);
        const double future_spot = market.spot * std::exp((market.rate - market.dividend_yield) * time);
        best = std::max(best, std::exp(-market.rate * time) * intrinsic_value(option, future_spot));
    }
    return best;
}

double tree_price_once(
    const OptionSpec& option,
    const MarketData& market,
    double volatility,
    std::size_t steps) {
    if (steps == 0) {
        throw std::invalid_argument("tree steps must be positive");
    }
    if (option.maturity == 0.0) {
        return intrinsic_value(option, market.spot);
    }
    if (volatility * std::sqrt(option.maturity) < 1e-10) {
        return deterministic_american(option, market, steps);
    }

    const double dt = option.maturity / static_cast<double>(steps);
    const double root_dt = std::sqrt(dt);
    double up = std::exp(volatility * root_dt);
    double down = 1.0 / up;
    double probability =
        (std::exp((market.rate - market.dividend_yield) * dt) - down) / (up - down);

    // Extreme carry/volatility combinations can violate the CRR probability bound.
    // The equal-probability, drift-matched tree is a stable recombining fallback.
    if (!(probability >= 0.0 && probability <= 1.0)) {
        const double drift =
            (market.rate - market.dividend_yield - 0.5 * volatility * volatility) * dt;
        up = std::exp(drift + volatility * root_dt);
        down = std::exp(drift - volatility * root_dt);
        probability = 0.5;
    }

    const double discount = std::exp(-market.rate * dt);
    const double ratio = up / down;
    std::vector<double> values(steps + 1);

    double node_spot = market.spot * std::pow(down, static_cast<double>(steps));
    for (std::size_t node = 0; node <= steps; ++node) {
        values[node] = intrinsic_value(option, node_spot);
        node_spot *= ratio;
    }

    for (std::size_t level = steps; level-- > 0;) {
        node_spot = market.spot * std::pow(down, static_cast<double>(level));
        for (std::size_t node = 0; node <= level; ++node) {
            const double continuation = discount *
                (probability * values[node + 1] + (1.0 - probability) * values[node]);
            values[node] = std::max(continuation, intrinsic_value(option, node_spot));
            node_spot *= ratio;
        }
    }
    return values.front();
}

double upper_boundary(
    const OptionSpec& option,
    const MarketData& market,
    double maximum_spot,
    double time_to_expiry) {
    if (option.type == OptionType::put) {
        return 0.0;
    }
    const double european_asymptote =
        maximum_spot * std::exp(-market.dividend_yield * time_to_expiry) -
        option.strike * std::exp(-market.rate * time_to_expiry);
    return std::max(maximum_spot - option.strike, european_asymptote);
}

double lower_boundary(const OptionSpec& option) {
    return option.type == OptionType::put ? option.strike : 0.0;
}

}  // namespace

double intrinsic_value(const OptionSpec& option, double spot) noexcept {
    if (option.type == OptionType::call) {
        return std::max(spot - option.strike, 0.0);
    }
    return std::max(option.strike - spot, 0.0);
}

double european_black_scholes(
    const OptionSpec& option,
    const MarketData& market,
    double volatility) {
    validate_inputs(option, market, volatility);
    if (option.maturity == 0.0) {
        return intrinsic_value(option, market.spot);
    }

    const double discount_rate = std::exp(-market.rate * option.maturity);
    const double discount_dividend = std::exp(-market.dividend_yield * option.maturity);
    if (volatility * std::sqrt(option.maturity) < 1e-12) {
        const double discounted_spot = market.spot * discount_dividend;
        const double discounted_strike = option.strike * discount_rate;
        return option.type == OptionType::call
            ? std::max(discounted_spot - discounted_strike, 0.0)
            : std::max(discounted_strike - discounted_spot, 0.0);
    }

    const double standard_deviation = volatility * std::sqrt(option.maturity);
    const double d1 =
        (std::log(market.spot / option.strike) +
         (market.rate - market.dividend_yield + 0.5 * volatility * volatility) * option.maturity) /
        standard_deviation;
    const double d2 = d1 - standard_deviation;
    if (option.type == OptionType::call) {
        return market.spot * discount_dividend * normal_cdf(d1) -
            option.strike * discount_rate * normal_cdf(d2);
    }
    return option.strike * discount_rate * normal_cdf(-d2) -
        market.spot * discount_dividend * normal_cdf(-d1);
}

double american_binomial(
    const OptionSpec& option,
    const MarketData& market,
    double volatility,
    const TreeConfig& config) {
    validate_inputs(option, market, volatility);
    if (config.steps == 0) {
        throw std::invalid_argument("tree steps must be positive");
    }
    const double first = tree_price_once(option, market, volatility, config.steps);
    if (!config.even_odd_smoothing || option.maturity == 0.0) {
        return first;
    }
    const double second = tree_price_once(option, market, volatility, config.steps + 1);
    return 0.5 * (first + second);
}

SolverResult american_finite_difference(
    const OptionSpec& option,
    const MarketData& market,
    double volatility,
    const FiniteDifferenceConfig& config) {
    validate_inputs(option, market, volatility);
    if (config.space_steps < 3 || config.time_steps == 0) {
        throw std::invalid_argument("finite-difference grid must have at least 3 space steps and 1 time step");
    }
    if (!(config.s_max_multiplier > 1.0) || !(config.relaxation > 0.0 && config.relaxation < 2.0) ||
        !(config.tolerance > 0.0) || config.max_iterations == 0) {
        throw std::invalid_argument("invalid finite-difference configuration");
    }
    if (option.maturity == 0.0) {
        return {intrinsic_value(option, market.spot), 0, true};
    }

    const std::size_t space_steps = config.space_steps;
    const double diffusion_scale = std::exp(
        std::max(0.0, (market.rate - market.dividend_yield) * option.maturity) +
        4.0 * volatility * std::sqrt(option.maturity));
    const double maximum_spot = std::max({
        config.s_max_multiplier * option.strike,
        config.s_max_multiplier * market.spot,
        1.25 * market.spot * diffusion_scale});
    const double ds = maximum_spot / static_cast<double>(space_steps);
    const double dt = option.maturity / static_cast<double>(config.time_steps);

    std::vector<double> previous(space_steps + 1);
    std::vector<double> current(space_steps + 1);
    std::vector<double> rhs(space_steps + 1);
    for (std::size_t index = 0; index <= space_steps; ++index) {
        previous[index] = intrinsic_value(option, ds * static_cast<double>(index));
    }

    bool all_converged = true;
    std::size_t total_iterations = 0;
    for (std::size_t time_index = 1; time_index <= config.time_steps; ++time_index) {
        const double time_to_expiry = dt * static_cast<double>(time_index);
        current = previous;
        current.front() = lower_boundary(option);
        current.back() = upper_boundary(option, market, maximum_spot, time_to_expiry);

        for (std::size_t index = 1; index < space_steps; ++index) {
            const double i = static_cast<double>(index);
            const double variance = volatility * volatility * i * i;
            const double carry = (market.rate - market.dividend_yield) * i;
            const double alpha = 0.25 * dt * (variance - carry);
            const double beta = -0.5 * dt * (variance + market.rate);
            const double gamma = 0.25 * dt * (variance + carry);
            rhs[index] = alpha * previous[index - 1] +
                (1.0 + beta) * previous[index] + gamma * previous[index + 1];
        }

        bool step_converged = false;
        for (std::size_t iteration = 0; iteration < config.max_iterations; ++iteration) {
            double maximum_change = 0.0;
            for (std::size_t index = 1; index < space_steps; ++index) {
                const double i = static_cast<double>(index);
                const double variance = volatility * volatility * i * i;
                const double carry = (market.rate - market.dividend_yield) * i;
                const double alpha = 0.25 * dt * (variance - carry);
                const double beta = -0.5 * dt * (variance + market.rate);
                const double gamma = 0.25 * dt * (variance + carry);
                const double unconstrained =
                    (rhs[index] + alpha * current[index - 1] + gamma * current[index + 1]) /
                    (1.0 - beta);
                const double relaxed = current[index] +
                    config.relaxation * (unconstrained - current[index]);
                const double projected = std::max(
                    relaxed,
                    intrinsic_value(option, ds * static_cast<double>(index)));
                maximum_change = std::max(maximum_change, std::abs(projected - current[index]));
                current[index] = projected;
            }
            ++total_iterations;
            if (maximum_change < config.tolerance) {
                step_converged = true;
                break;
            }
        }
        all_converged = all_converged && step_converged;
        previous.swap(current);
    }

    const double grid_coordinate = market.spot / ds;
    const auto lower_index = std::min(
        static_cast<std::size_t>(std::floor(grid_coordinate)),
        space_steps - 1);
    const double fraction = std::clamp(grid_coordinate - static_cast<double>(lower_index), 0.0, 1.0);
    const double price = previous[lower_index] * (1.0 - fraction) + previous[lower_index + 1] * fraction;
    return {price, total_iterations, all_converged};
}

std::vector<double> price_batch(
    std::span<const PricingRequest> requests,
    const TreeConfig& config,
    std::size_t workers) {
    std::vector<double> prices(requests.size());
    if (requests.empty()) {
        return prices;
    }
    if (workers == 0) {
        workers = std::max(1u, std::thread::hardware_concurrency());
    }
    workers = std::min(workers, requests.size());
    if (workers == 1) {
        for (std::size_t index = 0; index < requests.size(); ++index) {
            const auto& request = requests[index];
            prices[index] = american_binomial(
                request.option, request.market, request.volatility, config);
        }
        return prices;
    }

    std::atomic_size_t next_index{0};
    std::exception_ptr failure;
    std::mutex failure_mutex;
    std::vector<std::jthread> threads;
    threads.reserve(workers);
    for (std::size_t worker = 0; worker < workers; ++worker) {
        threads.emplace_back([&] {
            try {
                while (true) {
                    const std::size_t index = next_index.fetch_add(1);
                    if (index >= requests.size()) {
                        break;
                    }
                    const auto& request = requests[index];
                    prices[index] = american_binomial(
                        request.option, request.market, request.volatility, config);
                }
            } catch (...) {
                std::scoped_lock lock(failure_mutex);
                if (!failure) {
                    failure = std::current_exception();
                }
                next_index.store(requests.size());
            }
        });
    }
    threads.clear();
    if (failure) {
        std::rethrow_exception(failure);
    }
    return prices;
}

}  // namespace aofve

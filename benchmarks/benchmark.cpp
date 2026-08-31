#include "aofve/aofve.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    using Clock = std::chrono::steady_clock;
    constexpr std::size_t count = 256;
    std::vector<aofve::PricingRequest> requests;
    requests.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        requests.push_back({
            {75.0 + static_cast<double>(index % 51),
             0.25 + 0.05 * static_cast<double>(index % 20),
             index % 2 == 0 ? aofve::OptionType::call : aofve::OptionType::put},
            {100.0 + 0.1 * static_cast<double>(index % 11), 0.04, 0.012},
            0.15 + 0.005 * static_cast<double>(index % 30)});
    }
    const aofve::TreeConfig config{300, true};

    const auto scalar_start = Clock::now();
    std::vector<double> scalar;
    scalar.reserve(count);
    for (const auto& request : requests) {
        scalar.push_back(aofve::american_binomial(
            request.option, request.market, request.volatility, config));
    }
    const auto scalar_end = Clock::now();

    const auto batch_start = Clock::now();
    const auto batch = aofve::price_batch(requests, config, 0);
    const auto batch_end = Clock::now();

    double maximum_error = 0.0;
    for (std::size_t index = 0; index < count; ++index) {
        maximum_error = std::max(maximum_error, std::abs(scalar[index] - batch[index]));
    }
    const auto scalar_us = std::chrono::duration_cast<std::chrono::microseconds>(
        scalar_end - scalar_start).count();
    const auto batch_us = std::chrono::duration_cast<std::chrono::microseconds>(
        batch_end - batch_start).count();

    std::cout << "options,steps,workers,scalar_us,batch_us,speedup,max_abs_error\n"
              << count << ',' << config.steps << ','
              << std::max(1u, std::thread::hardware_concurrency()) << ','
              << scalar_us << ',' << batch_us << ',' << std::fixed << std::setprecision(3)
              << static_cast<double>(scalar_us) / static_cast<double>(batch_us) << ','
              << std::scientific << maximum_error << '\n';
}

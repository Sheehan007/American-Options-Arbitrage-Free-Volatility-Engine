#include "aofve/aofve.hpp"

#include <iomanip>
#include <iostream>
#include <vector>

int main() {
    using namespace aofve;

    const MarketData market{100.0, 0.045, 0.018};
    const OptionSpec put{105.0, 0.75, OptionType::put};
    const TreeConfig tree{500, true};
    const double price = american_binomial(put, market, 0.28, tree);

    ImpliedVolatilityConfig iv_config;
    iv_config.tree = tree;
    const auto implied = american_implied_volatility(price, put, market, iv_config);

    const SVIParameters short_slice{0.020, 0.10, -0.35, 0.0, 0.22};
    const SVIParameters long_slice{0.050, 0.11, -0.30, 0.0, 0.25};
    const SVISurface surface({0.5, 1.0}, {short_slice, long_slice});
    const auto diagnostics = diagnose_svi_surface(surface);

    std::cout << std::fixed << std::setprecision(8)
              << "American put price: " << price << '\n'
              << "Recovered volatility: " << implied.volatility << '\n'
              << "Surface 9M ATM volatility: " << surface.implied_volatility(0.0, 0.75) << '\n'
              << "Arbitrage-free on diagnostic grid: " << std::boolalpha
              << diagnostics.arbitrage_free << '\n';
}

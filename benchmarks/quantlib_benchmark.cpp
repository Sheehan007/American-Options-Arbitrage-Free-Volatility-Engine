#include "aofve/aofve.hpp"

#include <ql/exercise.hpp>
#include <ql/instruments/payoffs.hpp>
#include <ql/instruments/vanillaoption.hpp>
#include <ql/math/interpolations/linearinterpolation.hpp>
#include <ql/methods/lattices/binomialtree.hpp>
#include <ql/pricingengines/vanilla/binomialengine.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/termstructures/volatility/equityfx/blackconstantvol.hpp>
#include <ql/termstructures/yield/flatforward.hpp>
#include <ql/time/calendars/nullcalendar.hpp>
#include <ql/time/daycounters/actual365fixed.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

int main() {
    using Clock = std::chrono::steady_clock;
    using namespace QuantLib;

    const Date today(2, January, 2025);
    Settings::instance().evaluationDate() = today;
    const Date expiry = today + 365;
    const DayCounter day_count = Actual365Fixed();
    const NullCalendar calendar;
    constexpr Size steps = 500;

    std::vector<double> strikes;
    for (double strike = 70.0; strike <= 130.0; strike += 2.0) {
        strikes.push_back(strike);
    }

    const aofve::MarketData market{100.0, 0.04, 0.015};
    constexpr double volatility = 0.25;
    const auto spot = ext::make_shared<SimpleQuote>(market.spot);
    const auto risk_free = Handle<YieldTermStructure>(
        ext::make_shared<FlatForward>(today, market.rate, day_count));
    const auto dividend = Handle<YieldTermStructure>(
        ext::make_shared<FlatForward>(today, market.dividend_yield, day_count));
    const auto vol = Handle<BlackVolTermStructure>(
        ext::make_shared<BlackConstantVol>(today, calendar, volatility, day_count));
    const auto process = ext::make_shared<BlackScholesMertonProcess>(
        Handle<Quote>(spot), dividend, risk_free, vol);

    std::vector<double> local_prices;
    const auto local_start = Clock::now();
    for (const double strike : strikes) {
        local_prices.push_back(aofve::american_binomial(
            {strike, 1.0, aofve::OptionType::put}, market, volatility, {steps, false}));
    }
    const auto local_end = Clock::now();

    std::vector<double> quantlib_prices;
    const auto quantlib_start = Clock::now();
    for (const double strike : strikes) {
        const auto payoff = ext::make_shared<PlainVanillaPayoff>(Option::Put, strike);
        VanillaOption option(payoff, ext::make_shared<AmericanExercise>(today, expiry));
        option.setPricingEngine(ext::make_shared<
            BinomialVanillaEngine<CoxRossRubinstein>>(process, steps));
        quantlib_prices.push_back(option.NPV());
    }
    const auto quantlib_end = Clock::now();

    double maximum_error = 0.0;
    double squared_error = 0.0;
    for (std::size_t index = 0; index < strikes.size(); ++index) {
        const double error = local_prices[index] - quantlib_prices[index];
        maximum_error = std::max(maximum_error, std::abs(error));
        squared_error += error * error;
    }
    const double rmse = std::sqrt(squared_error / static_cast<double>(strikes.size()));
    const auto local_us = std::chrono::duration_cast<std::chrono::microseconds>(
        local_end - local_start).count();
    const auto quantlib_us = std::chrono::duration_cast<std::chrono::microseconds>(
        quantlib_end - quantlib_start).count();

    std::cout << "contracts,steps,aofve_us,quantlib_us,rmse,max_abs_error\n"
              << strikes.size() << ',' << steps << ',' << local_us << ',' << quantlib_us << ','
              << std::scientific << std::setprecision(6) << rmse << ',' << maximum_error << '\n';
}

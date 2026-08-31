import numpy as np

import aofve


def test_python_round_trip_and_batch():
    option = aofve.OptionSpec(100.0, 1.0, aofve.OptionType.PUT)
    market = aofve.MarketData(100.0, 0.04, 0.01)
    config = aofve.TreeConfig()
    config.steps = 250
    price = aofve.american_binomial(option, market, 0.25, config)
    iv_config = aofve.ImpliedVolatilityConfig()
    iv_config.tree = config
    result = aofve.american_implied_volatility(price, option, market, iv_config)
    assert result.converged
    assert abs(result.volatility - 0.25) < 2e-6

    count = 6
    prices = aofve.price_batch(
        np.full(count, 100.0),
        np.linspace(90.0, 110.0, count),
        np.full(count, 1.0),
        np.full(count, 0.04),
        np.full(count, 0.01),
        np.full(count, 0.25),
        np.array([1, 1, 1, 0, 0, 0], dtype=np.uint8),
        config,
        2,
    )
    assert prices.shape == (count,)
    assert np.all(np.isfinite(prices))
    assert np.all(prices >= 0.0)


def test_python_svi_diagnostics():
    parameters = aofve.SVIParameters(0.025, 0.10, -0.3, 0.0, 0.25)
    report = aofve.check_svi_butterfly_arbitrage(parameters, -1.5, 1.5)
    assert report.arbitrage_free
    assert parameters.is_admissible()

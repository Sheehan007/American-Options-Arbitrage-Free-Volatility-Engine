# American Options & Arbitrage-Free Volatility Engine

A C++20 library for dividend-aware American option pricing, robust implied-volatility inversion, constrained raw-SVI calibration, and static-arbitrage diagnostics. A pybind11 extension exposes scalar and high-throughput batch workflows to Python, while an optional benchmark compares the lattice implementation with QuantLib.

## Features

- American calls and puts on assets with continuous dividend yield
- Recombining Cox-Ross-Rubinstein lattice with early exercise at every node and even/odd smoothing
- Drift-matched recombining fallback for extreme carry-to-volatility regimes
- Independent Crank-Nicolson finite-difference reference solver with projected SOR
- Safeguarded secant/bisection implied-volatility solver with automatic bracket expansion
- Raw-SVI slice calibration using deterministic multi-start constrained optimization
- Positive minimum-total-variance parameterization and density-factor penalty
- Calendar-monotone surface construction across expiries
- Strike monotonicity, call-price convexity, risk-neutral density, SVI butterfly, and calendar checks
- Native C++ batch pricing with configurable worker count and NumPy bindings
- Optional accuracy and latency comparison against QuantLib

## Model conventions

Market inputs are spot \(S\), continuously compounded risk-free rate \(r\), continuous dividend yield \(q\), volatility \(\sigma\), and year-fraction maturity \(T\). The pricing lattice uses the risk-neutral carry \(r-q\) and discounts at \(r\).

The raw-SVI slice is

\[
w(k)=a+b\left[\rho(k-m)+\sqrt{(k-m)^2+\sigma_{SVI}^2}\right],
\]

where \(k=\log(K/F_T)\) and \(w=\sigma_{BS}^2T\). Calibration enforces \(b\ge0\), \(|\rho|<1\), \(\sigma_{SVI}>0\), and

\[
a+b\sigma_{SVI}\sqrt{1-\rho^2}\ge0.
\]

The SVI density diagnostic evaluates the Durrleman/Gatheral density factor

\[
g(k)=\left(1-\frac{kw'(k)}{2w(k)}\right)^2
-\frac{w'(k)^2}{4}\left(\frac{1}{w(k)}+\frac14\right)
+\frac{w''(k)}2.
\]

A negative \(g(k)\), negative log-strike density, decreasing call prices, decreasing call-price slopes, or decreasing total variance across expiries is reported with its coordinate and margin.

## Build and test

Requirements: a C++20 compiler and CMake 3.20 or newer.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/aofve_example
```

Build the native throughput benchmark with:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DAOFVE_BUILD_BENCHMARKS=ON
cmake --build build --parallel
./build/aofve_benchmark
```

## C++ example

```cpp
#include <aofve/aofve.hpp>

using namespace aofve;

const MarketData market{100.0, 0.045, 0.018};
const OptionSpec option{105.0, 0.75, OptionType::put};
const TreeConfig tree{500, true};

const double price = american_binomial(option, market, 0.28, tree);

ImpliedVolatilityConfig iv_config;
iv_config.tree = tree;
const auto iv = american_implied_volatility(price, option, market, iv_config);
```

The finite-difference reference is available through `american_finite_difference`. It returns the price, total projected-SOR iterations, and a convergence flag.

## Python installation and usage

The Python build uses scikit-build-core and pybind11:

```bash
python -m pip install .
```

Scalar pricing and inversion mirror the C++ API:

```python
import aofve

market = aofve.MarketData(100.0, 0.045, 0.018)
option = aofve.OptionSpec(105.0, 0.75, aofve.OptionType.PUT)
tree = aofve.TreeConfig()
tree.steps = 500

price = aofve.american_binomial(option, market, 0.28, tree)

iv_config = aofve.ImpliedVolatilityConfig()
iv_config.tree = tree
result = aofve.american_implied_volatility(price, option, market, iv_config)
assert result.converged
```

`price_batch` accepts equal-length, one-dimensional NumPy arrays. Set `workers=0` to use the machine's reported hardware concurrency:

```python
import numpy as np
import aofve

n = 10_000
prices = aofve.price_batch(
    np.full(n, 100.0),             # spots
    np.linspace(70.0, 130.0, n),  # strikes
    np.full(n, 1.0),               # maturities
    np.full(n, 0.04),              # rates
    np.full(n, 0.015),             # dividend yields
    np.full(n, 0.25),              # volatilities
    np.ones(n, dtype=np.uint8),    # 1 = call, 0 = put
    workers=0,
)
```

## SVI calibration and diagnostics

```python
import numpy as np
import aofve

k = np.linspace(-0.8, 0.8, 21)
observed_w = 0.025 + 0.11 * (
    -0.35 * (k - 0.02) + np.sqrt((k - 0.02) ** 2 + 0.24 ** 2)
)

fit = aofve.calibrate_svi(k.tolist(), observed_w.tolist())
slice_report = aofve.check_svi_butterfly_arbitrage(fit.parameters)

surface = aofve.SVISurface(
    [0.5, 1.0],
    [
        aofve.SVIParameters(0.02, 0.10, -0.35, 0.0, 0.22),
        aofve.SVIParameters(0.05, 0.11, -0.30, 0.0, 0.25),
    ],
)
surface_report = aofve.diagnose_svi_surface(surface)
```

`calibrate_svi_surface` fits expiries in maturity order. When calendar enforcement is enabled, each later slice receives the smallest non-negative variance shift required to keep total variance nondecreasing on the configured log-moneyness grid. The applied shifts are returned for auditability.

## QuantLib comparison

If QuantLib is installed with a discoverable CMake package or `quantlib.pc` pkg-config file:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DAOFVE_BUILD_EXAMPLES=OFF \
  -DAOFVE_WITH_QUANTLIB=ON
cmake --build build --parallel
./build/aofve_quantlib_benchmark
```

The benchmark prices the same American put strip with a 500-step CRR engine in both libraries and prints contract count, elapsed microseconds, RMSE, and maximum absolute price difference as CSV. Timings are intentionally generated locally rather than published as machine-independent claims.

One reference run using GCC 13 and QuantLib 1.33 in an Ubuntu 24.04 ARM64 container produced:

| Contracts | Steps | AOFVE | QuantLib | Price RMSE | Max abs. error |
|---:|---:|---:|---:|---:|---:|
| 31 | 500 | 5,968 µs | 17,398 µs | 1.23e-5 | 1.67e-5 |

This is a reproducibility check, not a hardware-independent performance guarantee. Run the benchmark locally for decision-useful latency measurements.

## Numerical safeguards

- Invalid or non-finite contract inputs fail fast with `std::invalid_argument`/`ValueError`.
- Expiry and near-zero-volatility cases have explicit deterministic limits.
- Implied volatility never takes an unbracketed Newton step.
- The finite-difference solver reports non-convergence rather than silently treating the last iterate as exact.
- SVI constraints are encoded in transformed parameters, so optimizer proposals cannot violate the raw admissibility conditions.
- Arbitrage-free status is always relative to the user-selected diagnostic grid and tolerance; no finite grid proves global absence of arbitrage.

## Repository layout

```text
include/aofve/       Public C++ API
src/                 Pricing, IV, SVI, and arbitrage implementations
python/              pybind11 extension and Python package
tests/               Native and Python regression tests
benchmarks/          Native throughput and optional QuantLib comparison
examples/            Minimal C++ workflow
```

## License

MIT

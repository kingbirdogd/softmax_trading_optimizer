# Softmax Trading Optimizer

This project is explicitly scoped to mean-reverting, multi-leg arbitrage and spread products, not to directional trend-following or single-leg directional trading.

The framework is appropriate only for instruments that are structurally close to a stationary relative-value process, such as:

- cross-contract futures spreads
- calendar spreads
- basis trades
- multi-leg arbitrage portfolios
- spread-style products whose fair value is defined relative to recent history and whose price is expected to revert rather than trend persistently

It is not intended for outright directional assets or products with a strong long-term drift bias. If the market is fundamentally directional, this model should not be used as the primary trading engine.

This repository implements a rolling, walk-forward mean-reversion trading framework for cross-period / spread-style products. The design is intentionally structured as a three-layer system:

1. Layer 1: fair-price estimator
2. Layer 2: risk estimator
3. Layer 3: adaptive execution policy with X1/X2 add-on logic and rolling feedback

The project is a C++20 training prototype and not an institutional execution engine. The goal is to keep the architecture transparent, time-ordered, and configurable so that future agents can reason about the model without hidden assumptions.

## 1. Core trading idea

The model is not a directional price predictor. It is a mean-reversion framework:

- Fair price estimate: what should the contract be worth relative to recent history?
- Risk estimate: how wide should the deviation band be, and how much volatility is currently present?
- Trading policy: if price deviates enough from fair value, open or add position; if the regime is too noisy or drawdown is bad, tighten or loosen the add-on gate.

For cross-period futures or spread products, the core question is not "will price rise?" but rather:

- how far is price from fair value?
- how much risk is associated with the current deviation?
- should the strategy add to scale when the deviation further expands?

This is consistent with a mean-reversion / adaptive channel model.

## 2. Training objective and intermediate targets

There are three meaningful intermediate targets and one end goal.

### 2.1 Intermediate target 1: fair-price prediction

The fair-price model estimates the next trading-day reference price or fair spread relative to recent market history.

Feature vector:

x^F_t = [\mu^p_{t-60:t-1}, \mu^p_{t-30:t-1}, ..., p^preopen_{1}, ..., p^preopen_{30}]

with the implementation using:

- historical price means: `fair_price_historical_samples`
- pre-open price features: `fair_price_preopen_prices`

The code builds the feature vector with:

- `historical_price_means` from `DayFeatures`
- plus pre-open prices appended in order

The model is a softmax-weighted regressor:

w_t = softmax(z_t)

\hat{y}_t = \sum_{j=1}^{n} w_j x_j

where:
- `z_t` are the logits
- `x_j` are the feature values
- `\hat{y}_t` is the fair-price estimate

This is implemented in `SoftmaxRegressor` in `include/models.hpp` and `src/models.cpp`.

The training objective is scalar regression to a target such as observed next-day fair value or next-day basis reference. The optimization uses a simple Adam version on MSE:

L = (1/N) \sum_i (\hat{y}_i - y_i)^2

and updates logits using the gradient of the weighted softmax regressor.

### 2.2 Intermediate target 2: risk prediction

The risk model estimates the deviation magnitude / volatility / spread expansion level for the next trading day.

Feature vector:

x^R_t = [\sigma^p_{t-30}, \mu^{spread}_{t-30}, \sigma^{spread}_{t-30}, \sigma^{intraday}_{1:10}, \mu^{spread,intraday}_{1:10}, \sigma^{spread,intraday}_{1:10}]

This is the implementation used in the code:

- historical price std: `risk_historical_price_stds`
- historical spread mean: `risk_historical_spread_means`
- historical spread std: `risk_historical_spread_stds`
- intraday rolling std / mean / spread features

The risk estimator is also a softmax regressor with a non-negative output:

\hat{r}_t = max(0, \sum_j w_j x_j)

This is implemented in `RiskModel::predict_day31` with the clamp to `max(0, ...)` to keep risk non-negative.

### 2.3 Intermediate target 3: adaptive add-on gate X1 / X2

This is the strategy-level target that determines how far price must continue moving away from fair value before additional size is added.

In code, entry / add conditions are:

- long entry: `ask < fair - fee_ticks - risk`
- long add: `ask < last_long_tick - x1_ticks * tick_size`
- short entry: `bid > fair + fee_ticks + risk`
- short add: `bid > last_short_tick + x2_ticks * tick_size`

The important conceptual point is that `x1_ticks` and `x2_ticks` are not fixed constants. They are dynamic policy parameters updated from recent performance.

In other words:

- X1 = additional tolerance for long-side add-on
- X2 = additional tolerance for short-side add-on
- they are state variables that evolve with recent market quality

### 2.4 Final target

The final end goal is risk-adjusted execution quality, not binary directional correctness.

Typical target formulation:

maximize: total_pnl - λ * drawdown

or equivalently:

maximize: net_pnl

subject to:
- drawdown control
- win rate quality
- exposure discipline
- fees and slippage

The project currently tracks this in `ReplayResult` as:

- realized_pnl
- floating_pnl
- total_pnl
- net_pnl
- max_drawdown
- win_rate
- exposure_ratio
- trade count
- opportunity count
- long/short opportunity count
- avg_signal_strength

The strategy is therefore trained through outcomes, not just by a one-shot regression target.

## 3. Mathematical model of the softmax regressor

The base learning model is a convex combination of features weighted by softmax-normalized logits:

z = [z_1, z_2, ..., z_n]

w_j = exp(z_j) / \sum_k exp(z_k)

\hat{y} = \sum_j w_j x_j

This is the model used in `SoftmaxRegressor` and is intentionally simple and interpretable.

The training objective is:

L = (1/N) * \sum_i (\hat{y}_i - y_i)^2

The gradient of the softmax-weighted model is:

∂\hat{y}/∂z_j = w_j (x_j - \hat{y})

and the Adam update is:

m_j ← β_1 m_j + (1 - β_1) g_j
v_j ← β_2 v_j + (1 - β_2) g_j^2

m̂_j = m_j / (1 - β_1^t)
v̂_j = v_j / (1 - β_2^t)

z_j ← z_j - η * m̂_j / (sqrt(v̂_j) + ε)

This is exactly the logic implemented in `SoftmaxRegressor::fit()`.

## 4. Fair-price day model

The fair-price model tries to estimate the next reference price or fair basis.

### 4.1 Feature construction

`make_fair_price_features(...)` uses:

- historical price means: `cfg.fair_price_historical_samples`
- pre-open observations: `cfg.fair_price_preopen_prices`

The result is:

x^F = [μ_{t-60}, ..., μ_{t-1}, p^preopen_1, ..., p^preopen_30]

The code validates that the length matches the configured values exactly.

### 4.2 Recursive update

The model exposes a recursive blend for previous and current estimates:

\hat{p}_{new} = w_0 * \hat{p}_{old} + w_1 * p_{current}

where the weights are generated by a softmax over `update_weights`.

This lets a stable fair-price estimate absorb new information without switching completely to the most recent tick.

## 5. Risk day model

The risk model estimates the expected deviation scale and the local width of the mean-reverting band.

### 5.1 Feature construction

The implementation combines:

- historical price std dev
- historical spread mean
- historical spread std dev
- intraday price std features
- intraday spread mean features
- intraday spread std features

This yields:

x^R = [σ_p, μ_spread, σ_spread, σ_intra_p, μ_intra_spread, σ_intra_spread]

The output is clipped to be non-negative:

\hat{r} = max(0, \sum_j w_j x_j)

This is desirable because a risk band must never become negative.

### 5.2 Dynamic risk band

The execution logic defines:

upper = fair + fee_ticks + max(0, risk)
lower = fair - fee_ticks - max(0, risk)

Therefore the risk band grows with the estimated volatility / deviation intensity.

This provides the daily adaptive channel width used by the trading execution layer.

## 6. Execution policy and X1/X2 logic

The execution layer is a channel/mean-reversion gate.

For a long position:

entry if ask < lower
add if ask < last_long_tick - x1_ticks * tick_size

For a short position:

entry if bid > upper
add if bid > last_short_tick + x2_ticks * tick_size

This means the strategy is only scaling when continued price movement confirms the same mean-reversion direction.

The pattern is intentionally conservative: do not add on every tick; only add after the adverse move has continued beyond a marginal threshold.

## 7. Daily replay and rolling PnL accounting

The project computes three important PnL notions per day:

1. realized_pnl
   - PnL from completed trades that were closed
2. floating_pnl
   - mark-to-market PnL on still-open position at end of day
3. total_pnl = realized_pnl + floating_pnl

The strategy uses the end-of-day mid price to compute floating PnL:

floating = (mid - avg_entry) * |quantity| for long
floating = (avg_entry - mid) * |quantity| for short

This is crucial because a dry run should reflect ongoing open position value, not only realized PnL from closed trades.

## 8. Daily feedback loop: from output to next-day training

This is the central part of the project.

### 8.1 ReplayResult fields

`ReplayResult` stores a daily summary of the dry-run:

- gross_pnl
- fees
- realized_pnl
- floating_pnl
- total_pnl
- net_pnl
- max_drawdown
- trades
- opportunity_count
- long_opportunity_count
- short_opportunity_count
- avg_signal_strength
- win_rate
- exposure_ratio

### 8.2 Rolling feedback feature vector

The helper `make_recent_feedback_features(...)` aggregates the last `N` days into a compact vector.

This is not a single scalar reward. It is a multi-dimensional state vector summarizing the last `N` days:

f_t = [avg_realized, avg_floating, avg_total, avg_net, avg_drawdown, avg_trades, avg_fees, avg_opportunity, avg_signal_strength, avg_win_rate, avg_exposure]

The window is defaulted to 10 days.

### 8.3 Policy update from recent history

The function `select_strategy_from_recent_results(...)` changes the strategy parameters based on the latest window:

- if avg_total > 0 and drawdown is low -> tighten/add-on gate to allow more size
- if win_rate < 0.4 or drawdown > threshold -> widen gate to lower risk
- if opportunities are too few -> loosen threshold to keep strategy active
- if opportunities are too many but win rate is poor -> tighten threshold
- if floating PnL is negative -> reduce add-on aggressiveness

This is exactly the daily adaptive strategy logic you described.

## 9. Walk-forward / no-leakage training schedule

The model is designed to prevent leakage.

The correct workflow is:

1. Use historical data up to day t
2. Train on the current-day + prior warm-up history at the close of day t
3. Freeze the model parameters
4. Use the frozen parameters for next-day dry run on day t+1
5. Record daily outcomes = realized + floating + total PnL
6. Feed the previous day or previous `N` days into the next training cycle
7. Repeat with a rolling window

This is the essence of walk-forward training.

The default values in config are:

- `warmup_days = 30`
- `first_dry_run_day = 31`
- `rolling_feedback_window = 10`
- `carry_position_forward = true`

So the intended logic is:

- warm-up with the first 30 days
- first dry run begins on day 31
- day 32 immediately uses day 31 result as feedback
- from day 32 onward, the rolling history window is used, capped at 10 days

This means there is no trigger-day gate; there is only a rolling window. The latest result is always available for feedback when the window is non-empty.

## 10. Adaptive windowing (slow layer)

The project supports a second, slower self-adaptation layer.

The configuration includes min/max bounds for the feature window sizes:

- `fair_price_historical_samples` with min/max
- `fair_price_preopen_prices` with min/max
- `risk_historical_price_stds` with min/max
- `risk_historical_spread_means` with min/max
- `risk_historical_spread_stds` with min/max
- intraday sample windows with min/max ranges

This means the system is not limited to a single fixed window. It can adapt over time:

- if the market is stable and high-quality -> broaden feature windows to smooth over noise
- if the market is unstable and noisy -> narrow windows to respond faster
- if the regime is shrinking / mean-reverting strongly -> tune window sizes to align with current dynamics

A slow layer can therefore update these values with a bounded step size without human intervention.

The important idea is that the slow layer should use a regime score, not a random rule. A simple regime score is:

score = α * total_pnl_norm + β * win_rate - γ * drawdown - δ * poor_signal_ratio

and then apply a bounded update:

window_new = clamp(window_old + step * score, min, max)

The defaults are intentionally conservative so that the model does not oscillate too aggressively.

## 11. Configuration file and default assumptions

The default configuration is in `config/default.json`.

The current defaults are:

- fair_price_historical_samples = 60
- fair_price_preopen_prices = 30
- risk_historical_price_stds = 30
- risk_historical_spread_means = 30
- risk_historical_spread_stds = 30
- risk_intraday_price_std_samples = 10
- risk_intraday_spread_mean_samples = 10
- risk_intraday_spread_std_samples = 10
- tick_size = 0.01
- fixed_fee_ticks = 1.0
- x1_ticks = 3.0
- x2_ticks = 3.0
- fee_per_side = 0.0
- quantity = 1.0
- liquidate_at_end = true
- warmup_days = 30
- first_dry_run_day = 31
- rolling_feedback_window = 10
- carry_position_forward = true

Important semantic note:

- these are default starting values, not necessarily final fixed values
- X1/X2 and window parameters are intended to evolve with recent performance and regime shifts

## 12. Why rolling training is mathematically sensible

There are three reasons this rolling design is appropriate for mean-reverting products.

### 12.1 Market regime is non-stationary

A trend or basis regime can shift. A static 60-day or 30-day window can become stale. Rolling training preserves responsiveness by letting the model adapt to the recent regime.

### 12.2 Mean reversion is strongest around recent deviations

The most relevant signal is usually not the very oldest history; it is the recent history where current spread conditions are most informative. A rolling window gives the model a more relevant bias.

### 12.3 Performance feedback is a natural reward signal

When the model executes on real or replayed data, the outcomes are not random noise. They are direct evidence about whether the current fair-value estimate, risk band, and add-on gate are aligned with the current market. Feeding this back into the next round is a valid form of online adaptation.

## 13. Operational sequence for future AI agents

When working on this project, the following rules should be obeyed:

- do not leak future data into training
- maintain time ordering: train on past, test on future
- keep all model state in config-driven defaults when not specified
- treat X1/X2 as dynamic gates, not fixed constants
- treat feature windows as adaptive regime parameters
- keep feedback bounded and non-leaky
- validate the build after any change

## 14. Current project status

The codebase is intentionally structured to support the workflow described above:

- `SoftmaxRegressor` implements the base weighted softmax model
- `FairPriceModel` estimates fair value
- `RiskModel` estimates risk band magnitude
- `TradingStrategy` contains the entry/add/exit logic
- `ReplayResult` records realized, floating, and total PnL plus reward features
- `make_recent_feedback_features(...)` and `select_strategy_from_recent_results(...)` implement the daily performance feedback loop
- `run_walkforward_dry_run(...)` is the cross-day dry run engine for carry-forward positions and rolling training

This is a strong conceptual match for the mean-reversion / cross-period trading design you want, even though production-grade data ingestion and real market microstructure logic still need to be connected to the model.

## 15. Summary

The architecture is built around a simple but powerful pattern:

- fair-price predictor
- risk predictor
- adaptive channel policy
- daily rolling feedback
- slow-window regime adaptation
- no future leakage

This is mathematically coherent for cross-period spread-type products and follows the training schedule you described.

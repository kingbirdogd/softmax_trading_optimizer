# AGENTS.md

This repository is the implementation of a rolling, no-leakage, mean-reversion trading framework for spread-style products. It is designed to be read and extended by both human developers and AI agents.

## 0. Scope and applicability

This framework is designed only for multi-leg, mean-reverting, spread-style or basis-like instruments whose fair value is relative and whose dynamics are expected to revert around a recent reference level.

It is not a general directional trading model and is not appropriate for assets that are fundamentally trend-driven or have a strong directional drift. This includes:

- cross-period futures spreads
- calendar spreads
- basis trades
- multi-leg relative-value arbitrage portfolios
- other products whose pricing relationship is expected to revert rather than trend persistently

Do not treat this as a generic price predictor for single-leg directional assets. The model is intentionally scoped to instruments where the central assumption is mean reversion, not directional momentum.

## 1. Purpose of this project

The current codebase implements a three-layer architecture:

1. Fair-value model
2. Risk model
3. Adaptive policy layer with X1/X2 and rolling feedback

The overall objective is not to forecast a directional trend. The objective is to estimate when a product is materially away from its recent fair value, how much risk that deviation implies, and whether the strategy should scale further into the position.

## 2. Core design assumptions

This project assumes a mean-reverting asset class:

- cross-period futures spreads
- basis-like or cross-contract deviations
- any instrument with a tendency to revert to a recent fair value

The model is therefore built to estimate:

- fair value
- risk / spread bandwidth
- trade-entry and add-on thresholds
- daily outcomes and feedback state

## 3. The three training targets

### Target 1: fair value

The fair-value model predicts the next representative reference price or fair spread given recent history and the current pre-open context.

It is a softmax-weighted regressor using recent historical features. Its goal is to estimate a price close to the latent fair value for the upcoming session.

### Target 2: risk band

The risk model predicts how volatile or spread-heavy the next regime is likely to be. It is trained to estimate the scale of deviation that should be tolerated before the strategy triggers a position or scales exposure.

### Target 3: adaptive X1 / X2 gate

The policy gate is the actual execution threshold used to decide when to add to a position. `x1_ticks` and `x2_ticks` are not fixed constants; they are state variables updated from recent performance.

This is a structural requirement of the project and should not be treated as hand-coded static values unless explicitly overridden by the config.

## 4. Walk-forward logic

The project is designed around a strict no-leakage flow:

- use data available up to the close of day t
- train model using the current day and recent history
- deploy the frozen model on day t+1
- compute realized and floating PnL after execution
- feed performance back into the next training cycle

The intended default schedule is:

- `warmup_days = 30`
- `first_dry_run_day = 31`
- `rolling_feedback_window = 10`
- `carry_position_forward = true`

This means the first dry run should begin immediately after warm-up, and feedback should begin from the next day onward with a rolling window capped at `N`.

## 5. Not allowed

Agents working in this repo should avoid the following mistakes:

- using future data in the training set
- treating X1/X2 as fixed constants
- ignoring floating PnL vs realized PnL separation
- discarding the carry-forward position when a day ends
- allowing a trigger-day that waits too long before feedback starts
- using a static window forever without bounded adaptation

## 6. Data structures to know

The following structures are central:

- `DayFeatures`
- `Trade`
- `ReplayResult`
- `StrategyConfig`
- `ModelConfig`

The `ReplayResult` object must include:

- realized_pnl
- floating_pnl
- total_pnl
- net_pnl
- max_drawdown
- opportunity_count
- long_opportunity_count
- short_opportunity_count
- avg_signal_strength
- win_rate
- exposure_ratio

These factors are the primary basis for the rolling performance feedback.

## 7. Exact interpretation of the rolling feedback loop

Every trading day, after the session ends, the system should aggregate recent outcomes and update strategy state by comparing them with recent performance.

The feedback loop should use a rolling window of the previous `N` days and summarize:

- average realized PnL
- average floating PnL
- average total PnL
- average net PnL
- drawdown exposure
- opportunity counts
- signal strength
- win rate
- exposure ratio

This is then used to make a bounded update to the policy seed values and to optionally adjust the slow feature windows.

## 8. X1/X2 update semantics

`x1_ticks` and `x2_ticks` are adaptive seed parameters.

If recent performance is strong and drawdown is manageable, the model may increase its willingness to add to the trade. If performance is weak or the strategy is losing, the model should shrink or tighten the add-on tolerance.

This is not a heuristic free-for-all. The update should remain bounded and should use recent `ReplayResult` state to keep it stable.

## 9. Slow update layer

The project includes a second, slower adaptation mechanism for feature windows.

This is meant to alter historical window lengths within configured bounds if the current regime becomes more or less volatile.

The important semantics are:

- update only after a substantial set of outcomes
- keep each window in a conservative min/max range
- adapt gradually rather than abruptly
- avoid destabilizing from one banner day of noise

## 10. Default config philosophy

The repository must prefer config-driven defaults over hard-coded assumptions.

If no explicit parameters are passed, the project must load `config/default.json` and use its values as the seed state.

The config is not supposed to be the final truth for all time. It is the initial regime state, from which the strategy and windows adapt.

## 11. Build expectations

This project is built with CMake and uses GoogleTest.

When modifying code:

- ensure the project still compiles
- run the unit tests
- do not leave hard-coded assumptions behind
- keep the math and configuration aligned

## 12. Implementation map

Important files:

- `README.md`: architecture and conceptual documentation
- `AGENTS.md`: agent operating instructions
- `config/default.json`: initial state and defaults
- `include/models.hpp`: model definitions
- `src/models.cpp`: model fitting and prediction logic
- `include/strategy.hpp`: strategy rules and config
- `src/strategy.cpp`: execution logic and PnL accounting
- `include/replay.hpp`: replay and feedback logic
- `src/replay.cpp`: rolling-day data processing and adaptation

## 13. Recommended reasoning style

When extending the project, think in the following sequence:

1. What data is available at day t?
2. What data would be available at day t+1?
3. Does the calculation avoid leakage?
4. Is the event outcome realized or floating?
5. Does the policy update depend on previous performance only?
6. Are X1/X2 and window sizes still bounded and adaptive?
7. Does the code maintain a stable, explainable path from input to decision to feedback?

## 14. Summary

This project should be understood as a transparent online learning architecture for mean-reverting spread products. It is deliberately explicit about:

- time ordering
- rolling adaptation
- daily feedback
- bounded policy updates
- risk-aware execution
- no leakage

The model is intentionally simpler than a deep-learning stack, but the logic is coherent and designed to be explainable rather than opaque.

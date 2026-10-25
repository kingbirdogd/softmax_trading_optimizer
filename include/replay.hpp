#pragma once
#include "models.hpp"
#include "strategy.hpp"
#include <vector>

namespace sto {
ReplayResult replay_day(const std::vector<Tick>& ticks,
                        const std::vector<double>& fair_price,
                        const std::vector<double>& risk,
                        StrategyConfig cfg);

// Aggregate the last N days of trading results into a feature vector that can be
// concatenated with market features during rolling retraining.
std::vector<double> make_recent_feedback_features(const std::vector<ReplayResult>& daily_results,
                                                 std::size_t lookback_days = 10);

// Use the last N days of realized outcomes to adapt the policy for the next round.
// This is intentionally conservative: it only tweaks x1/x2, not the full model.
StrategyConfig select_strategy_from_recent_results(const StrategyConfig& base_cfg,
                                                  const std::vector<ReplayResult>& daily_results,
                                                  std::size_t lookback_days = 10);

struct WalkForwardDryRunResult {
    std::vector<ReplayResult> daily_results;
    std::vector<Position> carried_positions;
    std::vector<StrategyConfig> configs_per_day;
    // Calendar-day indices corresponding to the aligned vectors above.
    std::vector<std::size_t> replayed_day_indices;
};

WalkForwardDryRunResult run_walkforward_dry_run(const std::vector<std::vector<Tick>>& daily_ticks,
                                               const std::vector<std::vector<double>>& fair_by_day,
                                               const std::vector<std::vector<double>>& risk_by_day,
                                               StrategyConfig initial_cfg,
                                               std::size_t warmup_days = 30,
                                               std::size_t feedback_window = 10,
                                               bool carry_position_forward = true);

// End-to-end no-leakage baseline.  At each session, it freezes models fitted
// only on prior labelled days, consumes that session's pre-open ticks to build
// features, then trades only the remaining ticks.  It is deliberately a
// baseline feature builder; production callers may use the lower-level replay
// function with their own frozen forecasts.
struct ModelWalkForwardResult {
    WalkForwardDryRunResult replay;
    std::vector<double> fair_forecasts;
    std::vector<double> risk_forecasts;
    std::vector<std::size_t> training_rows_per_day;
};

ModelWalkForwardResult run_model_walkforward_dry_run(
    const std::vector<std::vector<Tick>>& daily_ticks,
    const ModelConfig& model_cfg);
}

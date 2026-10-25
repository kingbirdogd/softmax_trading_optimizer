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
};

WalkForwardDryRunResult run_walkforward_dry_run(const std::vector<std::vector<Tick>>& daily_ticks,
                                               const std::vector<std::vector<double>>& fair_by_day,
                                               const std::vector<std::vector<double>>& risk_by_day,
                                               StrategyConfig initial_cfg,
                                               std::size_t warmup_days = 30,
                                               std::size_t feedback_window = 10,
                                               bool carry_position_forward = true);
}

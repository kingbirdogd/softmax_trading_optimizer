#include "replay.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <string>

namespace sto {
namespace {

void validate_tick(const Tick& tick) {
    if (!std::isfinite(tick.bid) || !std::isfinite(tick.ask) ||
        tick.bid < 0.0 || tick.ask < tick.bid) {
        throw std::invalid_argument("tick contains an invalid bid/ask quote");
    }
}

void validate_strategy_config(const StrategyConfig& cfg) {
    if (!std::isfinite(cfg.tick_size) || cfg.tick_size <= 0.0 ||
        !std::isfinite(cfg.fixed_fee_ticks) || cfg.fixed_fee_ticks < 0.0 ||
        !std::isfinite(cfg.x1_ticks) || cfg.x1_ticks < 0.0 ||
        !std::isfinite(cfg.x2_ticks) || cfg.x2_ticks < 0.0 ||
        !std::isfinite(cfg.fee_per_side) || cfg.fee_per_side < 0.0 ||
        !std::isfinite(cfg.quantity) || cfg.quantity <= 0.0) {
        throw std::invalid_argument("invalid strategy configuration");
    }
}

double price_value(const Tick& tick) {
    validate_tick(tick);
    if (std::isfinite(tick.price) && tick.price > 0.0) return tick.price;
    return tick.mid_price();
}

double mean(const std::vector<double>& values) {
    if (values.empty()) throw std::invalid_argument("cannot calculate mean of an empty series");
    return std::accumulate(values.begin(), values.end(), 0.0) /
           static_cast<double>(values.size());
}

double standard_deviation(const std::vector<double>& values) {
    if (values.size() < 2) return 0.0;
    const double avg = mean(values);
    double sum = 0.0;
    for (const double value : values) {
        const double delta = value - avg;
        sum += delta * delta;
    }
    return std::sqrt(sum / static_cast<double>(values.size() - 1));
}

std::vector<double> tail(const std::vector<double>& values, std::size_t count) {
    if (count == 0 || values.size() < count) {
        throw std::invalid_argument("insufficient history for configured feature window");
    }
    return {values.end() - static_cast<std::ptrdiff_t>(count), values.end()};
}

std::vector<double> rolling_statistics(const std::vector<double>& values, std::size_t count,
                                       bool use_standard_deviation) {
    if (count == 0 || values.empty()) {
        throw std::invalid_argument("cannot derive rolling statistics from an empty series");
    }
    std::vector<double> result;
    result.reserve(count);
    const std::size_t window = std::min<std::size_t>(10, values.size());
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t end = values.size() - count + i + 1;
        const std::size_t clamped_end = std::min(end, values.size());
        const std::size_t start = clamped_end > window ? clamped_end - window : 0;
        std::vector<double> slice(values.begin() + static_cast<std::ptrdiff_t>(start),
                                  values.begin() + static_cast<std::ptrdiff_t>(clamped_end));
        result.push_back(use_standard_deviation ? standard_deviation(slice) : mean(slice));
    }
    return result;
}

std::size_t required_preopen_ticks(const ModelConfig& cfg) {
    return std::max({cfg.fair_price_preopen_prices,
                     cfg.risk_intraday_price_std_samples,
                     cfg.risk_intraday_spread_mean_samples,
                     cfg.risk_intraday_spread_std_samples});
}

std::size_t required_history_ticks(const ModelConfig& cfg) {
    return std::max({cfg.fair_price_historical_samples,
                     cfg.risk_historical_price_stds,
                     cfg.risk_historical_spread_means,
                     cfg.risk_historical_spread_stds});
}

struct FeatureRow {
    std::vector<double> fair_features;
    std::vector<double> risk_features;
    double fair_target{};
    double risk_target{};
    std::size_t execution_start{};
};

// All inputs used here are strictly from earlier sessions plus the current
// session's pre-open prefix.  This function deliberately never reads a
// post-open tick; targets are attached only after a forecast is frozen.
bool make_feature_inputs(const std::vector<double>& historical_prices,
                         const std::vector<double>& historical_spreads,
                         const std::vector<Tick>& ticks,
                         const ModelConfig& cfg,
                         FeatureRow& row) {
    const std::size_t preopen_count = required_preopen_ticks(cfg);
    if (historical_prices.size() < required_history_ticks(cfg) ||
        historical_spreads.size() < required_history_ticks(cfg) ||
        ticks.size() <= preopen_count) {
        return false;
    }

    std::vector<double> preopen_prices;
    std::vector<double> preopen_spreads;
    preopen_prices.reserve(preopen_count);
    preopen_spreads.reserve(preopen_count);
    for (std::size_t i = 0; i < preopen_count; ++i) {
        preopen_prices.push_back(price_value(ticks[i]));
        preopen_spreads.push_back(ticks[i].ask - ticks[i].bid);
    }

    DayFeatures day;
    day.historical_price_means = tail(historical_prices, cfg.fair_price_historical_samples);
    day.historical_price_stds = rolling_statistics(historical_prices, cfg.risk_historical_price_stds, true);
    day.historical_spread_means = rolling_statistics(historical_spreads, cfg.risk_historical_spread_means, false);
    day.historical_spread_stds = rolling_statistics(historical_spreads, cfg.risk_historical_spread_stds, true);

    const std::vector<double> fair_preopen(preopen_prices.begin(),
                                           preopen_prices.begin() + static_cast<std::ptrdiff_t>(cfg.fair_price_preopen_prices));
    row.fair_features = make_fair_price_features(day, fair_preopen, cfg);
    row.risk_features = make_risk_features(
        day,
        rolling_statistics(preopen_prices, cfg.risk_intraday_price_std_samples, true),
        rolling_statistics(preopen_spreads, cfg.risk_intraday_spread_mean_samples, false),
        rolling_statistics(preopen_spreads, cfg.risk_intraday_spread_std_samples, true),
        cfg);

    row.execution_start = preopen_count;
    return true;
}

void attach_post_open_targets(FeatureRow& row, const std::vector<Tick>& ticks) {
    if (row.execution_start >= ticks.size()) {
        throw std::invalid_argument("cannot build a target without post-open ticks");
    }
    std::vector<double> post_open_prices;
    post_open_prices.reserve(ticks.size() - row.execution_start);
    for (std::size_t i = row.execution_start; i < ticks.size(); ++i) {
        post_open_prices.push_back(price_value(ticks[i]));
    }
    row.fair_target = mean(post_open_prices);
    row.risk_target = standard_deviation(post_open_prices);
}

void append_day_history(const std::vector<Tick>& ticks, std::vector<double>& prices,
                        std::vector<double>& spreads) {
    for (const Tick& tick : ticks) {
        prices.push_back(price_value(tick));
        spreads.push_back(tick.ask - tick.bid);
    }
}

void keep_latest_rows(std::vector<std::vector<double>>& features, std::vector<double>& targets,
                      std::size_t limit) {
    while (features.size() > limit) {
        features.erase(features.begin());
        targets.erase(targets.begin());
    }
}

WalkForwardDryRunResult run_walkforward_impl(
    const std::vector<std::vector<Tick>>& daily_ticks,
    const std::vector<std::vector<double>>& fair_by_day,
    const std::vector<std::vector<double>>& risk_by_day,
    StrategyConfig initial_cfg,
    std::size_t warmup_days,
    std::size_t feedback_window,
    bool carry_position_forward,
    const std::vector<std::size_t>& execution_starts) {
    validate_strategy_config(initial_cfg);
    if (feedback_window == 0) throw std::invalid_argument("feedback window must be positive");
    if (daily_ticks.size() != fair_by_day.size() || daily_ticks.size() != risk_by_day.size() ||
        daily_ticks.size() != execution_starts.size()) {
        throw std::invalid_argument("walk-forward inputs must contain the same number of days");
    }

    WalkForwardDryRunResult out;
    if (daily_ticks.empty() || warmup_days >= daily_ticks.size()) return out;

    StrategyConfig cfg = initial_cfg;
    std::vector<ReplayResult> history;

    for (std::size_t day = warmup_days; day < daily_ticks.size(); ++day) {
        const auto& ticks = daily_ticks[day];
        const auto& fair = fair_by_day[day];
        const auto& risk = risk_by_day[day];
        const std::size_t start = execution_starts[day];
        if (ticks.empty() || fair.size() != ticks.size() || risk.size() != ticks.size() || start >= ticks.size()) {
            throw std::invalid_argument("invalid walk-forward day at index " + std::to_string(day));
        }
        for (const Tick& tick : ticks) validate_tick(tick);
        for (std::size_t i = start; i < ticks.size(); ++i) {
            if (!std::isfinite(fair[i]) || !std::isfinite(risk[i])) {
                throw std::invalid_argument("non-finite frozen forecast at day " + std::to_string(day));
            }
        }

        if (!history.empty()) {
            cfg = select_strategy_from_recent_results(cfg, history, feedback_window);
        }

        TradingStrategy strategy(cfg);
        if (carry_position_forward && !out.carried_positions.empty()) {
            strategy.set_position(out.carried_positions.back());
        }
        strategy.begin_session(ticks[start].mid_price());
        for (std::size_t i = start; i < ticks.size(); ++i) {
            strategy.on_tick(ticks[i], fair[i], risk[i]);
        }

        const ReplayResult result = strategy.finish(ticks.back());
        out.daily_results.push_back(result);
        out.configs_per_day.push_back(cfg);
        out.carried_positions.push_back(strategy.position());
        out.replayed_day_indices.push_back(day);
        history.push_back(result);
        if (history.size() > feedback_window) history.erase(history.begin());
    }
    return out;
}

} // namespace

ReplayResult replay_day(const std::vector<Tick>& ticks,
                        const std::vector<double>& fair,
                        const std::vector<double>& risk,
                        StrategyConfig cfg) {
    validate_strategy_config(cfg);
    if (ticks.size() != fair.size() || ticks.size() != risk.size()) {
        throw std::invalid_argument("ticks/fair/risk lengths differ");
    }
    if (ticks.empty()) return {};
    TradingStrategy strategy(cfg);
    strategy.begin_session(ticks.front().mid_price());
    for (std::size_t i = 0; i < ticks.size(); ++i) {
        validate_tick(ticks[i]);
        strategy.on_tick(ticks[i], fair[i], risk[i]);
    }
    return strategy.finish(ticks.back());
}

std::vector<double> make_recent_feedback_features(const std::vector<ReplayResult>& daily_results,
                                                   std::size_t lookback_days) {
    constexpr std::size_t kFeatureCount = 11;
    if (daily_results.empty() || lookback_days == 0) return std::vector<double>(kFeatureCount, 0.0);
    const std::size_t n = std::min(lookback_days, daily_results.size());
    const auto begin = daily_results.end() - static_cast<std::ptrdiff_t>(n);

    double realized = 0.0, floating = 0.0, total = 0.0, net = 0.0, drawdown = 0.0;
    double trades = 0.0, fees = 0.0, opportunities = 0.0, signal_strength = 0.0;
    double win_rate = 0.0, exposure = 0.0;
    for (auto it = begin; it != daily_results.end(); ++it) {
        realized += it->realized_pnl;
        floating += it->floating_pnl;
        total += it->total_pnl;
        net += it->net_pnl;
        drawdown = std::max(drawdown, it->max_drawdown);
        trades += static_cast<double>(it->trades);
        fees += it->fees;
        opportunities += it->opportunity_count;
        signal_strength += it->avg_signal_strength;
        win_rate += it->win_rate;
        exposure += it->exposure_ratio;
    }
    const double divisor = static_cast<double>(n);
    return {realized / divisor, floating / divisor, total / divisor, net / divisor, drawdown,
            trades / divisor, fees / divisor, opportunities / divisor, signal_strength / divisor,
            win_rate / divisor, exposure / divisor};
}

StrategyConfig select_strategy_from_recent_results(const StrategyConfig& base_cfg,
                                                    const std::vector<ReplayResult>& daily_results,
                                                    std::size_t lookback_days) {
    StrategyConfig cfg = base_cfg;
    if (daily_results.empty() || lookback_days == 0) return cfg;

    const auto features = make_recent_feedback_features(daily_results, lookback_days);
    const double avg_realized = features[0];
    const double avg_floating = features[1];
    const double avg_net = features[3];
    const double avg_drawdown = features[4];
    const double avg_opportunities = features[7];
    const double avg_signal_strength = features[8];
    const double avg_win_rate = features[9];

    if (avg_net > 0.0 && avg_drawdown <= 0.0) {
        cfg.x1_ticks = std::max(0.5, cfg.x1_ticks * 0.95);
        cfg.x2_ticks = std::max(0.5, cfg.x2_ticks * 0.95);
    }
    if (avg_win_rate < 0.4 || avg_drawdown > 0.0 || avg_realized < 0.0 || avg_net < 0.0) {
        cfg.x1_ticks = std::max(0.5, cfg.x1_ticks * 1.15);
        cfg.x2_ticks = std::max(0.5, cfg.x2_ticks * 1.15);
    }
    if (avg_opportunities < 1.0 && avg_signal_strength > 0.0) {
        cfg.x1_ticks = std::max(0.5, cfg.x1_ticks * 0.95);
        cfg.x2_ticks = std::max(0.5, cfg.x2_ticks * 0.95);
    }
    if (avg_opportunities > 3.0 && avg_win_rate < 0.5) {
        cfg.x1_ticks = std::max(0.5, cfg.x1_ticks * 1.10);
        cfg.x2_ticks = std::max(0.5, cfg.x2_ticks * 1.10);
    }
    if (avg_floating < 0.0) {
        cfg.x1_ticks = std::max(0.5, cfg.x1_ticks * 1.10);
        cfg.x2_ticks = std::max(0.5, cfg.x2_ticks * 1.10);
    }
    cfg.x1_ticks = std::clamp(cfg.x1_ticks, 0.5, 20.0);
    cfg.x2_ticks = std::clamp(cfg.x2_ticks, 0.5, 20.0);
    return cfg;
}

WalkForwardDryRunResult run_walkforward_dry_run(const std::vector<std::vector<Tick>>& daily_ticks,
                                                const std::vector<std::vector<double>>& fair_by_day,
                                                const std::vector<std::vector<double>>& risk_by_day,
                                                StrategyConfig initial_cfg,
                                                std::size_t warmup_days,
                                                std::size_t feedback_window,
                                                bool carry_position_forward) {
    return run_walkforward_impl(daily_ticks, fair_by_day, risk_by_day, initial_cfg, warmup_days,
                                feedback_window, carry_position_forward,
                                std::vector<std::size_t>(daily_ticks.size(), 0));
}

ModelWalkForwardResult run_model_walkforward_dry_run(
    const std::vector<std::vector<Tick>>& daily_ticks,
    const ModelConfig& model_cfg) {
    model_cfg.validate();
    ModelWalkForwardResult out;
    if (daily_ticks.empty() || model_cfg.warmup_days >= daily_ticks.size()) return out;

    std::vector<std::vector<double>> fair_by_day(daily_ticks.size());
    std::vector<std::vector<double>> risk_by_day(daily_ticks.size());
    std::vector<std::size_t> execution_starts(daily_ticks.size(), 0);
    std::vector<double> historical_prices;
    std::vector<double> historical_spreads;
    std::vector<std::vector<double>> fair_training_features;
    std::vector<double> fair_targets;
    std::vector<std::vector<double>> risk_training_features;
    std::vector<double> risk_targets;

    for (std::size_t day = 0; day < daily_ticks.size(); ++day) {
        const auto& ticks = daily_ticks[day];
        for (const Tick& tick : ticks) validate_tick(tick);
        FeatureRow row;
        const bool has_row = make_feature_inputs(historical_prices, historical_spreads, ticks, model_cfg, row);
        if (day >= model_cfg.warmup_days) {
            if (!has_row || fair_training_features.empty() || risk_training_features.empty()) {
                throw std::runtime_error("insufficient causal history to create the first model forecast");
            }
            FairPriceModel fair_model(model_cfg);
            RiskModel risk_model(model_cfg);
            fair_model.initial.fit(fair_training_features, fair_targets, model_cfg.adam);
            risk_model.initial.fit(risk_training_features, risk_targets, model_cfg.adam);
            const double fair_forecast = fair_model.predict_next_reference(row.fair_features);
            const double risk_forecast = risk_model.predict_next_risk(row.risk_features);
            fair_by_day[day].assign(ticks.size(), fair_forecast);
            risk_by_day[day].assign(ticks.size(), risk_forecast);
            execution_starts[day] = row.execution_start;
            out.fair_forecasts.push_back(fair_forecast);
            out.risk_forecasts.push_back(risk_forecast);
            out.training_rows_per_day.push_back(fair_training_features.size());
        }

        if (has_row) {
            // This happens after the current day's forecast has been frozen.
            // It is therefore available only to a later session's fit.
            attach_post_open_targets(row, ticks);
            fair_training_features.push_back(std::move(row.fair_features));
            fair_targets.push_back(row.fair_target);
            risk_training_features.push_back(std::move(row.risk_features));
            risk_targets.push_back(row.risk_target);
            // A bounded rolling training set prevents old regimes from having
            // permanent influence while retaining a meaningful sample size.
            keep_latest_rows(fair_training_features, fair_targets, model_cfg.warmup_days);
            keep_latest_rows(risk_training_features, risk_targets, model_cfg.warmup_days);
        }
        append_day_history(ticks, historical_prices, historical_spreads);
    }

    out.replay = run_walkforward_impl(
        daily_ticks, fair_by_day, risk_by_day, StrategyConfig::from_model_config(model_cfg),
        model_cfg.warmup_days, model_cfg.rolling_feedback_window, model_cfg.carry_position_forward,
        execution_starts);
    return out;
}

} // namespace sto

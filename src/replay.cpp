#include "replay.hpp"
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace sto {
ReplayResult replay_day(const std::vector<Tick>& ticks,
                        const std::vector<double>& fair,
                        const std::vector<double>& risk,
                        StrategyConfig cfg) {
    if(ticks.size()!=fair.size() || ticks.size()!=risk.size())
        throw std::invalid_argument("ticks/fair/risk lengths differ");
    if(ticks.empty()) return {};
    TradingStrategy strategy(cfg);
    for(std::size_t i=0;i<ticks.size();++i)
        strategy.on_tick(ticks[i],fair[i],risk[i]);
    return strategy.finish(ticks.back());
}

std::vector<double> make_recent_feedback_features(const std::vector<ReplayResult>& daily_results,
                                                 std::size_t lookback_days) {
    if (daily_results.empty()) return {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    const std::size_t n = std::min(lookback_days, daily_results.size());
    const auto begin = daily_results.begin() + static_cast<std::ptrdiff_t>(daily_results.size() - n);

    double realized = 0.0;
    double floating = 0.0;
    double total = 0.0;
    double net = 0.0;
    double drawdown = 0.0;
    double trades = 0.0;
    double fees = 0.0;
    double opportunities = 0.0;
    double long_opportunities = 0.0;
    double short_opportunities = 0.0;
    double signal_strength = 0.0;
    double win_rate = 0.0;
    double exposure = 0.0;
    std::size_t count = 0;

    for (auto it = begin; it != daily_results.end(); ++it) {
        const auto& r = *it;
        realized += r.realized_pnl;
        floating += r.floating_pnl;
        total += r.total_pnl;
        net += r.net_pnl;
        drawdown = std::max(drawdown, r.max_drawdown);
        trades += static_cast<double>(r.trades);
        fees += r.fees;
        opportunities += r.opportunity_count;
        long_opportunities += r.long_opportunity_count;
        short_opportunities += r.short_opportunity_count;
        signal_strength += r.avg_signal_strength;
        win_rate += r.win_rate;
        exposure += r.exposure_ratio;
        ++count;
    }

    if (count == 0) return {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

    const double avg_realized = realized / static_cast<double>(count);
    const double avg_floating = floating / static_cast<double>(count);
    const double avg_total = total / static_cast<double>(count);
    const double avg_net = net / static_cast<double>(count);
    const double avg_drawdown = drawdown;
    const double avg_trades = trades / static_cast<double>(count);
    const double avg_fees = fees / static_cast<double>(count);
    const double avg_opportunities = opportunities / static_cast<double>(count);
    const double avg_signal_strength = signal_strength / static_cast<double>(count);
    const double avg_win_rate = win_rate / static_cast<double>(count);
    const double avg_exposure = exposure / static_cast<double>(count);

    return {
        avg_realized,
        avg_floating,
        avg_total,
        avg_net,
        avg_drawdown,
        avg_trades,
        avg_fees,
        avg_opportunities,
        avg_signal_strength,
        avg_win_rate,
        avg_exposure
    };
}

StrategyConfig select_strategy_from_recent_results(const StrategyConfig& base_cfg,
                                                  const std::vector<ReplayResult>& daily_results,
                                                  std::size_t lookback_days) {
    StrategyConfig cfg = base_cfg;
    if (daily_results.empty()) return cfg;

    const auto features = make_recent_feedback_features(daily_results, lookback_days);
    const double avg_realized = features[0];
    const double avg_floating = features[1];
    const double avg_total = features[2];
    const double avg_drawdown = features[4];
    const double avg_opportunities = features[7];
    const double avg_signal_strength = features[8];
    const double avg_win_rate = features[9];

    // Formal adaptive update rule for X1 / X2.
    // 1) If recent performance is good and drawdown is controlled, make the add-on gate tighter
    //    so we can add more aggressively when the mean-reversion regime is healthy.
    if (avg_total > 0.0 && avg_drawdown <= 0.0) {
        cfg.x1_ticks = std::max(0.5, cfg.x1_ticks * 0.95);
        cfg.x2_ticks = std::max(0.5, cfg.x2_ticks * 0.95);
    }

    // 2) If recent performance is poor or drawdown is bad, widen the add-on gate to avoid over-trading.
    if (avg_win_rate < 0.4 || avg_drawdown > 0.0 || avg_realized < 0.0) {
        cfg.x1_ticks = std::max(0.5, cfg.x1_ticks * 1.15);
        cfg.x2_ticks = std::max(0.5, cfg.x2_ticks * 1.15);
    }

    // 3) If there are too few opportunities, loosen the gate to keep the strategy alive.
    if (avg_opportunities < 1.0 && avg_signal_strength > 0.0) {
        cfg.x1_ticks = std::max(0.5, cfg.x1_ticks * 0.95);
        cfg.x2_ticks = std::max(0.5, cfg.x2_ticks * 0.95);
    }

    // 4) If opportunity count is high but quality is poor, tighten the gate to reduce low-quality additions.
    if (avg_opportunities > 3.0 && avg_win_rate < 0.5) {
        cfg.x1_ticks = std::max(0.5, cfg.x1_ticks * 1.10);
        cfg.x2_ticks = std::max(0.5, cfg.x2_ticks * 1.10);
    }

    // 5) Floating PnL negative means the open book is unhealthy; reduce add-on aggressiveness.
    if (avg_floating < 0.0) {
        cfg.x1_ticks = std::max(0.5, cfg.x1_ticks * 1.10);
        cfg.x2_ticks = std::max(0.5, cfg.x2_ticks * 1.10);
    }

    cfg.x1_ticks = std::min(cfg.x1_ticks, 20.0);
    cfg.x2_ticks = std::min(cfg.x2_ticks, 20.0);
    return cfg;
}

WalkForwardDryRunResult run_walkforward_dry_run(const std::vector<std::vector<Tick>>& daily_ticks,
                                               const std::vector<std::vector<double>>& fair_by_day,
                                               const std::vector<std::vector<double>>& risk_by_day,
                                               StrategyConfig initial_cfg,
                                               std::size_t warmup_days,
                                               std::size_t feedback_window,
                                               bool carry_position_forward) {
    WalkForwardDryRunResult out;
    if (daily_ticks.empty() || fair_by_day.empty() || risk_by_day.empty()) {
        return out;
    }

    const std::size_t total_days = std::min({daily_ticks.size(), fair_by_day.size(), risk_by_day.size()});
    if (total_days == 0) return out;

    StrategyConfig cfg = initial_cfg;
    std::vector<ReplayResult> history;
    std::vector<Position> carried_positions(total_days);

    for (std::size_t day = 0; day < total_days; ++day) {
        const auto& ticks = daily_ticks[day];
        const auto& fair = fair_by_day[day];
        const auto& risk = risk_by_day[day];

        if (ticks.empty() || fair.size() != ticks.size() || risk.size() != ticks.size()) {
            continue;
        }

        if (day >= warmup_days) {
            if (!history.empty()) {
                cfg = select_strategy_from_recent_results(cfg, history, feedback_window);
            }
        }

        TradingStrategy strategy(cfg);

        if (carry_position_forward && day > 0 && !out.daily_results.empty()) {
            const Position prev_pos = out.carried_positions[day - 1];
            strategy = TradingStrategy(cfg);
            strategy.on_tick({0.0, prev_pos.average_entry, prev_pos.average_entry, prev_pos.average_entry}, 0.0, 0.0);
        }

        for (std::size_t i = 0; i < ticks.size(); ++i) {
            strategy.on_tick(ticks[i], fair[i], risk[i]);
        }

        const ReplayResult result = strategy.finish(ticks.back());
        out.daily_results.push_back(result);
        out.configs_per_day.push_back(cfg);
        carried_positions[day] = strategy.position();
        out.carried_positions.push_back(strategy.position());

        if (day >= warmup_days) {
            history.push_back(result);
            if (history.size() > feedback_window) {
                history.erase(history.begin());
            }
        }
    }

    return out;
}
}

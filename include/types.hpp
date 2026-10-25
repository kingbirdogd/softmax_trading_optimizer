#pragma once
#include <cstddef>
#include <vector>

namespace sto {
struct Tick {
    double timestamp_seconds{};
    double bid{};
    double ask{};
    double price{}; // Last/trade price used for fair-price and risk features.

    double mid_price() const { return 0.5 * (bid + ask); }
};

struct DayFeatures {
    std::vector<double> historical_price_means; // 60 samples as described for Layer 1
    std::vector<double> historical_price_stds;  // 30 observations
    std::vector<double> historical_spread_means;// 30 observations
    std::vector<double> historical_spread_stds; // 30 observations
};

struct Trade {
    enum class Side { Long, Short };
    Side side{};
    double entry_price{};
    double exit_price{};
    double quantity{1.0};
    double pnl{};
};

struct ReplayResult {
    double gross_pnl{};
    double fees{};
    double realized_pnl{};
    double floating_pnl{};
    double total_pnl{};
    double net_pnl{};
    double max_drawdown{};
    std::size_t trades{};

    // Rolling feedback factors used for the next training round.
    double opportunity_count{};
    double long_opportunity_count{};
    double short_opportunity_count{};
    double avg_signal_strength{};
    double win_rate{};
    double exposure_ratio{};
};
}

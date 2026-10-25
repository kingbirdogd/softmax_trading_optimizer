#pragma once
#include "config.hpp"
#include "types.hpp"
#include <optional>

namespace sto {
struct StrategyConfig {
    double fixed_fee_ticks{1.0};
    double tick_size{0.01};
    double x1_ticks{3.0};
    double x2_ticks{3.0};
    double fee_per_side{0.0}; // currency per unit per fill
    double quantity{1.0};
    bool liquidate_at_end{true};

    static StrategyConfig default_config() {
        const ModelConfig model = ModelConfig::default_config();
        return StrategyConfig{model.fixed_fee_ticks, model.tick_size,
                              model.x1_ticks, model.x2_ticks,
                              model.fee_per_side, model.quantity,
                              model.liquidate_at_end};
    }

    static StrategyConfig from_model_config(const ModelConfig& model) {
        return StrategyConfig{model.fixed_fee_ticks, model.tick_size,
                              model.x1_ticks, model.x2_ticks,
                              model.fee_per_side, model.quantity,
                              model.liquidate_at_end};
    }
};

struct Position {
    double quantity{0.0}; // positive long, negative short
    double average_entry{0.0};
    double last_long_tick{0.0};
    double last_short_tick{0.0};
};

class TradingStrategy {
public:
    explicit TradingStrategy(StrategyConfig cfg = StrategyConfig::default_config()) : cfg_(cfg) {}
    void on_tick(const Tick& tick, double fair_price, double risk);
    ReplayResult finish(const Tick& last_tick);
    const Position& position() const { return pos_; }
    void set_position(const Position& pos) { pos_ = pos; }
    const std::vector<Trade>& trades() const { return trades_; }

    // Additional execution diagnostics for the rolling-feedback loop.
    double opportunity_count() const { return opportunities_; }
    double long_opportunity_count() const { return long_opportunities_; }
    double short_opportunity_count() const { return short_opportunities_; }
    double avg_signal_strength() const { return avg_signal_strength_; }
    double win_rate() const { return win_rate_; }
    double exposure_ratio() const { return exposure_ratio_value_; }
private:
    StrategyConfig cfg_;
    Position pos_;
    std::vector<Trade> trades_;
    double fees_{0.0};
    double equity_peak_{0.0};
    double max_drawdown_{0.0};
    double realized_{0.0};
    double opportunities_{0.0};
    double long_opportunities_{0.0};
    double short_opportunities_{0.0};
    double avg_signal_strength_{0.0};
    double win_rate_{0.0};
    double exposure_ratio_value_{0.0};
    void open_or_add_long(double price);
    void open_or_add_short(double price);
    void close_position(double price);
};
}

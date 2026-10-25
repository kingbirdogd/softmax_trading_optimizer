#pragma once

#include <cstddef>
#include <string>

namespace sto {

struct ModelConfig {
    std::size_t fair_price_historical_samples{60};
    std::size_t fair_price_historical_samples_min{30};
    std::size_t fair_price_historical_samples_max{90};
    std::size_t fair_price_preopen_prices{30};
    std::size_t fair_price_preopen_prices_min{10};
    std::size_t fair_price_preopen_prices_max{60};

    std::size_t risk_historical_price_stds{30};
    std::size_t risk_historical_price_stds_min{10};
    std::size_t risk_historical_price_stds_max{60};
    std::size_t risk_historical_spread_means{30};
    std::size_t risk_historical_spread_means_min{10};
    std::size_t risk_historical_spread_means_max{60};
    std::size_t risk_historical_spread_stds{30};
    std::size_t risk_historical_spread_stds_min{10};
    std::size_t risk_historical_spread_stds_max{60};
    std::size_t risk_intraday_price_std_samples{10};
    std::size_t risk_intraday_price_std_samples_min{5};
    std::size_t risk_intraday_price_std_samples_max{20};
    std::size_t risk_intraday_spread_mean_samples{10};
    std::size_t risk_intraday_spread_mean_samples_min{5};
    std::size_t risk_intraday_spread_mean_samples_max{20};
    std::size_t risk_intraday_spread_std_samples{10};
    std::size_t risk_intraday_spread_std_samples_min{5};
    std::size_t risk_intraday_spread_std_samples_max{20};

    double tick_size{0.01};
    double fixed_fee_ticks{1.0};
    double x1_ticks{3.0}; // initial seed; current value is updated by rolling feedback training
    double x2_ticks{3.0}; // initial seed; current value is updated by rolling feedback training
    double fee_per_side{0.0};
    double quantity{1.0};
    bool liquidate_at_end{true};

    std::size_t warmup_days{30};
    std::size_t first_dry_run_day{31};
    std::size_t rolling_feedback_window{10};
    bool carry_position_forward{true};

    static ModelConfig default_config();
    static ModelConfig load_from_file(const std::string& path);
};

} // namespace sto

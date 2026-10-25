#include "config.hpp"
#include "models.hpp"
#include "optimizer.hpp"
#include "replay.hpp"
#include "softmax.hpp"

#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

TEST(SoftmaxTest, BasicSoftmaxIsSymmetric) {
    const auto w = sto::softmax({0.0, 0.0});
    EXPECT_NEAR(w[0], 0.5, 1e-12);
    EXPECT_NEAR(w[1], 0.5, 1e-12);
}

TEST(SoftmaxRegressorTest, PredictsWeightedAverageForZeroLogits) {
    sto::SoftmaxRegressor m(2);
    EXPECT_NEAR(m.predict({10.0, 20.0}), 15.0, 1e-12);
}

TEST(OptimizerTest, SearchFindsBestCandidate) {
    const auto best = sto::search_x1_x2(
        [](double a, double b) { return -(a - 2.0) * (a - 2.0) - (b - 3.0) * (b - 3.0); },
        {1.0, 2.0, 3.0, 4.0});

    EXPECT_DOUBLE_EQ(best.x1, 2.0);
    EXPECT_DOUBLE_EQ(best.x2, 3.0);
}

TEST(ConfigTest, ParsesOverridesAndNestedAdamObject) {
    const auto path = std::filesystem::temp_directory_path() / "sto_config_override_test.json";
    {
        std::ofstream out(path);
        out << R"({
  "x1_ticks": 7.5,
  "x2_ticks": 8.5,
  "warmup_days": 5,
  "first_dry_run_day": 6,
  "AdamConfig": {
    "learning_rate": 0.01,
    "steps": 12
  }
})";
    }

    const auto cfg = sto::ModelConfig::load_from_file(path.string());
    std::filesystem::remove(path);
    EXPECT_DOUBLE_EQ(cfg.x1_ticks, 7.5);
    EXPECT_DOUBLE_EQ(cfg.x2_ticks, 8.5);
    EXPECT_EQ(cfg.warmup_days, 5U);
    EXPECT_EQ(cfg.first_dry_run_day, 6U);
    EXPECT_DOUBLE_EQ(cfg.adam.learning_rate, 0.01);
    EXPECT_EQ(cfg.adam.steps, 12U);
}

TEST(StrategyTest, TracksDrawdownAndTimeExposure) {
    sto::StrategyConfig cfg;
    cfg.liquidate_at_end = false;
    cfg.x1_ticks = 100.0;
    cfg.x2_ticks = 100.0;
    sto::TradingStrategy strategy(cfg);
    const sto::Tick first{0.0, 99.0, 99.1, 99.05};
    const sto::Tick last{1.0, 98.0, 98.1, 98.05};

    strategy.begin_session(100.0);
    strategy.on_tick(first, 100.0, 0.0);
    strategy.on_tick(last, 100.0, 0.0);
    const auto result = strategy.finish(last);

    EXPECT_LT(result.floating_pnl, 0.0);
    EXPECT_GT(result.max_drawdown, 1.0);
    EXPECT_DOUBLE_EQ(result.exposure_ratio, 1.0);
}

TEST(WalkForwardTest, SkipsWarmupAndCarriesActualPosition) {
    const std::vector<std::vector<sto::Tick>> days{
        {{0.0, 100.0, 100.1, 100.05}},
        {{0.0, 99.0, 99.1, 99.05}},
        {{0.0, 100.0, 100.1, 100.05}},
    };
    const std::vector<std::vector<double>> fair{{100.0}, {100.0}, {100.0}};
    const std::vector<std::vector<double>> risk{{0.0}, {0.0}, {0.0}};
    sto::StrategyConfig cfg;
    cfg.liquidate_at_end = false;
    cfg.x1_ticks = 100.0;
    cfg.x2_ticks = 100.0;

    const auto result = sto::run_walkforward_dry_run(days, fair, risk, cfg, 1, 10, true);
    ASSERT_EQ(result.daily_results.size(), 2U);
    ASSERT_EQ(result.replayed_day_indices.size(), 2U);
    EXPECT_EQ(result.replayed_day_indices[0], 1U);
    EXPECT_EQ(result.replayed_day_indices[1], 2U);
    ASSERT_EQ(result.carried_positions.size(), 2U);
    EXPECT_GT(result.carried_positions[0].quantity, 0.0);
    EXPECT_GT(result.carried_positions[1].quantity, 0.0);
    EXPECT_NEAR(result.daily_results[1].total_pnl, 0.0, 1e-12);
}

namespace {

sto::ModelConfig small_model_config() {
    sto::ModelConfig cfg;
    cfg.fair_price_historical_samples = 2;
    cfg.fair_price_historical_samples_min = 1;
    cfg.fair_price_historical_samples_max = 3;
    cfg.fair_price_preopen_prices = 2;
    cfg.fair_price_preopen_prices_min = 1;
    cfg.fair_price_preopen_prices_max = 3;
    cfg.risk_historical_price_stds = 1;
    cfg.risk_historical_price_stds_min = 1;
    cfg.risk_historical_price_stds_max = 3;
    cfg.risk_historical_spread_means = 1;
    cfg.risk_historical_spread_means_min = 1;
    cfg.risk_historical_spread_means_max = 3;
    cfg.risk_historical_spread_stds = 1;
    cfg.risk_historical_spread_stds_min = 1;
    cfg.risk_historical_spread_stds_max = 3;
    cfg.risk_intraday_price_std_samples = 1;
    cfg.risk_intraday_price_std_samples_min = 1;
    cfg.risk_intraday_price_std_samples_max = 3;
    cfg.risk_intraday_spread_mean_samples = 1;
    cfg.risk_intraday_spread_mean_samples_min = 1;
    cfg.risk_intraday_spread_mean_samples_max = 3;
    cfg.risk_intraday_spread_std_samples = 1;
    cfg.risk_intraday_spread_std_samples_min = 1;
    cfg.risk_intraday_spread_std_samples_max = 3;
    cfg.warmup_days = 2;
    cfg.first_dry_run_day = 3;
    cfg.rolling_feedback_window = 1;
    cfg.adam.steps = 5;
    return cfg;
}

std::vector<sto::Tick> make_day(double start) {
    std::vector<sto::Tick> ticks;
    for (std::size_t i = 0; i < 5; ++i) {
        const double price = start + static_cast<double>(i) * 0.1;
        ticks.push_back({static_cast<double>(i), price - 0.05, price + 0.05, price});
    }
    return ticks;
}

} // namespace

TEST(ModelWalkForwardTest, FreezesTrainingBeforeCurrentPostOpenTicks) {
    const auto cfg = small_model_config();
    const std::vector<std::vector<sto::Tick>> days{make_day(100.0), make_day(101.0), make_day(102.0)};
    const auto baseline = sto::run_model_walkforward_dry_run(days, cfg);
    ASSERT_EQ(baseline.replay.daily_results.size(), 1U);
    ASSERT_EQ(baseline.replay.replayed_day_indices[0], 2U);
    ASSERT_EQ(baseline.fair_forecasts.size(), 1U);
    ASSERT_EQ(baseline.risk_forecasts.size(), 1U);
    ASSERT_EQ(baseline.training_rows_per_day[0], 1U);

    auto altered = days;
    for (std::size_t i = 2; i < altered[2].size(); ++i) {
        altered[2][i].bid += 50.0;
        altered[2][i].ask += 50.0;
        altered[2][i].price += 50.0;
    }
    const auto without_current_post_open = sto::run_model_walkforward_dry_run(altered, cfg);
    EXPECT_NEAR(baseline.fair_forecasts[0], without_current_post_open.fair_forecasts[0], 1e-12);
    EXPECT_NEAR(baseline.risk_forecasts[0], without_current_post_open.risk_forecasts[0], 1e-12);
}

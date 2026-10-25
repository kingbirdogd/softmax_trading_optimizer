#pragma once
#include "config.hpp"
#include "types.hpp"
#include <vector>

namespace sto {

// A convex combination model: output = sum(softmax(logits[i]) * x[i]).
// Training minimizes mean squared error against scalar targets.
class SoftmaxRegressor {
public:
    explicit SoftmaxRegressor(std::size_t n);
    double predict(const std::vector<double>& x) const;
    void fit(const std::vector<std::vector<double>>& x,
             const std::vector<double>& y, const AdamConfig& cfg);
    const std::vector<double>& logits() const { return logits_; }
    void set_logits(const std::vector<double>& v);
private:
    std::vector<double> logits_, m_, v_;
    std::size_t adam_step_{0};
};

// Layer 1: fair-price features are configured through ModelConfig defaults.
class FairPriceModel {
public:
    FairPriceModel(std::size_t historical_samples = 60,
                   std::size_t preopen_samples = 30)
        : initial(historical_samples + preopen_samples) {}
    explicit FairPriceModel(const ModelConfig& cfg)
        : FairPriceModel(cfg.fair_price_historical_samples,
                         cfg.fair_price_preopen_prices) {}
    SoftmaxRegressor initial;
    double predict_next_reference(const std::vector<double>& x) const;
    double update(double previous_forecast, double current_price) const;
    std::vector<double> update_weights{0.0, 0.0};
};

// Layer 2: risk features are configured through ModelConfig defaults.
class RiskModel {
public:
    RiskModel(std::size_t historical_price_stds = 30,
              std::size_t historical_spread_means = 30,
              std::size_t historical_spread_stds = 30,
              std::size_t intraday_price_std = 10,
              std::size_t intraday_spread_mean = 10,
              std::size_t intraday_spread_std = 10)
        : initial(historical_price_stds + historical_spread_means +
                  historical_spread_stds + intraday_price_std +
                  intraday_spread_mean + intraday_spread_std) {}
    explicit RiskModel(const ModelConfig& cfg)
        : RiskModel(cfg.risk_historical_price_stds,
                    cfg.risk_historical_spread_means,
                    cfg.risk_historical_spread_stds,
                    cfg.risk_intraday_price_std_samples,
                    cfg.risk_intraday_spread_mean_samples,
                    cfg.risk_intraday_spread_std_samples) {}
    SoftmaxRegressor initial;
    double predict_next_risk(const std::vector<double>& x) const;
    double update(double previous_risk, double current_rolling_risk) const;
    std::vector<double> update_weights{0.0, 0.0};
};

std::vector<double> make_fair_price_features(const DayFeatures& day,
                                              const std::vector<double>& preopen,
                                              const ModelConfig& cfg);
std::vector<double> make_fair_price_features(const DayFeatures& day,
                                              const std::vector<double>& preopen);
std::vector<double> make_risk_features(const DayFeatures& day,
                                       const std::vector<double>& intraday_price_std,
                                       const std::vector<double>& intraday_spread_mean,
                                       const std::vector<double>& intraday_spread_std,
                                       const ModelConfig& cfg);
std::vector<double> make_risk_features(const DayFeatures& day,
                                       const std::vector<double>& intraday_price_std,
                                       const std::vector<double>& intraday_spread_mean,
                                       const std::vector<double>& intraday_spread_std);

} // namespace sto

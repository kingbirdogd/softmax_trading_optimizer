#include "models.hpp"
#include "softmax.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sto {
SoftmaxRegressor::SoftmaxRegressor(std::size_t n)
    : logits_(n, 0.0), m_(n, 0.0), v_(n, 0.0) {}

double SoftmaxRegressor::predict(const std::vector<double>& x) const {
    return weighted_sum(softmax(logits_), x);
}
void SoftmaxRegressor::set_logits(const std::vector<double>& v) {
    if (v.size()!=logits_.size()) throw std::invalid_argument("logit size mismatch");
    logits_=v;
    std::fill(m_.begin(), m_.end(), 0.0);
    std::fill(v_.begin(), v_.end(), 0.0);
    adam_step_ = 0;
}

void SoftmaxRegressor::fit(const std::vector<std::vector<double>>& x,
                           const std::vector<double>& y, const AdamConfig& cfg) {
    if (x.empty() || x.size()!=y.size()) throw std::invalid_argument("bad training data");
    for (const auto& row:x) if(row.size()!=logits_.size()) throw std::invalid_argument("bad feature width");
    const std::size_t n=logits_.size();
    if (n == 0) throw std::invalid_argument("model has no features");
    if (cfg.learning_rate <= 0.0 || cfg.beta1 < 0.0 || cfg.beta1 >= 1.0 ||
        cfg.beta2 < 0.0 || cfg.beta2 >= 1.0 || cfg.epsilon <= 0.0 ||
        cfg.l2 < 0.0 || cfg.steps == 0) {
        throw std::invalid_argument("invalid Adam configuration");
    }
    const double b1=cfg.beta1, b2=cfg.beta2;
    for(std::size_t local_step=0; local_step<cfg.steps; ++local_step) {
        ++adam_step_;
        auto w=softmax(logits_);
        std::vector<double> grad(n,0.0);
        for(std::size_t r=0;r<x.size();++r) {
            const double pred=weighted_sum(w,x[r]);
            const double err=pred-y[r];
            // d(pred)/d(logit_j) = w_j * (x_j - pred)
            for(std::size_t j=0;j<n;++j)
                grad[j] += (2.0*err/static_cast<double>(x.size())) * w[j]*(x[r][j]-pred);
        }
        for(std::size_t j=0;j<n;++j) {
            grad[j] += cfg.l2*logits_[j];
            m_[j]=b1*m_[j]+(1.0-b1)*grad[j];
            v_[j]=b2*v_[j]+(1.0-b2)*grad[j]*grad[j];
            const double mh=m_[j]/(1.0-std::pow(b1,static_cast<double>(adam_step_)));
            const double vh=v_[j]/(1.0-std::pow(b2,static_cast<double>(adam_step_)));
            logits_[j] -= cfg.learning_rate*mh/(std::sqrt(vh)+cfg.epsilon);
        }
    }
}

double FairPriceModel::predict_next_reference(const std::vector<double>& x) const { return initial.predict(x); }
double FairPriceModel::update(double prev,double current) const {
    auto w=softmax(update_weights); return w[0]*prev+w[1]*current;
}
double RiskModel::predict_next_risk(const std::vector<double>& x) const {
    // Risk must be non-negative. Feature construction should use non-negative statistics.
    return std::max(0.0, initial.predict(x));
}
double RiskModel::update(double prev,double rolling) const {
    auto w=softmax(update_weights); return std::max(0.0,w[0]*prev+w[1]*rolling);
}

std::vector<double> make_fair_price_features(const DayFeatures& d,const std::vector<double>& pre) {
    return make_fair_price_features(d, pre, ModelConfig::default_config());
}

std::vector<double> make_fair_price_features(const DayFeatures& d,
                                              const std::vector<double>& pre,
                                              const ModelConfig& cfg) {
    if(d.historical_price_means.size()!=static_cast<std::size_t>(cfg.fair_price_historical_samples) ||
       pre.size()!=static_cast<std::size_t>(cfg.fair_price_preopen_prices))
        throw std::invalid_argument("fair price feature sizes do not match configured model parameters");
    std::vector<double> x=d.historical_price_means;
    x.insert(x.end(),pre.begin(),pre.end()); return x;
}

std::vector<double> make_risk_features(const DayFeatures& d,
    const std::vector<double>& ps,const std::vector<double>& sm,const std::vector<double>& ss) {
    return make_risk_features(d, ps, sm, ss, ModelConfig::default_config());
}

std::vector<double> make_risk_features(const DayFeatures& d,
    const std::vector<double>& ps,const std::vector<double>& sm,const std::vector<double>& ss,
    const ModelConfig& cfg) {
    auto require = [&](std::size_t expected, const std::vector<double>& v) {
        return v.size() == expected;
    };
    if(!require(cfg.risk_historical_price_stds, d.historical_price_stds) ||
       !require(cfg.risk_historical_spread_means, d.historical_spread_means) ||
       !require(cfg.risk_historical_spread_stds, d.historical_spread_stds) ||
       !require(cfg.risk_intraday_price_std_samples, ps) ||
       !require(cfg.risk_intraday_spread_mean_samples, sm) ||
       !require(cfg.risk_intraday_spread_std_samples, ss))
        throw std::invalid_argument("risk feature sizes do not match configured model parameters");
    std::vector<double> x;
    x.insert(x.end(),d.historical_price_stds.begin(),d.historical_price_stds.end());
    x.insert(x.end(),d.historical_spread_means.begin(),d.historical_spread_means.end());
    x.insert(x.end(),d.historical_spread_stds.begin(),d.historical_spread_stds.end());
    x.insert(x.end(),ps.begin(),ps.end());
    x.insert(x.end(),sm.begin(),sm.end());
    x.insert(x.end(),ss.begin(),ss.end());
    return x;
}
}

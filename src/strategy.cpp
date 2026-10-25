#include "strategy.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace sto {
namespace {

void require_finite(double value, const char* name) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(name) + " must be finite");
    }
}

} // namespace

void TradingStrategy::begin_session(double opening_mark) {
    require_finite(opening_mark, "opening mark");
    if (opening_mark < 0.0) {
        throw std::invalid_argument("opening mark must be non-negative");
    }
    if (session_started_) {
        throw std::logic_error("session has already started");
    }
    opening_unrealized_ = unrealized_pnl(opening_mark);
    session_started_ = true;
    update_equity(opening_mark);
}

void TradingStrategy::open_or_add_long(double p) {
    const double old_abs=std::abs(pos_.quantity);
    if(pos_.quantity<0) close_position(p);
    const double q=cfg_.quantity;
    pos_.average_entry=(pos_.average_entry*old_abs+p*q)/(old_abs+q);
    pos_.quantity+=q; pos_.last_long_tick=p; fees_+=cfg_.fee_per_side*q;
}
void TradingStrategy::open_or_add_short(double p) {
    const double old_abs=std::abs(pos_.quantity);
    if(pos_.quantity>0) close_position(p);
    const double q=cfg_.quantity;
    pos_.average_entry=(pos_.average_entry*old_abs+p*q)/(old_abs+q);
    pos_.quantity-=q; pos_.last_short_tick=p; fees_+=cfg_.fee_per_side*q;
}
void TradingStrategy::close_position(double p) {
    if(pos_.quantity==0) return;
    const double q=std::abs(pos_.quantity);
    const double pnl=(p-pos_.average_entry)*pos_.quantity;
    realized_+=pnl; fees_+=cfg_.fee_per_side*q;
    trades_.push_back({pos_.quantity>0?Trade::Side::Long:Trade::Side::Short,
                       pos_.average_entry,p,q,pnl});
    pos_={};
}
void TradingStrategy::on_tick(const Tick& t,double fair,double risk) {
    require_finite(t.bid, "bid");
    require_finite(t.ask, "ask");
    require_finite(fair, "fair price");
    require_finite(risk, "risk");
    if (t.bid < 0.0 || t.ask < 0.0 || t.ask < t.bid) {
        throw std::invalid_argument("invalid bid/ask quote");
    }
    if (!session_started_) {
        begin_session(t.mid_price());
    }
    const double fee_ticks=cfg_.fixed_fee_ticks*cfg_.tick_size;
    const double upper=fair+fee_ticks+std::max(0.0,risk);
    const double lower=fair-fee_ticks-std::max(0.0,risk);
    const bool long_entry=t.ask<lower && pos_.quantity==0;
    const bool long_add=t.ask<lower && pos_.quantity>0 &&
                        t.ask<pos_.last_long_tick-cfg_.x1_ticks*cfg_.tick_size;
    const bool short_entry=t.bid>upper && pos_.quantity==0;
    const bool short_add=t.bid>upper && pos_.quantity<0 &&
                         t.bid>pos_.last_short_tick+cfg_.x2_ticks*cfg_.tick_size;

    if (long_entry || short_entry || long_add || short_add) {
        opportunities_ += 1.0;
        if (long_entry || long_add) long_opportunities_ += 1.0;
        if (short_entry || short_add) short_opportunities_ += 1.0;
        const double execution_price = (long_entry || long_add) ? t.ask : t.bid;
        const double signal_strength = std::abs((execution_price - fair) / std::max(1e-9, risk + fee_ticks));
        avg_signal_strength_ = (avg_signal_strength_ * (opportunities_ - 1.0) + signal_strength) / opportunities_;
    }

    if(long_entry||long_add) open_or_add_long(t.ask);
    else if(short_entry||short_add) open_or_add_short(t.bid);

    ++ticks_seen_;
    if (pos_.quantity != 0.0) {
        ++exposed_ticks_;
    }
    update_equity(t.mid_price());
}
ReplayResult TradingStrategy::finish(const Tick& last) {
    require_finite(last.bid, "last bid");
    require_finite(last.ask, "last ask");
    if (last.bid < 0.0 || last.ask < 0.0 || last.ask < last.bid) {
        throw std::invalid_argument("invalid final bid/ask quote");
    }
    if (!session_started_) {
        begin_session(last.mid_price());
    }
    if(cfg_.liquidate_at_end && pos_.quantity!=0)
        close_position(pos_.quantity>0?last.bid:last.ask);

    const double mid = 0.5 * (last.bid + last.ask);
    const double gross = realized_ + unrealized_pnl(mid) - opening_unrealized_;
    const double realized = pos_.quantity == 0.0 ? gross : 0.0;
    const double floating = gross - realized;
    const double net = gross - fees_;
    const double wins = std::count_if(trades_.begin(), trades_.end(), [](const Trade& tr){ return tr.pnl > 0.0; });
    const double total_trades = static_cast<double>(trades_.size());
    win_rate_ = total_trades > 0.0 ? wins / total_trades : 0.0;
    exposure_ratio_value_ = ticks_seen_ == 0 ? 0.0
        : static_cast<double>(exposed_ticks_) / static_cast<double>(ticks_seen_);

    update_equity(mid);

    ReplayResult result{gross, fees_, realized, floating, gross, net, max_drawdown_, trades_.size()};
    result.opportunity_count = opportunities_;
    result.long_opportunity_count = long_opportunities_;
    result.short_opportunity_count = short_opportunities_;
    result.avg_signal_strength = avg_signal_strength_;
    result.win_rate = win_rate_;
    result.exposure_ratio = exposure_ratio_value_;
    return result;
}

double TradingStrategy::unrealized_pnl(double mark) const {
    return (mark - pos_.average_entry) * pos_.quantity;
}

void TradingStrategy::update_equity(double mark) {
    const double equity = realized_ + unrealized_pnl(mark) - opening_unrealized_ - fees_;
    equity_peak_ = std::max(equity_peak_, equity);
    max_drawdown_ = std::max(max_drawdown_, equity_peak_ - equity);
}
}

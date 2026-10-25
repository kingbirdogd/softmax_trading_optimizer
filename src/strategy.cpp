#include "strategy.hpp"
#include <algorithm>
#include <cmath>

namespace sto {
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
        const double signal_strength = std::abs((t.ask - fair) / std::max(1e-9, risk + fee_ticks));
        avg_signal_strength_ = (avg_signal_strength_ * (opportunities_ - 1.0) + signal_strength) / opportunities_;
    }

    if(long_entry||long_add) open_or_add_long(t.ask);
    else if(short_entry||short_add) open_or_add_short(t.bid);
}
ReplayResult TradingStrategy::finish(const Tick& last) {
    if(cfg_.liquidate_at_end && pos_.quantity!=0)
        close_position(pos_.quantity>0?last.bid:last.ask);

    const double mid = 0.5 * (last.bid + last.ask);
    const double realized = realized_;
    const double floating = pos_.quantity != 0.0
        ? (pos_.quantity > 0.0 ? (mid - pos_.average_entry) * std::abs(pos_.quantity)
                               : (pos_.average_entry - mid) * std::abs(pos_.quantity))
        : 0.0;
    const double gross = realized + floating;
    const double net = gross - fees_;
    const double wins = std::count_if(trades_.begin(), trades_.end(), [](const Trade& tr){ return tr.pnl > 0.0; });
    const double total_trades = static_cast<double>(trades_.size());
    win_rate_ = total_trades > 0.0 ? wins / total_trades : 0.0;
    const double notional = std::max(1.0, std::abs(pos_.quantity));
    exposure_ratio_value_ = notional > 0.0 ? std::max(0.0, std::abs(pos_.quantity) / notional) : 0.0;

    ReplayResult result{gross, fees_, realized, floating, gross, net, max_drawdown_, trades_.size()};
    result.opportunity_count = opportunities_;
    result.long_opportunity_count = long_opportunities_;
    result.short_opportunity_count = short_opportunities_;
    result.avg_signal_strength = avg_signal_strength_;
    result.win_rate = win_rate_;
    result.exposure_ratio = exposure_ratio_value_;
    return result;
}
}

#pragma once
#include "Strategy.hpp"
#include <cmath>
#include <string>

// ── Momentum / EMA Crossover Strategy ─────────────────────────────────────────
// Signal: fast EMA crosses above/below slow EMA.
//
//   Buy signal  when fast_ema > slow_ema + threshold
//   Sell signal when fast_ema < slow_ema - threshold
//
// Positions are sized to ±target_position.  The strategy sends one market
// order when the desired position changes.
struct MomentumParams {
    int    fast_period      = 5;      // ticks for fast EMA
    int    slow_period      = 20;     // ticks for slow EMA
    int    target_position  = 200;    // shares when signal is on
    double threshold_bps    = 2.0;    // signal must exceed this to trigger
    int    min_ticks        = 30;     // warm-up period before trading
};

class Momentum : public Strategy {
public:
    Momentum(const std::string& id, const std::string& symbol,
             const MomentumParams& p = {})
        : id_(id), sym_(symbol), p_(p) {}

    const std::string& id() const override { return id_; }

    std::vector<Action> onQuote(const Quote& q, const Portfolio& pf) override {
        ++ticks_;

        // Update EMAs
        fast_ema_ = ema(q.mid, fast_ema_, p_.fast_period);
        slow_ema_ = ema(q.mid, slow_ema_, p_.slow_period);

        if (ticks_ < p_.min_ticks) return {};

        // Compute signal
        double spread   = q.mid * p_.threshold_bps / 10000.0;
        int    desired  = 0;
        if      (fast_ema_ > slow_ema_ + spread)  desired = +p_.target_position;
        else if (fast_ema_ < slow_ema_ - spread)  desired = -p_.target_position;

        int current = pf.position(sym_);
        int delta   = desired - current;
        if (delta == 0) return {};

        // Submit market order to reach desired position
        std::string oid = id_ + "_M" + std::to_string(++seq_);
        Side side = (delta > 0) ? Side::BUY : Side::SELL;
        return { submitMarket(id_, oid, sym_, side, std::abs(delta), q.ts) };
    }

    void onFill(const Fill& /*f*/) override {}

private:
    std::string     id_;
    std::string     sym_;
    MomentumParams  p_;
    double          fast_ema_ = 0.0;
    double          slow_ema_ = 0.0;
    int             ticks_    = 0;
    int             seq_      = 0;

    // EMA with span N (standard α = 2/(N+1) formula).
    // First call seeds the EMA at the current price.
    double ema(double price, double prev, int period) const {
        if (prev == 0.0) return price;
        double alpha = 2.0 / (period + 1.0);
        return alpha * price + (1.0 - alpha) * prev;
    }
};

#pragma once
#include "Strategy.hpp"
#include <deque>
#include <cmath>
#include <numeric>
#include <string>

// ── Mean-Reversion Strategy ────────────────────────────────────────────────────
// Bet that price returns to its rolling mean.
//
//   z-score = (price - mean) / std_dev
//
//   Entry:  z > +entry_z  → sell (price is high, expect fall)
//           z < -entry_z  → buy  (price is low,  expect rise)
//   Exit:   z crosses zero (or opposite entry threshold)
//
// Trades via limit orders placed at entry price to improve fill quality.
struct MeanReversionParams {
    int    lookback       = 40;     // ticks for rolling mean/std
    double entry_z        = 1.5;    // z-score threshold to enter
    double exit_z         = 0.3;    // z-score threshold to exit
    int    order_size     = 150;    // shares per trade
    int    min_ticks      = 50;     // warm-up before trading
};

class MeanReversion : public Strategy {
public:
    MeanReversion(const std::string& id, const std::string& symbol,
                  const MeanReversionParams& p = {})
        : id_(id), sym_(symbol), p_(p) {}

    const std::string& id() const override { return id_; }

    std::vector<Action> onQuote(const Quote& q, const Portfolio& pf) override {
        ++ticks_;
        window_.push_back(q.mid);
        if (static_cast<int>(window_.size()) > p_.lookback)
            window_.pop_front();

        if (ticks_ < p_.min_ticks || static_cast<int>(window_.size()) < p_.lookback)
            return {};

        double mu    = mean();
        double sigma = stddev(mu);
        if (sigma < 1e-9) return {};

        double z       = (q.mid - mu) / sigma;
        int    current = pf.position(sym_);

        std::vector<Action> actions;

        // ── Exit logic ────────────────────────────────────────────────────
        if (current != 0 && std::abs(z) < p_.exit_z) {
            // Close position with a market order
            std::string oid = id_ + "_X" + std::to_string(++seq_);
            Side side = (current > 0) ? Side::SELL : Side::BUY;
            actions.push_back(submitMarket(id_, oid, sym_, side, std::abs(current), q.ts));
            return actions;
        }

        // ── Entry logic ───────────────────────────────────────────────────
        // Don't add to existing position; only enter when flat
        if (current != 0) return {};

        if (z > p_.entry_z) {
            // Price is high → sell limit at ask (willing to sell at ask)
            std::string oid = id_ + "_S" + std::to_string(++seq_);
            actions.push_back(submitLimit(id_, oid, sym_, Side::SELL,
                                          roundPrice(q.ask), p_.order_size, q.ts));
        } else if (z < -p_.entry_z) {
            // Price is low → buy limit at bid (willing to buy at bid)
            std::string oid = id_ + "_B" + std::to_string(++seq_);
            actions.push_back(submitLimit(id_, oid, sym_, Side::BUY,
                                          roundPrice(q.bid), p_.order_size, q.ts));
        }

        return actions;
    }

    void onFill(const Fill& /*f*/) override {}

private:
    std::string           id_;
    std::string           sym_;
    MeanReversionParams   p_;
    std::deque<double>    window_;
    int                   ticks_ = 0;
    int                   seq_   = 0;

    double mean() const {
        double s = 0.0;
        for (double v : window_) s += v;
        return s / window_.size();
    }

    double stddev(double mu) const {
        double v = 0.0;
        for (double x : window_) v += (x - mu) * (x - mu);
        return std::sqrt(v / window_.size());
    }

    static double roundPrice(double p) {
        return std::round(p * 100.0) / 100.0;
    }
};

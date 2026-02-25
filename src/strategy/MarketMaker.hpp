#pragma once
#include "Strategy.hpp"
#include <cmath>
#include <string>
#include <deque>

// ── Market-Making Strategy ─────────────────────────────────────────────────────
// Places resting limit bid/ask around the mid-price.
//
// Each tick:
//  1. Cancel existing quotes (if any).
//  2. Compute spread: base_spread * vol_regime_multiplier.
//  3. Compute inventory skew: shift quotes toward zero inventory.
//  4. Submit new bid and ask.
//
// Inventory skew:
//   adjusted_mid = mid - inventory * skew_factor * tick_size
//   (long inventory → lower quotes to encourage selling)
struct MarketMakerParams {
    int    order_size        = 100;    // shares per side
    double spread_bps        = 10.0;   // base full spread in bps
    int    max_inventory     = 500;    // max abs position before stopping
    double skew_factor       = 0.3;    // inventory skew per share (bps)
    double vol_spread_mult   = 1.5;    // spread multiplier in high-vol regime
    double realized_vol_window = 20.0; // ticks for realized-vol estimate
};

class MarketMaker : public Strategy {
public:
    MarketMaker(const std::string& id, const std::string& symbol,
                const MarketMakerParams& p = {})
        : id_(id), sym_(symbol), p_(p) {}

    const std::string& id() const override { return id_; }

    std::vector<Action> onQuote(const Quote& q, const Portfolio& pf) override {
        std::vector<Action> actions;

        // ── 1. Cancel stale quotes ─────────────────────────────────────────
        if (!bid_id_.empty()) {
            actions.push_back(cancelOrder(sym_, bid_id_));
            bid_id_.clear();
        }
        if (!ask_id_.empty()) {
            actions.push_back(cancelOrder(sym_, ask_id_));
            ask_id_.clear();
        }

        // ── 2. Update realised-vol estimate ───────────────────────────────
        price_history_.push_back(q.mid);
        if (static_cast<int>(price_history_.size()) > static_cast<int>(p_.realized_vol_window) + 1)
            price_history_.pop_front();

        double rv = realizedVol();

        // ── 3. Compute spread ─────────────────────────────────────────────
        double regime_mult = (q.vol_regime > 1.5) ? p_.vol_spread_mult : 1.0;
        double vol_mult    = 1.0 + rv * 50.0;           // widen if vol spikes
        double half_spread = q.mid * p_.spread_bps / 20000.0  // bps → half-spread $
                             * regime_mult * vol_mult;
        half_spread = std::max(half_spread, 0.01);       // floor at 1 cent

        // ── 4. Inventory skew ─────────────────────────────────────────────
        int inv = pf.position(sym_);
        double skew = -static_cast<double>(inv) * p_.skew_factor * q.mid / 10000.0;

        // ── 5. Check inventory limit ─────────────────────────────────────
        if (std::abs(inv) >= p_.max_inventory) {
            // At max inventory: only quote one side to reduce position
            if (inv > 0) {
                // Only submit ask (to sell)
                std::string aid = id_ + "_A" + std::to_string(++seq_);
                actions.push_back(submitLimit(id_, aid, sym_,
                                              Side::SELL,
                                              roundPrice(q.mid + half_spread + skew),
                                              p_.order_size, q.ts));
                ask_id_ = aid;
            } else {
                // Only submit bid (to buy)
                std::string bid = id_ + "_B" + std::to_string(++seq_);
                actions.push_back(submitLimit(id_, bid, sym_,
                                              Side::BUY,
                                              roundPrice(q.mid - half_spread + skew),
                                              p_.order_size, q.ts));
                bid_id_ = bid;
            }
            return actions;
        }

        // ── 6. Submit both sides ──────────────────────────────────────────
        double bid_px = roundPrice(q.mid - half_spread + skew);
        double ask_px = roundPrice(q.mid + half_spread + skew);

        if (bid_px >= ask_px) ask_px = bid_px + 0.01;   // sanity check

        std::string bid_oid = id_ + "_B" + std::to_string(++seq_);
        std::string ask_oid = id_ + "_A" + std::to_string(++seq_);

        actions.push_back(submitLimit(id_, bid_oid, sym_, Side::BUY,  bid_px, p_.order_size, q.ts));
        actions.push_back(submitLimit(id_, ask_oid, sym_, Side::SELL, ask_px, p_.order_size, q.ts));

        bid_id_ = bid_oid;
        ask_id_ = ask_oid;
        return actions;
    }

    void onFill(const Fill& f) override {
        // If our resting order got filled, clear its tracked id
        if (f.order_id == bid_id_) bid_id_.clear();
        if (f.order_id == ask_id_) ask_id_.clear();
    }

private:
    std::string        id_;
    std::string        sym_;
    MarketMakerParams  p_;
    std::string        bid_id_;
    std::string        ask_id_;
    int                seq_ = 0;
    std::deque<double> price_history_;

    // Realised volatility: std dev of log-returns over recent window
    double realizedVol() const {
        if (price_history_.size() < 2) return 0.0;
        std::vector<double> rets;
        for (size_t i = 1; i < price_history_.size(); ++i)
            rets.push_back(std::log(price_history_[i] / price_history_[i-1]));
        double mu = 0.0;
        for (double r : rets) mu += r;
        mu /= rets.size();
        double var = 0.0;
        for (double r : rets) var += (r - mu) * (r - mu);
        return std::sqrt(var / rets.size());
    }

    // Round to nearest cent
    static double roundPrice(double p) {
        return std::round(p * 100.0) / 100.0;
    }
};

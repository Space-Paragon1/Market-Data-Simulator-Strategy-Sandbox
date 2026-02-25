#pragma once
#include "../core/Types.hpp"
#include <unordered_map>
#include <string>
#include <cmath>

// ── Portfolio ──────────────────────────────────────────────────────────────────
// Tracks cash, signed positions, and mark-to-market PnL.
//
// Total PnL = (cash - initial_cash) + sum_over_symbols(position * last_price)
//
// This covers both realised and unrealised PnL in a single unified number
// that is always correct regardless of trade direction.
class Portfolio {
public:
    explicit Portfolio(double initial_cash = 1'000'000.0)
        : cash_(initial_cash), initial_cash_(initial_cash) {}

    void onFill(const Fill& f) {
        // Cash: buy costs money, sell receives money; commission always out
        int delta = (f.side == Side::BUY) ? f.qty : -f.qty;
        cash_ -= static_cast<double>(delta) * f.price;
        cash_ -= f.comm;

        // Signed position
        positions_[f.symbol] += delta;
        last_prices_[f.symbol] = f.price;
    }

    void updatePrice(const std::string& sym, double price) {
        last_prices_[sym] = price;
    }

    // ── Queries ───────────────────────────────────────────────────────────────
    double cash()         const { return cash_; }
    double initialCash()  const { return initial_cash_; }

    int position(const std::string& sym) const {
        auto it = positions_.find(sym);
        return (it != positions_.end()) ? it->second : 0;
    }

    double lastPrice(const std::string& sym) const {
        auto it = last_prices_.find(sym);
        return (it != last_prices_.end()) ? it->second : 0.0;
    }

    // Mark-to-market value of all positions
    double mtm() const {
        double v = 0.0;
        for (auto& [sym, qty] : positions_) {
            auto pit = last_prices_.find(sym);
            if (pit != last_prices_.end())
                v += static_cast<double>(qty) * pit->second;
        }
        return v;
    }

    // Total PnL (realised + unrealised)
    double totalPnl() const {
        return (cash_ + mtm()) - initial_cash_;
    }

    // Net position across all symbols (signed)
    int netPosition() const {
        int n = 0;
        for (auto& [sym, qty] : positions_) n += qty;
        return n;
    }

    // Snapshot for the metrics recorder
    PortSnapshot snapshot(Timestamp ts, double mid, double vol_regime,
                          const std::string& strat_name) const {
        PortSnapshot s;
        s.ts         = ts;
        s.cash       = cash_;
        s.total_pnl  = totalPnl();
        s.net_pos    = netPosition();
        s.mid_price  = mid;
        s.vol_regime = vol_regime;
        s.strategy   = strat_name;
        return s;
    }

private:
    double                                 cash_;
    double                                 initial_cash_;
    std::unordered_map<std::string, int>   positions_;   // signed
    std::unordered_map<std::string, double> last_prices_;
};

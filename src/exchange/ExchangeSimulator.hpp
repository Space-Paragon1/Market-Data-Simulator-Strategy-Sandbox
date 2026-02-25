#pragma once
#include "OrderBook.hpp"
#include <unordered_map>
#include <string>
#include <stdexcept>

// ── Exchange configuration ─────────────────────────────────────────────────────
struct ExchangeConfig {
    double commission_per_share = 0.001;  // dollars per share
    double slippage_bps         = 1.0;    // market-order slippage in basis points
};

// ── Exchange Simulator ─────────────────────────────────────────────────────────
// Maintains one OrderBook per symbol.  Provides virtual liquidity so that
// market orders are always fully filled (at current quote ± slippage).
class ExchangeSimulator {
public:
    explicit ExchangeSimulator(ExchangeConfig cfg = {}) : cfg_(std::move(cfg)) {}

    // Called each tick so the exchange knows the current quote (for virtual fills)
    void updateQuote(const Quote& q) {
        last_quotes_[q.symbol] = q;
        // Ensure a book exists for this symbol
        books_.emplace(q.symbol, OrderBook{});
    }

    // Generate a unique order-id string
    std::string newOrderId(const std::string& prefix = "O") {
        return prefix + std::to_string(++order_cnt_);
    }

    // Submit an order; returns all fills (taker + maker)
    std::vector<Fill> process(Order order) {
        auto& book = books_[order.symbol];
        auto  fills = book.add(order, cfg_.commission_per_share);

        // ── Virtual liquidity for market orders ──────────────────────────────
        // Any remaining unfilled qty is matched against a virtual liquidity
        // provider at the current quote ± slippage.
        // Note: order is passed by value into book.add(), so we count
        // already-filled qty from the returned fill vector instead.
        if (order.type == OrdType::MARKET) {
            int already_filled = 0;
            for (auto& f : fills)
                if (f.order_id == order.id) already_filled += f.qty;
            int remaining = order.qty - already_filled;
            if (remaining > 0) {
                const Quote& q = lastQuote(order.symbol);
                double fill_px =
                    (order.side == Side::BUY)
                    ? q.ask * (1.0 + cfg_.slippage_bps / 10000.0)
                    : q.bid * (1.0 - cfg_.slippage_bps / 10000.0);

                Fill vf;
                vf.fill_id  = "VF" + std::to_string(++vfill_cnt_);
                vf.order_id = order.id;
                vf.symbol   = order.symbol;
                vf.strat_id = order.strat_id;
                vf.side     = order.side;
                vf.price    = fill_px;
                vf.qty      = remaining;
                vf.comm     = remaining * cfg_.commission_per_share;
                vf.ts       = order.ts;
                fills.push_back(vf);
            }
        }
        return fills;
    }

    // Cancel a resting order; returns true if found and removed
    bool cancel(const std::string& symbol, const std::string& order_id) {
        auto it = books_.find(symbol);
        return (it != books_.end()) && it->second.cancel(order_id);
    }

    double bestBid(const std::string& symbol) const {
        auto it = books_.find(symbol);
        return (it != books_.end()) ? it->second.bestBid() : 0.0;
    }
    double bestAsk(const std::string& symbol) const {
        auto it = books_.find(symbol);
        return (it != books_.end()) ? it->second.bestAsk() : 1e18;
    }

    const ExchangeConfig& config() const { return cfg_; }

private:
    ExchangeConfig                                cfg_;
    std::unordered_map<std::string, OrderBook>    books_;
    std::unordered_map<std::string, Quote>        last_quotes_;
    int order_cnt_  = 0;
    int vfill_cnt_  = 0;

    const Quote& lastQuote(const std::string& sym) const {
        auto it = last_quotes_.find(sym);
        if (it == last_quotes_.end()) throw std::runtime_error("No quote for " + sym);
        return it->second;
    }
};

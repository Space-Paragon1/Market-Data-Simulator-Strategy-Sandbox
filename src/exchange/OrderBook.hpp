#pragma once
#include "../core/Types.hpp"
#include <map>
#include <deque>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <string>

// ── Price-Time Priority Order Book ────────────────────────────────────────────
// Bids stored descending (best bid = highest price first).
// Asks stored ascending  (best ask = lowest  price first).
//
// Returns fills for BOTH the taker and any makers that were matched.
// The SimulationEngine routes fills to strategies by fill.strat_id.
class OrderBook {
public:
    // Add an order; returns all fills generated.
    std::vector<Fill> add(Order order, double comm_per_sh = 0.0) {
        std::vector<Fill> fills;

        if (order.type == OrdType::MARKET) {
            matchMarket(order, fills, comm_per_sh);
        } else {
            matchLimit(order, fills, comm_per_sh);
            // If order still has open qty, rest it in the book
            if (order.status == OrdStatus::OPEN || order.status == OrdStatus::PARTIAL) {
                resting_[order.id] = order;
                if (order.side == Side::BUY)
                    bids_[order.price].push_back(order);
                else
                    asks_[order.price].push_back(order);
            }
        }
        return fills;
    }

    bool cancel(const std::string& id) {
        auto it = resting_.find(id);
        if (it == resting_.end()) return false;
        const Order& o = it->second;
        if (o.side == Side::BUY) removeFrom(bids_, id, o.price);
        else                     removeFrom(asks_, id, o.price);
        resting_.erase(it);
        return true;
    }

    double bestBid() const { return bids_.empty() ? 0.0       : bids_.begin()->first; }
    double bestAsk() const { return asks_.empty() ? 1e18       : asks_.begin()->first; }
    double mid()     const {
        double b = bestBid(), a = bestAsk();
        if (b > 0.0 && a < 1e18) return (b + a) * 0.5;
        if (b > 0.0) return b;
        return a;
    }

    int bidDepth() const {
        int d = 0;
        for (auto& [p, q] : bids_) for (auto& o : q) d += o.qty - o.filled;
        return d;
    }
    int askDepth() const {
        int d = 0;
        for (auto& [p, q] : asks_) for (auto& o : q) d += o.qty - o.filled;
        return d;
    }

private:
    // Bid side: highest price first (buy orders want to pay the most)
    std::map<double, std::deque<Order>, std::greater<double>> bids_;
    // Ask side: lowest price first (sell orders want to receive the least)
    std::map<double, std::deque<Order>>                       asks_;
    // Fast lookup for cancellation
    std::unordered_map<std::string, Order>                    resting_;
    int fill_cnt_ = 0;

    std::string nextFillId() {
        return "F" + std::to_string(++fill_cnt_);
    }

    void matchMarket(Order& taker, std::vector<Fill>& fills, double comm) {
        if (taker.side == Side::BUY)  matchAgainst(taker, asks_, fills, comm);
        else                           matchAgainst(taker, bids_, fills, comm);
        // Market orders: any unmatched qty is simply unfilled (virtual liq added in exchange)
    }

    void matchLimit(Order& taker, std::vector<Fill>& fills, double comm) {
        if (taker.side == Side::BUY)  matchAgainst(taker, asks_, fills, comm, taker.price);
        else                           matchAgainst(taker, bids_, fills, comm, taker.price);
    }

    // Works for both bids (std::greater) and asks (default) maps via template
    template<typename Map>
    void matchAgainst(Order& taker, Map& book, std::vector<Fill>& fills,
                      double comm, double price_lim = -1.0) {
        for (auto& [px, queue] : book) {
            if (taker.filled >= taker.qty) break;

            // Price-limit check
            if (price_lim >= 0.0) {
                if (taker.side == Side::BUY  && px > price_lim) break;
                if (taker.side == Side::SELL && px < price_lim) break;
            }

            while (!queue.empty() && taker.filled < taker.qty) {
                Order& maker = queue.front();   // reference into deque

                int can = std::min(taker.qty - taker.filled,
                                   maker.qty  - maker.filled);

                // ── Taker fill ────────────────────────────────────────────────
                fills.push_back({
                    nextFillId(), taker.id, taker.symbol, taker.strat_id,
                    taker.side, px, can, can * comm, taker.ts
                });

                // ── Maker fill (opposite side) ────────────────────────────────
                fills.push_back({
                    nextFillId(), maker.id, maker.symbol, maker.strat_id,
                    maker.side, px, can, can * comm, taker.ts
                });

                taker.filled += can;
                maker.filled += can;

                if (maker.filled >= maker.qty) {
                    maker.status = OrdStatus::FILLED;
                    resting_.erase(maker.id);
                    queue.pop_front();          // maker reference now invalid
                } else {
                    maker.status = OrdStatus::PARTIAL;
                    // sync resting_ entry
                    resting_[maker.id].filled = maker.filled;
                    resting_[maker.id].status = OrdStatus::PARTIAL;
                    // taker must also be satisfied at this point (can = taker need)
                }
            }
        }
        // Sweep empty price levels
        for (auto it = book.begin(); it != book.end(); ) {
            it = it->second.empty() ? book.erase(it) : std::next(it);
        }

        if      (taker.filled >= taker.qty) taker.status = OrdStatus::FILLED;
        else if (taker.filled > 0)           taker.status = OrdStatus::PARTIAL;
    }

    template<typename Map>
    void removeFrom(Map& book, const std::string& id, double price) {
        auto it = book.find(price);
        if (it == book.end()) return;
        auto& q = it->second;
        for (auto jt = q.begin(); jt != q.end(); ++jt) {
            if (jt->id == id) { q.erase(jt); break; }
        }
        if (q.empty()) book.erase(it);
    }
};

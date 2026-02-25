#pragma once
#include "../core/Types.hpp"
#include "../portfolio/Portfolio.hpp"
#include <deque>
#include <string>

// ── Risk configuration ─────────────────────────────────────────────────────────
struct RiskConfig {
    int    max_position     = 1000;   // max absolute signed position per symbol
    int    max_order_rate   = 20;     // max orders per second
    double max_daily_loss   = 50000;  // stop-out if PnL < -max_daily_loss
    int    max_order_size   = 500;    // max single-order quantity
};

// ── Rejection reason ──────────────────────────────────────────────────────────
enum class RejectReason {
    NONE,
    MAX_POSITION,
    MAX_DAILY_LOSS,
    ORDER_RATE_LIMIT,
    ORDER_SIZE_LIMIT,
    STOPPED_OUT
};

inline const char* rejectStr(RejectReason r) {
    switch (r) {
        case RejectReason::NONE:             return "OK";
        case RejectReason::MAX_POSITION:     return "MAX_POSITION";
        case RejectReason::MAX_DAILY_LOSS:   return "MAX_DAILY_LOSS";
        case RejectReason::ORDER_RATE_LIMIT: return "ORDER_RATE_LIMIT";
        case RejectReason::ORDER_SIZE_LIMIT: return "ORDER_SIZE_LIMIT";
        case RejectReason::STOPPED_OUT:      return "STOPPED_OUT";
    }
    return "UNKNOWN";
}

// ── Risk Engine ────────────────────────────────────────────────────────────────
// Stateful per-strategy risk gate.  Called before every order is sent to the
// exchange.  Returns NONE to approve, or a reason to reject/block.
class RiskEngine {
public:
    explicit RiskEngine(const RiskConfig& cfg = {}) : cfg_(cfg) {}

    RejectReason check(const Order& order, const Portfolio& pf, Timestamp now_ns) {
        if (stopped_out_) return RejectReason::STOPPED_OUT;

        // ── Stop-out check ─────────────────────────────────────────────────
        if (pf.totalPnl() < -cfg_.max_daily_loss) {
            stopped_out_ = true;
            return RejectReason::MAX_DAILY_LOSS;
        }

        // ── Order size ────────────────────────────────────────────────────
        if (order.qty > cfg_.max_order_size)
            return RejectReason::ORDER_SIZE_LIMIT;

        // ── Order rate throttle ───────────────────────────────────────────
        // Purge timestamps older than 1 second
        while (!recent_ts_.empty() && (now_ns - recent_ts_.front()) > NS_PER_SEC)
            recent_ts_.pop_front();
        if (static_cast<int>(recent_ts_.size()) >= cfg_.max_order_rate)
            return RejectReason::ORDER_RATE_LIMIT;

        // ── Max position ──────────────────────────────────────────────────
        int current = pf.position(order.symbol);
        int delta   = (order.side == Side::BUY) ? order.qty : -order.qty;
        int after   = current + delta;
        if (std::abs(after) > cfg_.max_position)
            return RejectReason::MAX_POSITION;

        // ── Approve ────────────────────────────────────────────────────────
        recent_ts_.push_back(now_ns);
        return RejectReason::NONE;
    }

    bool isStoppedOut() const { return stopped_out_; }

    void reset() {
        stopped_out_ = false;
        recent_ts_.clear();
    }

private:
    RiskConfig          cfg_;
    bool                stopped_out_ = false;
    std::deque<Timestamp> recent_ts_;
};

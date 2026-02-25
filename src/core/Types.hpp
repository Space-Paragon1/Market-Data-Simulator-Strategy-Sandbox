#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <cmath>

// ── Time ─────────────────────────────────────────────────────────────────────
using Timestamp = int64_t;                          // nanoseconds since sim epoch
static constexpr int64_t NS_PER_MS  = 1'000'000LL;
static constexpr int64_t NS_PER_SEC = 1'000'000'000LL;

// ── Enums ─────────────────────────────────────────────────────────────────────
enum class Side      { BUY, SELL };
enum class OrdType   { MARKET, LIMIT };
enum class OrdStatus { OPEN, PARTIAL, FILLED, CANCELLED, REJECTED };

inline const char* sideStr(Side s)     { return s == Side::BUY ? "BUY" : "SELL"; }
inline const char* typeStr(OrdType t)  { return t == OrdType::MARKET ? "MKT" : "LMT"; }
inline const char* statusStr(OrdStatus s) {
    switch (s) {
        case OrdStatus::OPEN:      return "OPEN";
        case OrdStatus::PARTIAL:   return "PARTIAL";
        case OrdStatus::FILLED:    return "FILLED";
        case OrdStatus::CANCELLED: return "CANCELLED";
        case OrdStatus::REJECTED:  return "REJECTED";
    }
    return "UNKNOWN";
}

// ── Order ─────────────────────────────────────────────────────────────────────
struct Order {
    std::string  id;
    std::string  symbol;
    std::string  strat_id;
    Side         side     = Side::BUY;
    OrdType      type     = OrdType::LIMIT;
    double       price    = 0.0;        // ignored for MARKET
    int          qty      = 0;
    int          filled   = 0;
    OrdStatus    status   = OrdStatus::OPEN;
    Timestamp    ts       = 0;
};

// ── Market Data ───────────────────────────────────────────────────────────────
struct Quote {
    std::string symbol;
    double      bid        = 0.0;
    double      ask        = 0.0;
    double      mid        = 0.0;
    int         bid_sz     = 0;
    int         ask_sz     = 0;
    double      vol_regime = 1.0;   // 1.0 = normal, 2.0 = high-vol
    Timestamp   ts         = 0;
};

// ── Fill ──────────────────────────────────────────────────────────────────────
struct Fill {
    std::string fill_id;
    std::string order_id;
    std::string symbol;
    std::string strat_id;
    Side        side    = Side::BUY;
    double      price   = 0.0;
    int         qty     = 0;
    double      comm    = 0.0;
    Timestamp   ts      = 0;
};

// ── Portfolio snapshot (for metrics) ─────────────────────────────────────────
struct PortSnapshot {
    Timestamp ts          = 0;
    double    cash        = 0.0;
    double    total_pnl   = 0.0;   // (cash + mtm) - initial_cash
    int       net_pos     = 0;
    double    mid_price   = 0.0;
    double    vol_regime  = 1.0;
    std::string strategy;
};

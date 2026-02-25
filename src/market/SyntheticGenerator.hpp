#pragma once
#include "../core/Types.hpp"
#include <random>
#include <cmath>
#include <unordered_map>
#include <string>
#include <stdexcept>

// ── Per-symbol parameters ──────────────────────────────────────────────────────
struct SymbolParams {
    double initial_price = 100.0;   // starting price
    double half_spread   = 0.05;    // base half-spread in dollars
    double annual_vol    = 0.20;    // annualised volatility (20%)
    double annual_drift  = 0.0;     // annualised drift
};

// ── Synthetic Market Data Generator ───────────────────────────────────────────
// Geometric Brownian Motion with two-state Markov volatility regime switching.
//
//   price(t+dt) = price(t) * exp( (mu - σ²/2)*dt + σ*√dt*Z )
//   Z ~ N(0,1)
//
// Regime switching: switch between normal (1×) and high-vol (2×) each tick
// with probability p_switch = 0.003.
class SyntheticGenerator {
public:
    explicit SyntheticGenerator(uint64_t seed)
        : rng_(seed), ndist_(0.0, 1.0), udist_(0.0, 1.0) {}

    void addSymbol(const std::string& sym, const SymbolParams& p) {
        params_[sym]  = p;
        prices_[sym]  = p.initial_price;
        regimes_[sym] = 1.0;
    }

    // Generate next quote.
    // dt_secs: tick size as a fraction of a trading year
    //   e.g. 100ms tick at 252 trading days, 6.5 hrs/day:
    //   dt = 0.1 / (252 * 6.5 * 3600)  ≈ 1.70e-8 years
    Quote next(const std::string& sym, Timestamp ts, double dt_secs) {
        auto it = params_.find(sym);
        if (it == params_.end()) throw std::runtime_error("Unknown symbol: " + sym);

        const SymbolParams& p  = it->second;
        double&             px = prices_[sym];
        double&             vr = regimes_[sym];

        // Markov regime switch
        if (udist_(rng_) < 0.003)
            vr = (vr > 1.5) ? 1.0 : 2.0;

        // GBM step
        double sigma = p.annual_vol * vr;
        double z     = ndist_(rng_);
        px *= std::exp((p.annual_drift - 0.5 * sigma * sigma) * dt_secs
                       + sigma * std::sqrt(dt_secs) * z);
        px = std::max(px, 0.01);   // price floor

        // Spread widens in high-vol regime
        double hs   = p.half_spread * vr;
        int    bsz  = 50 + static_cast<int>(udist_(rng_) * 200);
        int    asz  = 50 + static_cast<int>(udist_(rng_) * 200);

        return Quote{ sym, px - hs, px + hs, px, bsz, asz, vr, ts };
    }

    double price(const std::string& sym) const {
        auto it = prices_.find(sym);
        return (it != prices_.end()) ? it->second : 0.0;
    }

    double volRegime(const std::string& sym) const {
        auto it = regimes_.find(sym);
        return (it != regimes_.end()) ? it->second : 1.0;
    }

private:
    std::mt19937_64                                rng_;
    std::normal_distribution<double>               ndist_;
    std::uniform_real_distribution<double>         udist_;
    std::unordered_map<std::string, SymbolParams>  params_;
    std::unordered_map<std::string, double>        prices_;
    std::unordered_map<std::string, double>        regimes_;
};

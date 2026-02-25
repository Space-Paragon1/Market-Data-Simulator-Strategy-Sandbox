#pragma once
#include "../market/SyntheticGenerator.hpp"
#include "../exchange/ExchangeSimulator.hpp"
#include "../risk/RiskEngine.hpp"
#include "../strategy/MarketMaker.hpp"
#include "../strategy/Momentum.hpp"
#include "../strategy/MeanReversion.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>

using json = nlohmann::json;

// ── Per-strategy config block ──────────────────────────────────────────────────
struct StrategyConfig {
    std::string type;         // "market_maker" | "momentum" | "mean_reversion"
    std::string id;           // unique id, e.g. "mm1"
    bool        enabled = true;
    // raw JSON params forwarded to each strategy's params struct
    json        params;
};

// ── Top-level simulation config ────────────────────────────────────────────────
struct SimConfig {
    uint64_t    seed                = 42;
    int64_t     duration_seconds    = 3600;
    int64_t     tick_ms             = 100;       // market tick interval
    double      initial_cash        = 1'000'000.0;
    std::string output_dir          = "output";
    std::string symbol              = "AAPL";

    SymbolParams  market;
    ExchangeConfig exchange;
    RiskConfig    risk;

    std::vector<StrategyConfig> strategies;
};

// ── JSON loader ────────────────────────────────────────────────────────────────
inline SimConfig loadConfig(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open config: " + path);
    json j;
    f >> j;

    SimConfig cfg;

    if (j.contains("seed"))             cfg.seed             = j["seed"].get<uint64_t>();
    if (j.contains("duration_seconds")) cfg.duration_seconds = j["duration_seconds"].get<int64_t>();
    if (j.contains("tick_ms"))          cfg.tick_ms          = j["tick_ms"].get<int64_t>();
    if (j.contains("initial_cash"))     cfg.initial_cash     = j["initial_cash"].get<double>();
    if (j.contains("output_dir"))       cfg.output_dir       = j["output_dir"].get<std::string>();
    if (j.contains("symbol"))           cfg.symbol           = j["symbol"].get<std::string>();

    // Market data
    if (j.contains("market")) {
        auto& m = j["market"];
        if (m.contains("initial_price")) cfg.market.initial_price = m["initial_price"].get<double>();
        if (m.contains("half_spread"))   cfg.market.half_spread   = m["half_spread"].get<double>();
        if (m.contains("annual_vol"))    cfg.market.annual_vol    = m["annual_vol"].get<double>();
        if (m.contains("annual_drift"))  cfg.market.annual_drift  = m["annual_drift"].get<double>();
    }

    // Exchange
    if (j.contains("exchange")) {
        auto& e = j["exchange"];
        if (e.contains("commission_per_share")) cfg.exchange.commission_per_share = e["commission_per_share"].get<double>();
        if (e.contains("slippage_bps"))         cfg.exchange.slippage_bps         = e["slippage_bps"].get<double>();
    }

    // Risk
    if (j.contains("risk")) {
        auto& r = j["risk"];
        if (r.contains("max_position"))   cfg.risk.max_position   = r["max_position"].get<int>();
        if (r.contains("max_order_rate")) cfg.risk.max_order_rate = r["max_order_rate"].get<int>();
        if (r.contains("max_daily_loss")) cfg.risk.max_daily_loss = r["max_daily_loss"].get<double>();
        if (r.contains("max_order_size")) cfg.risk.max_order_size = r["max_order_size"].get<int>();
    }

    // Strategies
    if (j.contains("strategies")) {
        for (auto& s : j["strategies"]) {
            StrategyConfig sc;
            sc.type    = s.value("type", "");
            sc.id      = s.value("id", sc.type);
            sc.enabled = s.value("enabled", true);
            if (s.contains("params")) sc.params = s["params"];
            cfg.strategies.push_back(sc);
        }
    }

    return cfg;
}

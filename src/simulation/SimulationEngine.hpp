#pragma once
#include "SimConfig.hpp"
#include "../market/SyntheticGenerator.hpp"
#include "../exchange/ExchangeSimulator.hpp"
#include "../strategy/Strategy.hpp"
#include "../portfolio/Portfolio.hpp"
#include "../risk/RiskEngine.hpp"
#include "../metrics/MetricsRecorder.hpp"
#include <vector>
#include <memory>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <string>
#include <chrono>

// nlohmann::json and 'json' alias come via SimConfig.hpp

// ── Per-strategy runtime state ─────────────────────────────────────────────────
struct StrategySlot {
    std::unique_ptr<Strategy> strat;
    Portfolio                 portfolio;
    RiskEngine                risk;
};

// ── Simulation Engine ──────────────────────────────────────────────────────────
// Drives the event loop: each simulated tick
//   1. Market emits a Quote
//   2. Each strategy observes the Quote, returns Actions
//   3. Risk gate filters Actions
//   4. Exchange processes approved orders → Fills
//   5. Portfolios and strategies update on Fills
//   6. Metrics recorder logs state
class SimulationEngine {
public:
    explicit SimulationEngine(const SimConfig& cfg) : cfg_(cfg) {}

    void build() {
        market_ = std::make_unique<SyntheticGenerator>(cfg_.seed);
        market_->addSymbol(cfg_.symbol, cfg_.market);

        exchange_ = std::make_unique<ExchangeSimulator>(cfg_.exchange);

        for (const auto& sc : cfg_.strategies) {
            if (!sc.enabled) continue;
            StrategySlot slot;
            slot.strat     = makeStrategy(sc);
            slot.portfolio = Portfolio(cfg_.initial_cash);
            slot.risk      = RiskEngine(cfg_.risk);
            metrics_.registerStrategy(sc.id);
            slots_.push_back(std::move(slot));
        }
    }

    void run() {
        using namespace std::chrono;
        auto wall_start = high_resolution_clock::now();

        // Tick size in nanoseconds and as a fraction of a trading year
        const int64_t tick_ns  = cfg_.tick_ms * NS_PER_MS;
        const int64_t end_ns   = cfg_.duration_seconds * NS_PER_SEC;

        // 252 trading days × 6.5 hours × 3600 s/hr = 5,896,800 s/year
        const double dt_year = static_cast<double>(cfg_.tick_ms) / 1000.0
                               / (252.0 * 6.5 * 3600.0);

        int64_t total_ticks = end_ns / tick_ns;
        int64_t log_every   = std::max(total_ticks / 20, int64_t(1)); // 5% progress

        std::cout << "\n=== Simulation Start ===\n"
                  << "  Symbol   : " << cfg_.symbol << "\n"
                  << "  Duration : " << cfg_.duration_seconds << "s\n"
                  << "  Tick     : " << cfg_.tick_ms << "ms\n"
                  << "  Ticks    : " << total_ticks << "\n"
                  << "  Seed     : " << cfg_.seed << "\n"
                  << "  Strategies: " << slots_.size() << "\n\n";

        for (int64_t tick = 0; tick < total_ticks; ++tick) {
            Timestamp ts = tick * tick_ns;

            // ── 1. Generate market data ────────────────────────────────────
            Quote q = market_->next(cfg_.symbol, ts, dt_year);
            exchange_->updateQuote(q);

            // ── 2. Per-strategy processing ─────────────────────────────────
            for (auto& slot : slots_) {
                const std::string& sid = slot.strat->id();

                // Strategy decides actions
                auto actions = slot.strat->onQuote(q, slot.portfolio);

                for (auto& action : actions) {
                    if (action.type == Action::Type::CANCEL) {
                        bool ok = exchange_->cancel(action.cancel_sym, action.cancel_id);
                        if (ok) metrics_.recordCancel(sid);
                        continue;
                    }

                    // ── Risk check ─────────────────────────────────────────
                    Order& ord = action.order;
                    ord.ts = ts;

                    auto reason = slot.risk.check(ord, slot.portfolio, ts);
                    if (reason != RejectReason::NONE) {
                        metrics_.recordReject(sid);
                        continue;
                    }

                    // Record only approved (risk-passed) orders
                    metrics_.recordOrder(ord);

                    // ── Exchange processing ────────────────────────────────
                    auto fills = exchange_->process(ord);

                    for (auto& fill : fills) {
                        // Only route fills that belong to a known strategy slot
                        // (maker fills go to the right strategy)
                        auto* target = findSlot(fill.strat_id);
                        if (target) {
                            target->portfolio.onFill(fill);
                            target->strat->onFill(fill);
                            metrics_.recordFill(fill);
                        }
                    }
                }

                // ── Update portfolio MTM and record snapshot ───────────────
                slot.portfolio.updatePrice(cfg_.symbol, q.mid);
                metrics_.recordSnapshot(
                    slot.portfolio.snapshot(ts, q.mid, q.vol_regime, sid));
            }

            // ── Progress print ─────────────────────────────────────────────
            if (tick % log_every == 0) {
                double pct = 100.0 * tick / total_ticks;
                std::cout << std::fixed << std::setprecision(1)
                          << "  [" << std::setw(5) << pct << "%]  tick=" << tick
                          << "  mid=" << std::setprecision(2) << q.mid
                          << "  regime=" << std::setprecision(1) << q.vol_regime;
                for (auto& s : slots_)
                    std::cout << "  " << s.strat->id()
                              << " pnl=" << std::setprecision(2)
                              << s.portfolio.totalPnl();
                std::cout << "\n";
            }
        }

        auto wall_end = high_resolution_clock::now();
        double wall_s = duration<double>(wall_end - wall_start).count();

        // ── Output ─────────────────────────────────────────────────────────
        writeOutputs(total_ticks, wall_s);
    }

private:
    SimConfig                      cfg_;
    std::unique_ptr<SyntheticGenerator> market_;
    std::unique_ptr<ExchangeSimulator>  exchange_;
    std::vector<StrategySlot>      slots_;
    MetricsRecorder                metrics_;

    // ── Strategy factory ───────────────────────────────────────────────────
    std::unique_ptr<Strategy> makeStrategy(const StrategyConfig& sc) {
        const json& p = sc.params;
        if (sc.type == "market_maker") {
            MarketMakerParams mp;
            if (p.contains("order_size"))       mp.order_size       = p["order_size"].get<int>();
            if (p.contains("spread_bps"))       mp.spread_bps       = p["spread_bps"].get<double>();
            if (p.contains("max_inventory"))    mp.max_inventory    = p["max_inventory"].get<int>();
            if (p.contains("skew_factor"))      mp.skew_factor      = p["skew_factor"].get<double>();
            if (p.contains("vol_spread_mult"))  mp.vol_spread_mult  = p["vol_spread_mult"].get<double>();
            return std::make_unique<MarketMaker>(sc.id, cfg_.symbol, mp);
        }
        if (sc.type == "momentum") {
            MomentumParams mp;
            if (p.contains("fast_period"))     mp.fast_period     = p["fast_period"].get<int>();
            if (p.contains("slow_period"))     mp.slow_period     = p["slow_period"].get<int>();
            if (p.contains("target_position")) mp.target_position = p["target_position"].get<int>();
            if (p.contains("threshold_bps"))   mp.threshold_bps   = p["threshold_bps"].get<double>();
            if (p.contains("min_ticks"))       mp.min_ticks       = p["min_ticks"].get<int>();
            return std::make_unique<Momentum>(sc.id, cfg_.symbol, mp);
        }
        if (sc.type == "mean_reversion") {
            MeanReversionParams mp;
            if (p.contains("lookback"))    mp.lookback    = p["lookback"].get<int>();
            if (p.contains("entry_z"))     mp.entry_z     = p["entry_z"].get<double>();
            if (p.contains("exit_z"))      mp.exit_z      = p["exit_z"].get<double>();
            if (p.contains("order_size"))  mp.order_size  = p["order_size"].get<int>();
            if (p.contains("min_ticks"))   mp.min_ticks   = p["min_ticks"].get<int>();
            return std::make_unique<MeanReversion>(sc.id, cfg_.symbol, mp);
        }
        throw std::runtime_error("Unknown strategy type: " + sc.type);
    }

    StrategySlot* findSlot(const std::string& id) {
        for (auto& s : slots_)
            if (s.strat->id() == id) return &s;
        return nullptr;
    }

    // ── Write CSV + report.json + console summary ──────────────────────────
    void writeOutputs(int64_t total_ticks, double wall_secs) const {
        // Ensure output dir exists
        std::filesystem::create_directories(cfg_.output_dir);

        // CSV files
        metrics_.writeAll(cfg_.output_dir);

        // Compute summaries
        auto summaries = metrics_.allSummaries();

        // ── Console ────────────────────────────────────────────────────────
        std::cout << "\n══════════════════════════════════════════════════════\n";
        std::cout << "  SIMULATION COMPLETE — " << total_ticks << " ticks  "
                  << std::fixed << std::setprecision(2) << wall_secs << "s wall\n";
        std::cout << "══════════════════════════════════════════════════════\n";
        std::cout << std::left
                  << std::setw(18) << "Strategy"
                  << std::setw(10) << "Orders"
                  << std::setw(10) << "Fills"
                  << std::setw(10) << "FillRat%"
                  << std::setw(14) << "FinalPnL($)"
                  << std::setw(14) << "MaxDD($)"
                  << std::setw(10) << "Sharpe"
                  << "\n";
        std::cout << std::string(86, '-') << "\n";
        for (auto& m : summaries) {
            std::cout << std::left
                      << std::setw(18) << m.strategy
                      << std::setw(10) << m.total_orders
                      << std::setw(10) << m.total_fills
                      << std::setw(10) << std::fixed << std::setprecision(1)
                                       << m.fill_ratio * 100.0
                      << std::setw(14) << std::setprecision(2) << m.final_pnl
                      << std::setw(14) << std::setprecision(2) << m.max_drawdown
                      << std::setw(10) << std::setprecision(3) << m.sharpe
                      << "\n";
        }
        std::cout << "\nOutput CSVs → " << cfg_.output_dir << "/\n";

        // ── report.json ────────────────────────────────────────────────────
        json report;
        report["simulation"]["seed"]              = cfg_.seed;
        report["simulation"]["duration_seconds"]  = cfg_.duration_seconds;
        report["simulation"]["tick_ms"]           = cfg_.tick_ms;
        report["simulation"]["total_ticks"]       = total_ticks;
        report["simulation"]["wall_seconds"]      = wall_secs;
        report["simulation"]["symbol"]            = cfg_.symbol;

        for (auto& m : summaries) {
            json s;
            s["total_orders"]    = m.total_orders;
            s["total_fills"]     = m.total_fills;
            s["rejected_orders"] = m.rejected_orders;
            s["fill_ratio"]      = m.fill_ratio;
            s["final_pnl"]       = m.final_pnl;
            s["max_drawdown"]    = m.max_drawdown;
            s["sharpe_ratio"]    = m.sharpe;
            s["max_position"]    = m.max_position;
            s["min_position"]    = m.min_position;
            report["strategies"][m.strategy] = s;
        }

        std::ofstream rf(cfg_.output_dir + "/report.json");
        rf << report.dump(2) << "\n";
        std::cout << "Report      → " << cfg_.output_dir << "/report.json\n\n";
    }
};

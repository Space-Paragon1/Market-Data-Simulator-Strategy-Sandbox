#pragma once
#include "../core/Types.hpp"
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>
#include <stdexcept>

// ── Per-strategy recorded data ─────────────────────────────────────────────────
struct StrategyRecord {
    std::string              name;
    std::vector<PortSnapshot> snapshots;      // one per tick
    std::vector<Fill>         fills;
    std::vector<Order>        orders;         // submitted
    int                       rejected  = 0;
    int                       cancelled = 0;
};

// ── Computed summary metrics ───────────────────────────────────────────────────
struct SummaryMetrics {
    std::string strategy;
    int    total_orders    = 0;
    int    total_fills     = 0;
    int    rejected_orders = 0;
    int    cancelled_orders= 0;
    double fill_ratio      = 0.0;
    double final_pnl       = 0.0;
    double max_drawdown    = 0.0;   // magnitude (negative value)
    double sharpe          = 0.0;
    int    max_position    = 0;
    int    min_position    = 0;
};

// ── Metrics Recorder ──────────────────────────────────────────────────────────
class MetricsRecorder {
public:
    void registerStrategy(const std::string& name) {
        records_[name].name = name;
    }

    void recordSnapshot(const PortSnapshot& s) {
        records_[s.strategy].snapshots.push_back(s);
    }

    void recordFill(const Fill& f) {
        records_[f.strat_id].fills.push_back(f);
    }

    void recordOrder(const Order& o) {
        records_[o.strat_id].orders.push_back(o);
    }

    void recordReject(const std::string& strat_id) {
        records_[strat_id].rejected++;
    }

    void recordCancel(const std::string& strat_id) {
        records_[strat_id].cancelled++;
    }

    // ── Compute summary metrics ───────────────────────────────────────────
    SummaryMetrics computeSummary(const std::string& name) const {
        const auto& rec = records_.at(name);
        SummaryMetrics m;
        m.strategy       = name;
        m.total_orders   = static_cast<int>(rec.orders.size());
        m.total_fills    = static_cast<int>(rec.fills.size());
        m.rejected_orders = rec.rejected;
        m.cancelled_orders = rec.cancelled;
        m.fill_ratio     = (m.total_orders > 0)
                           ? static_cast<double>(m.total_fills) / m.total_orders
                           : 0.0;

        if (!rec.snapshots.empty())
            m.final_pnl = rec.snapshots.back().total_pnl;

        m.max_drawdown = maxDrawdown(rec.snapshots);
        m.sharpe       = sharpe(rec.snapshots);

        for (auto& s : rec.snapshots) {
            m.max_position = std::max(m.max_position, s.net_pos);
            m.min_position = std::min(m.min_position, s.net_pos);
        }
        return m;
    }

    std::vector<SummaryMetrics> allSummaries() const {
        std::vector<SummaryMetrics> out;
        for (auto& [name, rec] : records_)
            out.push_back(computeSummary(name));
        return out;
    }

    // ── CSV writers ───────────────────────────────────────────────────────
    void writePnlCsv(const std::string& path) const {
        std::ofstream f(path);
        if (!f) throw std::runtime_error("Cannot open " + path);

        // Header: ts + one column per strategy
        f << "timestamp_ns";
        for (auto& [name, rec] : records_) f << "," << name;
        f << "\n";

        // Align by index (all strategies run the same ticks)
        size_t n = 0;
        for (auto& [name, rec] : records_) n = std::max(n, rec.snapshots.size());

        for (size_t i = 0; i < n; ++i) {
            // Use timestamp from first strategy with data at this index
            Timestamp ts = 0;
            for (auto& [name, rec] : records_)
                if (i < rec.snapshots.size()) { ts = rec.snapshots[i].ts; break; }
            f << ts;
            for (auto& [name, rec] : records_)
                f << "," << (i < rec.snapshots.size() ? rec.snapshots[i].total_pnl : 0.0);
            f << "\n";
        }
    }

    void writeFillsCsv(const std::string& path) const {
        std::ofstream f(path);
        if (!f) throw std::runtime_error("Cannot open " + path);
        f << "fill_id,order_id,symbol,strategy,side,price,qty,commission,timestamp_ns\n";
        for (auto& [name, rec] : records_)
            for (auto& fl : rec.fills)
                f << fl.fill_id << "," << fl.order_id << "," << fl.symbol << ","
                  << fl.strat_id << "," << sideStr(fl.side) << ","
                  << std::fixed << std::setprecision(4) << fl.price << ","
                  << fl.qty << ","
                  << std::fixed << std::setprecision(4) << fl.comm << ","
                  << fl.ts << "\n";
    }

    void writeOrdersCsv(const std::string& path) const {
        std::ofstream f(path);
        if (!f) throw std::runtime_error("Cannot open " + path);
        f << "order_id,symbol,strategy,side,type,price,qty,filled,status,timestamp_ns\n";
        for (auto& [name, rec] : records_)
            for (auto& o : rec.orders)
                f << o.id << "," << o.symbol << "," << o.strat_id << ","
                  << sideStr(o.side) << "," << typeStr(o.type) << ","
                  << std::fixed << std::setprecision(4) << o.price << ","
                  << o.qty << "," << o.filled << "," << statusStr(o.status) << ","
                  << o.ts << "\n";
    }

    // Write all CSVs at once
    void writeAll(const std::string& dir) const {
        writePnlCsv(dir + "/pnl.csv");
        writeFillsCsv(dir + "/fills.csv");
        writeOrdersCsv(dir + "/orders.csv");
    }

    const std::unordered_map<std::string, StrategyRecord>& records() const {
        return records_;
    }

private:
    std::unordered_map<std::string, StrategyRecord> records_;

    // Max drawdown: largest peak-to-trough decline in PnL
    static double maxDrawdown(const std::vector<PortSnapshot>& snaps) {
        if (snaps.empty()) return 0.0;
        double peak = snaps[0].total_pnl;
        double mdd  = 0.0;
        for (auto& s : snaps) {
            peak = std::max(peak, s.total_pnl);
            mdd  = std::min(mdd, s.total_pnl - peak);
        }
        return mdd;
    }

    // Annualised Sharpe ratio from PnL curve (tick-level returns)
    // Assumes ticks are uniform; annualises assuming 252 days × 6.5 hrs × 3600 s
    static double sharpe(const std::vector<PortSnapshot>& snaps) {
        if (snaps.size() < 2) return 0.0;
        std::vector<double> rets;
        rets.reserve(snaps.size() - 1);
        for (size_t i = 1; i < snaps.size(); ++i)
            rets.push_back(snaps[i].total_pnl - snaps[i-1].total_pnl);
        double mu = 0.0;
        for (double r : rets) mu += r;
        mu /= rets.size();
        double var = 0.0;
        for (double r : rets) var += (r - mu) * (r - mu);
        double sd = std::sqrt(var / rets.size());
        if (sd < 1e-12) return 0.0;
        // Annualise: sqrt(ticks_per_year)
        // Default 100ms ticks: 252 * 6.5 * 3600 * 10 = 59,022,000
        // We use a conservative 252 * 6.5 * 3600 * 10 ticks/year
        double tpy = 252.0 * 6.5 * 3600.0 * 10.0;
        return (mu / sd) * std::sqrt(tpy);
    }
};

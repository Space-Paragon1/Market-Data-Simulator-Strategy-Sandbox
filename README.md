# Market Data Simulator + Strategy Sandbox

> **"Built an event-driven trading simulation sandbox with plug-in strategies,
> exchange fills, risk controls, and performance analytics—designed for
> reproducible experiments via seeded configs."**

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                      SimulationEngine (main loop)                   │
│                                                                     │
│  ┌──────────────────┐    Quote     ┌──────────────────────────────┐ │
│  │ SyntheticGenerator│─────────────▶│  Strategy Slots (per strat) │ │
│  │  (GBM + regimes) │              │                              │ │
│  └──────────────────┘              │  ┌────────────┐  Actions     │ │
│                                    │  │  Strategy  │──────────┐   │ │
│  ┌──────────────────┐              │  │  Interface │          │   │ │
│  │ ExchangeSimulator│◀─────────────│  └────────────┘          │   │ │
│  │  ┌────────────┐  │  Orders      │  ┌────────────┐          │   │ │
│  │  │ OrderBook  │  │              │  │ Portfolio  │◀──Fills──┘   │ │
│  │  │(price-time │  │──Fills──────▶│  └────────────┘              │ │
│  │  │  priority) │  │              │  ┌────────────┐              │ │
│  │  └────────────┘  │              │  │ RiskEngine │              │ │
│  └──────────────────┘              │  └────────────┘              │ │
│                                    └──────────────────────────────┘ │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │          MetricsRecorder  →  CSVs  +  report.json           │    │
│  └─────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────┘
```

### Simulation Loop (per tick)

```
for tick in 0 .. N_ticks:
    quote  = MarketGenerator.next(ts)          // GBM + vol regime
    exchange.updateQuote(quote)

    for each strategy:
        actions = strategy.onQuote(quote, portfolio)

        for action in actions:
            if CANCEL  → exchange.cancel(order_id)
            if SUBMIT  → risk.check(order)
                           → if OK   : fills = exchange.process(order)
                                         portfolio.onFill(fill)
                                         strategy.onFill(fill)
                                         metrics.record(fill)
                           → if FAIL : metrics.recordReject()

        portfolio.updatePrice(quote.mid)
        metrics.recordSnapshot(portfolio)
```

---

## Components

| Component | File | Description |
|-----------|------|-------------|
| Types | `src/core/Types.hpp` | Order, Fill, Quote, enums |
| Market Data | `src/market/SyntheticGenerator.hpp` | GBM price model + Markov vol regimes |
| Order Book | `src/exchange/OrderBook.hpp` | Price-time priority matching (limit + market) |
| Exchange | `src/exchange/ExchangeSimulator.hpp` | Multi-symbol book + virtual liquidity |
| Strategy API | `src/strategy/Strategy.hpp` | Plug-in interface returning `Action` vectors |
| Market Maker | `src/strategy/MarketMaker.hpp` | Inventory-aware two-sided quoting |
| Momentum | `src/strategy/Momentum.hpp` | EMA crossover directional strategy |
| Mean Reversion | `src/strategy/MeanReversion.hpp` | Z-score threshold entry/exit |
| Portfolio | `src/portfolio/Portfolio.hpp` | Cash + signed positions + MTM PnL |
| Risk Engine | `src/risk/RiskEngine.hpp` | Position limits, order rate, daily loss |
| Metrics | `src/metrics/MetricsRecorder.hpp` | PnL curve, max drawdown, Sharpe, CSVs |
| Config | `src/simulation/SimConfig.hpp` | JSON config loader |
| Engine | `src/simulation/SimulationEngine.hpp` | Main event-driven loop |

---

## Build

### Prerequisites
- CMake ≥ 3.15
- C++17 compiler (MSVC 2019+, GCC 9+, Clang 10+)
- Internet access on first build (CMake fetches `nlohmann/json`)

### Linux / macOS

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
./market_sim
```

### Windows (MSVC) — Recommended: x64 Native Tools Command Prompt

The easiest way on Windows is to open the **x64 Native Tools Command Prompt**,
which sets up CMake and the MSVC compiler automatically:

1. Press the **Windows key** (the ⊞ logo key on your keyboard)
2. Type `x64 native`
3. Click **"x64 Native Tools Command Prompt for VS 2022"**

Then in that terminal, run **one command at a time**:

```cmd
cd "C:\Users\DELL\OneDrive - Grambling State University\Desktop\Market-Data-Simulator-Strategy-Sandbox"
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
Release\market_sim.exe
```

### Windows — Alternative: Regular PowerShell

If you prefer PowerShell, first add the VS tools to your PATH (run once per
session), then build:

```powershell
# Step 1 — add CMake and MSVC compiler to PATH for this session
$env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;" + $env:PATH
$env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64;" + $env:PATH

# Step 2 — navigate to project root (NOT already inside build/)
cd "C:\Users\DELL\OneDrive - Grambling State University\Desktop\Market-Data-Simulator-Strategy-Sandbox"

# Step 3 — create build folder and configure (run each line separately)
mkdir build
cd build
cmake ..

# Step 4 — compile
cmake --build . --config Release

# Step 5 — run
.\Release\market_sim.exe
```

> **Important:** Run each command one at a time. Do not paste the whole block
> at once — `cd` must take effect before the next command runs.

---

## Run

```cmd
# Windows — from inside the build/ folder
Release\market_sim.exe

# With a custom config
Release\market_sim.exe --config ..\config\default_config.json
```

```bash
# Linux / macOS
./market_sim
./market_sim --config path/to/my_config.json
```

---

## Common Errors & Fixes

### VS 2022 Build Tools — `cmake` or `cl` not recognized in PowerShell / VS Code terminal

**What happened:** You opened a regular PowerShell (or VS Code's default terminal)
and ran `cmake ..`. Windows reported:
```
cmake : The term 'cmake' is not recognized as a name of a cmdlet, function,
script file, or executable program.
```
**Why:** Visual Studio installs CMake and the MSVC compiler (`cl.exe`) inside
its own folder — they are NOT added to the system PATH automatically.

**Fix A — Temporary (current session only):** Paste both lines into PowerShell
before running cmake:
```powershell
$env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;" + $env:PATH
$env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64;" + $env:PATH
```
You must re-run these every time you open a new terminal.

**Fix B — Permanent (use x64 Native Tools Command Prompt):**
Press the Windows key, type `x64 native`, open
**"x64 Native Tools Command Prompt for VS 2022"** — cmake and cl are always
available there with no extra setup.

---

### Connecting the VS 2022 Build Tools PATH to VS Code's Terminal

By default, VS Code opens a plain PowerShell terminal that does not know about
Visual Studio tools. There are two ways to fix this permanently:

#### Option 1 — Add a dedicated terminal profile (Recommended)

1. In VS Code press `Ctrl + Shift + P`, type **"Open User Settings (JSON)"**, press Enter.
2. Add the following inside the outer `{}` of your `settings.json`:

```jsonc
"terminal.integrated.profiles.windows": {
  "x64 Native Tools (VS 2022)": {
    "path": "cmd.exe",
    "args": [
      "/k",
      "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat"
    ]
  }
},
"terminal.integrated.defaultProfile.windows": "x64 Native Tools (VS 2022)"
```

3. Save the file. Close and reopen any terminal inside VS Code (`Ctrl + `` ` ``).
4. The new terminal will show:
   ```
   **********************************************************************
   ** Visual Studio 2022 Developer Command Prompt
   **********************************************************************
   [vcvarsall.bat] Environment initialized for: 'x64'
   ```
   Now `cmake` and `cl` work directly inside VS Code's terminal.

#### Option 2 — Add the PATH permanently to VS Code's terminal environment

Add this to your `settings.json` instead (keeps PowerShell as the shell but
injects the paths automatically):

```jsonc
"terminal.integrated.env.windows": {
  "PATH": "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin;C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Tools\\MSVC\\14.44.35207\\bin\\Hostx64\\x64;${env:PATH}"
}
```

---

### `Error: could not load cache`
`cmake --build` was run from the wrong directory (not the `build/` folder
where `CMakeCache.txt` lives).
- **Fix:** Make sure you `cd build` first, then run `cmake --build . --config Release`.
  Check you are in the right folder with `pwd` (PowerShell) or `cd` (cmd).

### `mkdir build && cd build` goes into `build\build\`
If you are already inside `build/` and run `mkdir build && cd build`, it
creates a nested `build\build\` folder and cmake runs from the wrong place.
- **Fix:** Run `mkdir` and `cd` as separate commands, and verify your current
  directory with `pwd` before running cmake.

### `./market_sim` not recognized (PowerShell)
PowerShell uses `.\` not `./`, and the binary is inside `Release\`.
- **Fix:** Use `.\Release\market_sim.exe` from inside the `build/` folder.

### First build is slow / hangs
CMake is downloading `nlohmann/json` from GitHub on first run.
- **Fix:** Wait — it only happens once. Subsequent builds are fast.

### Build succeeds but `output/` folder is missing
The output folder is created automatically at runtime. If it does not appear,
check that `market_sim.exe` ran without errors.
- **Fix:** Run from the `build/` directory so relative paths resolve correctly:
  ```cmd
  cd build
  Release\market_sim.exe
  ```

Output is written to `output/` (or the path in your config):

```
output/
  pnl.csv        # tick-level PnL for every strategy
  fills.csv      # every fill with price, qty, commission
  orders.csv     # every submitted order with status
  report.json    # summary statistics
```

---

## Configuration (`config/default_config.json`)

```jsonc
{
  "seed": 42,                    // RNG seed (reproducible)
  "duration_seconds": 3600,      // sim length
  "tick_ms": 100,                // market data tick interval
  "initial_cash": 1000000.0,

  "market": {
    "initial_price": 150.0,
    "half_spread":   0.05,       // $0.05 per side
    "annual_vol":    0.25,       // 25% annualised vol
    "annual_drift":  0.0
  },
  "exchange": {
    "commission_per_share": 0.001,
    "slippage_bps": 1.0          // market order slippage
  },
  "risk": {
    "max_position":   1000,      // max abs shares per strategy
    "max_order_rate":   30,      // orders/second
    "max_daily_loss": 50000.0,   // stop-out threshold
    "max_order_size":   500
  },
  "strategies": [ ... ]
}
```

---

## Strategies

### Market Maker (`"type": "market_maker"`)
Places resting limit bid/ask around the mid-price each tick.

| Param | Default | Description |
|-------|---------|-------------|
| `order_size` | 100 | shares per side |
| `spread_bps` | 10 | base full spread (bps) |
| `max_inventory` | 500 | stops quoting one side at this abs position |
| `skew_factor` | 0.3 | inventory-skew strength (bps per share) |
| `vol_spread_mult` | 1.8 | spread multiplier in high-vol regime |

**Inventory skew:** when long, the MM shades both quotes down to encourage
buying from the MM, reducing exposure. When short, quotes shift up.

### Momentum (`"type": "momentum"`)
EMA crossover signal — buys/sells with market orders.

| Param | Default | Description |
|-------|---------|-------------|
| `fast_period` | 5 | fast EMA span (ticks) |
| `slow_period` | 20 | slow EMA span (ticks) |
| `target_position` | 200 | max directional position |
| `threshold_bps` | 2.0 | signal dead-band |

### Mean Reversion (`"type": "mean_reversion"`)
Z-score threshold entries; closes on mean-reversion.

| Param | Default | Description |
|-------|---------|-------------|
| `lookback` | 40 | rolling window (ticks) |
| `entry_z` | 1.5 | z-score to enter |
| `exit_z` | 0.3 | z-score to exit |
| `order_size` | 150 | shares per trade |

---

## Risk Controls

The `RiskEngine` blocks or throttles orders before they reach the exchange:

| Check | Behaviour |
|-------|-----------|
| Max position | Order rejected if post-fill abs position > limit |
| Max order rate | Sliding 1-second window; excess orders dropped |
| Max daily loss | Total PnL < -threshold → strategy stopped out |
| Max order size | Single-order qty cap |

---

## Outputs

### `pnl.csv`
```
timestamp_ns,mm,mom,mr
0,0.00,0.00,0.00
100000000,12.34,0.00,0.00
200000000,18.72,-4.10,0.00
...
```

### `fills.csv`
```
fill_id,order_id,symbol,strategy,side,price,qty,commission,timestamp_ns
F1,mm_B1,AAPL,mm,BUY,149.9300,100,0.1000,100000000
...
```

### `report.json` (example)
```json
{
  "simulation": {
    "seed": 42,
    "duration_seconds": 3600,
    "tick_ms": 100,
    "total_ticks": 36000,
    "symbol": "AAPL"
  },
  "strategies": {
    "mm": {
      "total_orders": 71998,
      "total_fills": 4231,
      "fill_ratio": 0.059,
      "final_pnl": 1842.50,
      "max_drawdown": -612.30,
      "sharpe_ratio": 1.74
    },
    "mom": {
      "total_orders": 312,
      "total_fills": 312,
      "fill_ratio": 1.000,
      "final_pnl": -287.40,
      "max_drawdown": -945.10,
      "sharpe_ratio": -0.31
    },
    "mr": {
      "total_orders": 88,
      "total_fills": 62,
      "fill_ratio": 0.705,
      "final_pnl": 543.20,
      "max_drawdown": -321.80,
      "sharpe_ratio": 0.89
    }
  }
}
```

---

## Adding a New Strategy

1. Create `src/strategy/MyStrategy.hpp` inheriting from `Strategy`.
2. Implement `onQuote()` and `onFill()`.
3. Register a new `"type"` string in `SimulationEngine::makeStrategy()`.
4. Add an entry to `config/default_config.json` under `"strategies"`.

```cpp
class MyStrategy : public Strategy {
    std::string id_;
public:
    MyStrategy(const std::string& id, const std::string& sym) : id_(id) {}
    const std::string& id() const override { return id_; }

    std::vector<Action> onQuote(const Quote& q, const Portfolio& pf) override {
        // ... your logic ...
        return {};
    }
    void onFill(const Fill& f) override {}
};
```

---

## Reproducibility

All random numbers flow from a single seeded `std::mt19937_64`.
Running the same config twice produces **identical** price paths, fills,
and metrics—enabling controlled A/B comparisons between strategies.

```bash
# Two runs with seed=42 → identical output
./market_sim --config config/default_config.json
./market_sim --config config/default_config.json
diff output/pnl.csv output_copy/pnl.csv   # → empty diff
```

---

## Project Layout

```
Market-Data-Simulator-Strategy-Sandbox/
├── CMakeLists.txt
├── config/
│   └── default_config.json
├── src/
│   ├── core/           Types.hpp
│   ├── market/         SyntheticGenerator.hpp
│   ├── exchange/       OrderBook.hpp  ExchangeSimulator.hpp
│   ├── strategy/       Strategy.hpp  MarketMaker.hpp
│   │                   Momentum.hpp  MeanReversion.hpp
│   ├── portfolio/      Portfolio.hpp
│   ├── risk/           RiskEngine.hpp
│   ├── metrics/        MetricsRecorder.hpp
│   ├── simulation/     SimConfig.hpp  SimulationEngine.hpp
│   └── main.cpp
├── output/             (generated: pnl.csv fills.csv orders.csv report.json)
└── data/               (optional replay CSV files)
```

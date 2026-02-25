#include "simulation/SimulationEngine.hpp"
#include <iostream>
#include <stdexcept>

int main(int argc, char* argv[]) {
    // ── Parse args ─────────────────────────────────────────────────────────────
    std::string config_path = "config/default_config.json";
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: market_sim [--config <path>]\n"
                      << "  --config, -c  Path to JSON config (default: config/default_config.json)\n"
                      << "  --help,   -h  Show this help\n";
            return 0;
        } else {
            // treat bare argument as config path
            config_path = arg;
        }
    }

    try {
        std::cout << "Loading config: " << config_path << "\n";
        SimConfig cfg = loadConfig(config_path);

        SimulationEngine engine(cfg);
        engine.build();
        engine.run();

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

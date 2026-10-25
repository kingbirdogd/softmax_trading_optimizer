#include "config.hpp"
#include "replay.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

sto::ModelConfig resolve_config(int argc, char** argv) {
    const std::filesystem::path default_cfg = "config/default.json";
    const std::filesystem::path current_dir = std::filesystem::current_path();

    if (argc > 1) {
        const std::filesystem::path user_cfg = argv[1];
        return sto::ModelConfig::load_from_file(user_cfg.string());
    }

    const std::filesystem::path candidate = current_dir / default_cfg;
    if (std::filesystem::exists(candidate)) {
        return sto::ModelConfig::load_from_file(candidate.string());
    }

    return sto::ModelConfig::default_config();
}

} // namespace

int main(int argc, char** argv) {
    using namespace sto;
    try {
        const auto cfg = resolve_config(argc, argv);
        std::cout << "Loaded config with fair_price_historical_samples=" << cfg.fair_price_historical_samples
                  << ", fair_price_preopen_prices=" << cfg.fair_price_preopen_prices
                  << ", x1_ticks=" << cfg.x1_ticks
                  << ", x2_ticks=" << cfg.x2_ticks << "\n";

        std::cout << "Configuration is valid. Use run_model_walkforward_dry_run() with timestamp-ordered "
                     "market sessions to train and replay the causal baseline.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Configuration error: " << e.what() << "\n";
        return 1;
    }
}

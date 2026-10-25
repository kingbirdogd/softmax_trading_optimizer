#include "config.hpp"
#include "optimizer.hpp"
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

        auto objective=[](double x1,double x2) {
            return 100.0 - (x1-4.0)*(x1-4.0) - 0.7*(x2-6.0)*(x2-6.0);
        };
        auto result=search_x1_x2(objective,{1,2,3,4,5,6,7,8,9,10});
        std::cout << "Baseline X1=" << result.x1
                  << " X2=" << result.x2
                  << " objective=" << result.objective << "\n";
        std::cout << "Build succeeded. Connect your market-data loader and real replay objective next.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Configuration error: " << e.what() << "\n";
        return 1;
    }
}

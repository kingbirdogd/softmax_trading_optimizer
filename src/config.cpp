#include "config.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace sto {
namespace {

std::string trim(const std::string& s) {
    const auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

bool read_bool(const std::string& value, bool def) {
    std::string v = trim(value);
    if (v.empty()) return def;
    if (v == "1" || v == "true" || v == "True" || v == "TRUE") return true;
    if (v == "0" || v == "false" || v == "False" || v == "FALSE") return false;
    throw std::runtime_error("Invalid boolean value: " + value);
}

std::size_t read_size_t(const std::string& value, std::size_t def) {
    std::string v = trim(value);
    if (v.empty()) return def;
    std::size_t out = 0;
    std::size_t idx = 0;
    try {
        out = std::stoul(v, &idx);
    } catch (...) {
        throw std::runtime_error("Invalid size value: " + value);
    }
    if (idx != v.size()) throw std::runtime_error("Invalid size value: " + value);
    return out;
}

double read_double(const std::string& value, double def) {
    std::string v = trim(value);
    if (v.empty()) return def;
    return std::stod(v);
}

std::unordered_map<std::string, std::string> parse_json_object(const std::string& text) {
    std::unordered_map<std::string, std::string> values;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos) {
            line = line.substr(0, hash);
        }
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = trim(line.substr(0, colon));
        std::string value = trim(line.substr(colon + 1));
        if (!key.empty()) {
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }
            if (value.size() >= 2 && value.front() == '{' && value.back() == '}') {
                value = trim(value.substr(1, value.size() - 2));
            }
            values[key] = value;
        }
    }
    return values;
}

} // namespace

ModelConfig ModelConfig::default_config() {
    ModelConfig cfg;
    cfg.fair_price_historical_samples = 60;
    cfg.fair_price_historical_samples_min = 30;
    cfg.fair_price_historical_samples_max = 90;
    cfg.fair_price_preopen_prices = 30;
    cfg.fair_price_preopen_prices_min = 10;
    cfg.fair_price_preopen_prices_max = 60;

    cfg.risk_historical_price_stds = 30;
    cfg.risk_historical_price_stds_min = 10;
    cfg.risk_historical_price_stds_max = 60;
    cfg.risk_historical_spread_means = 30;
    cfg.risk_historical_spread_means_min = 10;
    cfg.risk_historical_spread_means_max = 60;
    cfg.risk_historical_spread_stds = 30;
    cfg.risk_historical_spread_stds_min = 10;
    cfg.risk_historical_spread_stds_max = 60;
    cfg.risk_intraday_price_std_samples = 10;
    cfg.risk_intraday_price_std_samples_min = 5;
    cfg.risk_intraday_price_std_samples_max = 20;
    cfg.risk_intraday_spread_mean_samples = 10;
    cfg.risk_intraday_spread_mean_samples_min = 5;
    cfg.risk_intraday_spread_mean_samples_max = 20;
    cfg.risk_intraday_spread_std_samples = 10;
    cfg.risk_intraday_spread_std_samples_min = 5;
    cfg.risk_intraday_spread_std_samples_max = 20;

    cfg.tick_size = 0.01;
    cfg.fixed_fee_ticks = 1.0;
    cfg.x1_ticks = 3.0;
    cfg.x2_ticks = 3.0;
    cfg.fee_per_side = 0.0;
    cfg.quantity = 1.0;
    cfg.liquidate_at_end = true;
    cfg.warmup_days = 30;
    cfg.first_dry_run_day = 31;
    cfg.rolling_feedback_window = 10;
    cfg.carry_position_forward = true;
    return cfg;
}

ModelConfig ModelConfig::load_from_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Unable to open config file: " + path);
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::unordered_map<std::string, std::string> values = parse_json_object(buffer.str());

    ModelConfig cfg = default_config();

    const auto has_key = [&](const std::string& k) {
        return values.find(k) != values.end();
    };

    if (has_key("fair_price_historical_samples")) {
        cfg.fair_price_historical_samples = read_size_t(values.at("fair_price_historical_samples"), cfg.fair_price_historical_samples);
    }
    if (has_key("fair_price_historical_samples_min")) {
        cfg.fair_price_historical_samples_min = read_size_t(values.at("fair_price_historical_samples_min"), cfg.fair_price_historical_samples_min);
    }
    if (has_key("fair_price_historical_samples_max")) {
        cfg.fair_price_historical_samples_max = read_size_t(values.at("fair_price_historical_samples_max"), cfg.fair_price_historical_samples_max);
    }
    if (has_key("fair_price_preopen_prices")) {
        cfg.fair_price_preopen_prices = read_size_t(values.at("fair_price_preopen_prices"), cfg.fair_price_preopen_prices);
    }
    if (has_key("fair_price_preopen_prices_min")) {
        cfg.fair_price_preopen_prices_min = read_size_t(values.at("fair_price_preopen_prices_min"), cfg.fair_price_preopen_prices_min);
    }
    if (has_key("fair_price_preopen_prices_max")) {
        cfg.fair_price_preopen_prices_max = read_size_t(values.at("fair_price_preopen_prices_max"), cfg.fair_price_preopen_prices_max);
    }
    if (has_key("risk_historical_price_stds")) {
        cfg.risk_historical_price_stds = read_size_t(values.at("risk_historical_price_stds"), cfg.risk_historical_price_stds);
    }
    if (has_key("risk_historical_price_stds_min")) {
        cfg.risk_historical_price_stds_min = read_size_t(values.at("risk_historical_price_stds_min"), cfg.risk_historical_price_stds_min);
    }
    if (has_key("risk_historical_price_stds_max")) {
        cfg.risk_historical_price_stds_max = read_size_t(values.at("risk_historical_price_stds_max"), cfg.risk_historical_price_stds_max);
    }
    if (has_key("risk_historical_spread_means")) {
        cfg.risk_historical_spread_means = read_size_t(values.at("risk_historical_spread_means"), cfg.risk_historical_spread_means);
    }
    if (has_key("risk_historical_spread_means_min")) {
        cfg.risk_historical_spread_means_min = read_size_t(values.at("risk_historical_spread_means_min"), cfg.risk_historical_spread_means_min);
    }
    if (has_key("risk_historical_spread_means_max")) {
        cfg.risk_historical_spread_means_max = read_size_t(values.at("risk_historical_spread_means_max"), cfg.risk_historical_spread_means_max);
    }
    if (has_key("risk_historical_spread_stds")) {
        cfg.risk_historical_spread_stds = read_size_t(values.at("risk_historical_spread_stds"), cfg.risk_historical_spread_stds);
    }
    if (has_key("risk_historical_spread_stds_min")) {
        cfg.risk_historical_spread_stds_min = read_size_t(values.at("risk_historical_spread_stds_min"), cfg.risk_historical_spread_stds_min);
    }
    if (has_key("risk_historical_spread_stds_max")) {
        cfg.risk_historical_spread_stds_max = read_size_t(values.at("risk_historical_spread_stds_max"), cfg.risk_historical_spread_stds_max);
    }
    if (has_key("risk_intraday_price_std_samples")) {
        cfg.risk_intraday_price_std_samples = read_size_t(values.at("risk_intraday_price_std_samples"), cfg.risk_intraday_price_std_samples);
    }
    if (has_key("risk_intraday_price_std_samples_min")) {
        cfg.risk_intraday_price_std_samples_min = read_size_t(values.at("risk_intraday_price_std_samples_min"), cfg.risk_intraday_price_std_samples_min);
    }
    if (has_key("risk_intraday_price_std_samples_max")) {
        cfg.risk_intraday_price_std_samples_max = read_size_t(values.at("risk_intraday_price_std_samples_max"), cfg.risk_intraday_price_std_samples_max);
    }
    if (has_key("risk_intraday_spread_mean_samples")) {
        cfg.risk_intraday_spread_mean_samples = read_size_t(values.at("risk_intraday_spread_mean_samples"), cfg.risk_intraday_spread_mean_samples);
    }
    if (has_key("risk_intraday_spread_mean_samples_min")) {
        cfg.risk_intraday_spread_mean_samples_min = read_size_t(values.at("risk_intraday_spread_mean_samples_min"), cfg.risk_intraday_spread_mean_samples_min);
    }
    if (has_key("risk_intraday_spread_mean_samples_max")) {
        cfg.risk_intraday_spread_mean_samples_max = read_size_t(values.at("risk_intraday_spread_mean_samples_max"), cfg.risk_intraday_spread_mean_samples_max);
    }
    if (has_key("risk_intraday_spread_std_samples")) {
        cfg.risk_intraday_spread_std_samples = read_size_t(values.at("risk_intraday_spread_std_samples"), cfg.risk_intraday_spread_std_samples);
    }
    if (has_key("risk_intraday_spread_std_samples_min")) {
        cfg.risk_intraday_spread_std_samples_min = read_size_t(values.at("risk_intraday_spread_std_samples_min"), cfg.risk_intraday_spread_std_samples_min);
    }
    if (has_key("risk_intraday_spread_std_samples_max")) {
        cfg.risk_intraday_spread_std_samples_max = read_size_t(values.at("risk_intraday_spread_std_samples_max"), cfg.risk_intraday_spread_std_samples_max);
    }
    if (has_key("tick_size")) {
        cfg.tick_size = read_double(values.at("tick_size"), cfg.tick_size);
    }
    if (has_key("fixed_fee_ticks")) {
        cfg.fixed_fee_ticks = read_double(values.at("fixed_fee_ticks"), cfg.fixed_fee_ticks);
    }
    if (has_key("x1_ticks")) {
        cfg.x1_ticks = read_double(values.at("x1_ticks"), cfg.x1_ticks);
    }
    if (has_key("x2_ticks")) {
        cfg.x2_ticks = read_double(values.at("x2_ticks"), cfg.x2_ticks);
    }
    if (has_key("fee_per_side")) {
        cfg.fee_per_side = read_double(values.at("fee_per_side"), cfg.fee_per_side);
    }
    if (has_key("quantity")) {
        cfg.quantity = read_double(values.at("quantity"), cfg.quantity);
    }
    if (has_key("liquidate_at_end")) {
        cfg.liquidate_at_end = read_bool(values.at("liquidate_at_end"), cfg.liquidate_at_end);
    }
    if (has_key("warmup_days")) {
        cfg.warmup_days = read_size_t(values.at("warmup_days"), cfg.warmup_days);
    }
    if (has_key("first_dry_run_day")) {
        cfg.first_dry_run_day = read_size_t(values.at("first_dry_run_day"), cfg.first_dry_run_day);
    }
    if (has_key("rolling_feedback_window")) {
        cfg.rolling_feedback_window = read_size_t(values.at("rolling_feedback_window"), cfg.rolling_feedback_window);
    }
    if (has_key("carry_position_forward")) {
        cfg.carry_position_forward = read_bool(values.at("carry_position_forward"), cfg.carry_position_forward);
    }

    return cfg;
}

} // namespace sto

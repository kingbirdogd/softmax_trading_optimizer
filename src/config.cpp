#include "config.hpp"

#include <cmath>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace sto {
namespace {

struct JsonValue {
    enum class Kind { Number, Bool, Object };

    Kind kind{};
    double number{};
    bool boolean{};
    std::unordered_map<std::string, std::unique_ptr<JsonValue>> object;
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    JsonValue parse_document() {
        skip_space();
        JsonValue root = parse_object();
        skip_space();
        if (pos_ != text_.size()) fail("unexpected trailing content");
        return root;
    }

private:
    std::string_view text_;
    std::size_t pos_{0};

    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error("Invalid JSON config at offset " + std::to_string(pos_) + ": " + message);
    }

    void skip_space() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\n' ||
                                       text_[pos_] == '\r' || text_[pos_] == '\t')) {
            ++pos_;
        }
    }

    void expect(char expected) {
        skip_space();
        if (pos_ == text_.size() || text_[pos_] != expected) {
            fail(std::string("expected '") + expected + "'");
        }
        ++pos_;
    }

    std::string parse_string() {
        expect('"');
        std::string result;
        while (pos_ < text_.size()) {
            const char ch = text_[pos_++];
            if (ch == '"') return result;
            if (ch != '\\') {
                result.push_back(ch);
                continue;
            }
            if (pos_ == text_.size()) fail("unfinished escape sequence");
            const char escaped = text_[pos_++];
            switch (escaped) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            default: fail("unsupported string escape");
            }
        }
        fail("unterminated string");
    }

    JsonValue parse_object() {
        JsonValue result;
        result.kind = JsonValue::Kind::Object;
        expect('{');
        skip_space();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return result;
        }
        while (true) {
            skip_space();
            if (pos_ == text_.size() || text_[pos_] != '"') fail("object key must be a string");
            const std::string key = parse_string();
            expect(':');
            auto value = std::make_unique<JsonValue>(parse_value());
            if (!result.object.emplace(key, std::move(value)).second) {
                fail("duplicate key: " + key);
            }
            skip_space();
            if (pos_ == text_.size()) fail("unterminated object");
            if (text_[pos_] == '}') {
                ++pos_;
                return result;
            }
            if (text_[pos_] != ',') fail("expected ',' or '}'");
            ++pos_;
        }
    }

    JsonValue parse_value() {
        skip_space();
        if (pos_ == text_.size()) fail("missing value");
        if (text_[pos_] == '{') return parse_object();
        if (text_.substr(pos_, 4) == "true") {
            pos_ += 4;
            return JsonValue{JsonValue::Kind::Bool, 0.0, true, {}};
        }
        if (text_.substr(pos_, 5) == "false") {
            pos_ += 5;
            return JsonValue{JsonValue::Kind::Bool, 0.0, false, {}};
        }
        if (text_[pos_] == '"' || text_[pos_] == '[' || text_.substr(pos_, 4) == "null") {
            fail("only numbers, booleans, and objects are supported as config values");
        }

        const std::string remaining(text_.substr(pos_));
        std::size_t consumed = 0;
        double value = 0.0;
        try {
            value = std::stod(remaining, &consumed);
        } catch (...) {
            fail("invalid number");
        }
        if (consumed == 0 || !std::isfinite(value)) fail("number must be finite");
        pos_ += consumed;
        return JsonValue{JsonValue::Kind::Number, value, false, {}};
    }
};

const JsonValue* find_value(const JsonValue& object, const std::string& key) {
    const auto it = object.object.find(key);
    return it == object.object.end() ? nullptr : it->second.get();
}

double number_value(const JsonValue& value, const std::string& name) {
    if (value.kind != JsonValue::Kind::Number) {
        throw std::runtime_error("Config field '" + name + "' must be a number");
    }
    return value.number;
}

bool bool_value(const JsonValue& value, const std::string& name) {
    if (value.kind != JsonValue::Kind::Bool) {
        throw std::runtime_error("Config field '" + name + "' must be a boolean");
    }
    return value.boolean;
}

std::size_t size_value(const JsonValue& value, const std::string& name) {
    const double number = number_value(value, name);
    if (number < 0.0 || std::floor(number) != number ||
        number > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("Config field '" + name + "' must be a non-negative integer");
    }
    return static_cast<std::size_t>(number);
}

void reject_unknown_keys(const JsonValue& object, const std::unordered_set<std::string>& allowed,
                         const std::string& object_name) {
    for (const auto& [key, unused] : object.object) {
        (void)unused;
        if (!allowed.contains(key)) {
            throw std::runtime_error("Unknown config field '" + object_name + key + "'");
        }
    }
}

template <typename T, typename Reader>
void assign_if_present(const JsonValue& object, const std::string& key, T& target, Reader reader) {
    if (const JsonValue* value = find_value(object, key)) {
        target = reader(*value, key);
    }
}

void validate_window(std::size_t value, std::size_t minimum, std::size_t maximum,
                     const std::string& name) {
    if (minimum == 0 || minimum > maximum || value < minimum || value > maximum) {
        throw std::runtime_error("Invalid configured window '" + name + "'");
    }
}

} // namespace

ModelConfig ModelConfig::default_config() {
    return ModelConfig{};
}

ModelConfig ModelConfig::load_from_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Unable to open config file: " + path);

    std::stringstream buffer;
    buffer << in.rdbuf();
    const JsonValue root = JsonParser(buffer.str()).parse_document();

    const std::unordered_set<std::string> top_level{
        "fair_price_historical_samples", "fair_price_historical_samples_min", "fair_price_historical_samples_max",
        "fair_price_preopen_prices", "fair_price_preopen_prices_min", "fair_price_preopen_prices_max",
        "risk_historical_price_stds", "risk_historical_price_stds_min", "risk_historical_price_stds_max",
        "risk_historical_spread_means", "risk_historical_spread_means_min", "risk_historical_spread_means_max",
        "risk_historical_spread_stds", "risk_historical_spread_stds_min", "risk_historical_spread_stds_max",
        "risk_intraday_price_std_samples", "risk_intraday_price_std_samples_min", "risk_intraday_price_std_samples_max",
        "risk_intraday_spread_mean_samples", "risk_intraday_spread_mean_samples_min", "risk_intraday_spread_mean_samples_max",
        "risk_intraday_spread_std_samples", "risk_intraday_spread_std_samples_min", "risk_intraday_spread_std_samples_max",
        "tick_size", "fixed_fee_ticks", "x1_ticks", "x2_ticks", "fee_per_side", "quantity",
        "liquidate_at_end", "warmup_days", "first_dry_run_day", "rolling_feedback_window",
        "carry_position_forward", "AdamConfig"
    };
    reject_unknown_keys(root, top_level, "");

    ModelConfig cfg = default_config();
    const auto size_reader = [](const JsonValue& value, const std::string& name) { return size_value(value, name); };
    const auto number_reader = [](const JsonValue& value, const std::string& name) { return number_value(value, name); };
    const auto bool_reader = [](const JsonValue& value, const std::string& name) { return bool_value(value, name); };

    assign_if_present(root, "fair_price_historical_samples", cfg.fair_price_historical_samples, size_reader);
    assign_if_present(root, "fair_price_historical_samples_min", cfg.fair_price_historical_samples_min, size_reader);
    assign_if_present(root, "fair_price_historical_samples_max", cfg.fair_price_historical_samples_max, size_reader);
    assign_if_present(root, "fair_price_preopen_prices", cfg.fair_price_preopen_prices, size_reader);
    assign_if_present(root, "fair_price_preopen_prices_min", cfg.fair_price_preopen_prices_min, size_reader);
    assign_if_present(root, "fair_price_preopen_prices_max", cfg.fair_price_preopen_prices_max, size_reader);
    assign_if_present(root, "risk_historical_price_stds", cfg.risk_historical_price_stds, size_reader);
    assign_if_present(root, "risk_historical_price_stds_min", cfg.risk_historical_price_stds_min, size_reader);
    assign_if_present(root, "risk_historical_price_stds_max", cfg.risk_historical_price_stds_max, size_reader);
    assign_if_present(root, "risk_historical_spread_means", cfg.risk_historical_spread_means, size_reader);
    assign_if_present(root, "risk_historical_spread_means_min", cfg.risk_historical_spread_means_min, size_reader);
    assign_if_present(root, "risk_historical_spread_means_max", cfg.risk_historical_spread_means_max, size_reader);
    assign_if_present(root, "risk_historical_spread_stds", cfg.risk_historical_spread_stds, size_reader);
    assign_if_present(root, "risk_historical_spread_stds_min", cfg.risk_historical_spread_stds_min, size_reader);
    assign_if_present(root, "risk_historical_spread_stds_max", cfg.risk_historical_spread_stds_max, size_reader);
    assign_if_present(root, "risk_intraday_price_std_samples", cfg.risk_intraday_price_std_samples, size_reader);
    assign_if_present(root, "risk_intraday_price_std_samples_min", cfg.risk_intraday_price_std_samples_min, size_reader);
    assign_if_present(root, "risk_intraday_price_std_samples_max", cfg.risk_intraday_price_std_samples_max, size_reader);
    assign_if_present(root, "risk_intraday_spread_mean_samples", cfg.risk_intraday_spread_mean_samples, size_reader);
    assign_if_present(root, "risk_intraday_spread_mean_samples_min", cfg.risk_intraday_spread_mean_samples_min, size_reader);
    assign_if_present(root, "risk_intraday_spread_mean_samples_max", cfg.risk_intraday_spread_mean_samples_max, size_reader);
    assign_if_present(root, "risk_intraday_spread_std_samples", cfg.risk_intraday_spread_std_samples, size_reader);
    assign_if_present(root, "risk_intraday_spread_std_samples_min", cfg.risk_intraday_spread_std_samples_min, size_reader);
    assign_if_present(root, "risk_intraday_spread_std_samples_max", cfg.risk_intraday_spread_std_samples_max, size_reader);
    assign_if_present(root, "tick_size", cfg.tick_size, number_reader);
    assign_if_present(root, "fixed_fee_ticks", cfg.fixed_fee_ticks, number_reader);
    assign_if_present(root, "x1_ticks", cfg.x1_ticks, number_reader);
    assign_if_present(root, "x2_ticks", cfg.x2_ticks, number_reader);
    assign_if_present(root, "fee_per_side", cfg.fee_per_side, number_reader);
    assign_if_present(root, "quantity", cfg.quantity, number_reader);
    assign_if_present(root, "liquidate_at_end", cfg.liquidate_at_end, bool_reader);
    assign_if_present(root, "warmup_days", cfg.warmup_days, size_reader);
    assign_if_present(root, "first_dry_run_day", cfg.first_dry_run_day, size_reader);
    assign_if_present(root, "rolling_feedback_window", cfg.rolling_feedback_window, size_reader);
    assign_if_present(root, "carry_position_forward", cfg.carry_position_forward, bool_reader);

    if (const JsonValue* adam = find_value(root, "AdamConfig")) {
        if (adam->kind != JsonValue::Kind::Object) {
            throw std::runtime_error("Config field 'AdamConfig' must be an object");
        }
        reject_unknown_keys(*adam, {"learning_rate", "beta1", "beta2", "epsilon", "l2", "steps"}, "AdamConfig.");
        assign_if_present(*adam, "learning_rate", cfg.adam.learning_rate, number_reader);
        assign_if_present(*adam, "beta1", cfg.adam.beta1, number_reader);
        assign_if_present(*adam, "beta2", cfg.adam.beta2, number_reader);
        assign_if_present(*adam, "epsilon", cfg.adam.epsilon, number_reader);
        assign_if_present(*adam, "l2", cfg.adam.l2, number_reader);
        assign_if_present(*adam, "steps", cfg.adam.steps, size_reader);
    }

    cfg.validate();
    return cfg;
}

void ModelConfig::validate() const {
    validate_window(fair_price_historical_samples, fair_price_historical_samples_min,
                    fair_price_historical_samples_max, "fair_price_historical_samples");
    validate_window(fair_price_preopen_prices, fair_price_preopen_prices_min,
                    fair_price_preopen_prices_max, "fair_price_preopen_prices");
    validate_window(risk_historical_price_stds, risk_historical_price_stds_min,
                    risk_historical_price_stds_max, "risk_historical_price_stds");
    validate_window(risk_historical_spread_means, risk_historical_spread_means_min,
                    risk_historical_spread_means_max, "risk_historical_spread_means");
    validate_window(risk_historical_spread_stds, risk_historical_spread_stds_min,
                    risk_historical_spread_stds_max, "risk_historical_spread_stds");
    validate_window(risk_intraday_price_std_samples, risk_intraday_price_std_samples_min,
                    risk_intraday_price_std_samples_max, "risk_intraday_price_std_samples");
    validate_window(risk_intraday_spread_mean_samples, risk_intraday_spread_mean_samples_min,
                    risk_intraday_spread_mean_samples_max, "risk_intraday_spread_mean_samples");
    validate_window(risk_intraday_spread_std_samples, risk_intraday_spread_std_samples_min,
                    risk_intraday_spread_std_samples_max, "risk_intraday_spread_std_samples");

    if (!std::isfinite(tick_size) || tick_size <= 0.0 ||
        !std::isfinite(fixed_fee_ticks) || fixed_fee_ticks < 0.0 ||
        !std::isfinite(x1_ticks) || x1_ticks < 0.0 ||
        !std::isfinite(x2_ticks) || x2_ticks < 0.0 ||
        !std::isfinite(fee_per_side) || fee_per_side < 0.0 ||
        !std::isfinite(quantity) || quantity <= 0.0) {
        throw std::runtime_error("Invalid non-window strategy configuration");
    }
    if (warmup_days == 0 || first_dry_run_day != warmup_days + 1 || rolling_feedback_window == 0) {
        throw std::runtime_error("Invalid walk-forward schedule configuration");
    }
    if (!std::isfinite(adam.learning_rate) || adam.learning_rate <= 0.0 ||
        !std::isfinite(adam.beta1) || adam.beta1 < 0.0 || adam.beta1 >= 1.0 ||
        !std::isfinite(adam.beta2) || adam.beta2 < 0.0 || adam.beta2 >= 1.0 ||
        !std::isfinite(adam.epsilon) || adam.epsilon <= 0.0 ||
        !std::isfinite(adam.l2) || adam.l2 < 0.0 || adam.steps == 0) {
        throw std::runtime_error("Invalid Adam configuration");
    }
}

} // namespace sto

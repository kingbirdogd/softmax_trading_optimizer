#pragma once
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace sto {
inline std::vector<double> softmax(const std::vector<double>& logits) {
    if (logits.empty()) throw std::invalid_argument("softmax: empty logits");
    const double m = *std::max_element(logits.begin(), logits.end());
    std::vector<double> w(logits.size());
    double sum = 0.0;
    for (std::size_t i=0; i<logits.size(); ++i) {
        w[i] = std::exp(logits[i] - m);
        sum += w[i];
    }
    for (double& x : w) x /= sum;
    return w;
}

inline double weighted_sum(const std::vector<double>& weights,
                           const std::vector<double>& features) {
    if (weights.size() != features.size())
        throw std::invalid_argument("weights/features size mismatch");
    return std::inner_product(weights.begin(), weights.end(),
                              features.begin(), 0.0);
}
}

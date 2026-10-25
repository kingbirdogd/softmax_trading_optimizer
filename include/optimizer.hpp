#pragma once
#include "strategy.hpp"
#include <functional>
#include <vector>

namespace sto {
// Simple deterministic coordinate/grid search for X1/X2.
// This is an executable baseline, not CMA-ES.
struct SearchResult {
    double x1{};
    double x2{};
    double objective{};
};
SearchResult search_x1_x2(const std::function<double(double,double)>& objective,
                          const std::vector<double>& candidates);
}

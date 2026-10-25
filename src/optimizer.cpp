#include "optimizer.hpp"
#include <limits>

namespace sto {
SearchResult search_x1_x2(const std::function<double(double,double)>& objective,
                          const std::vector<double>& c) {
    SearchResult best{0,0,-std::numeric_limits<double>::infinity()};
    for(double x1:c) for(double x2:c) {
        const double score=objective(x1,x2);
        if(score>best.objective) best={x1,x2,score};
    }
    return best;
}
}

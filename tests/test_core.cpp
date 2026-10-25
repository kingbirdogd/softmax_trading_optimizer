#include "models.hpp"
#include "optimizer.hpp"
#include "softmax.hpp"

#include <gtest/gtest.h>

TEST(SoftmaxTest, BasicSoftmaxIsSymmetric) {
    const auto w = sto::softmax({0.0, 0.0});
    EXPECT_NEAR(w[0], 0.5, 1e-12);
    EXPECT_NEAR(w[1], 0.5, 1e-12);
}

TEST(SoftmaxRegressorTest, PredictsWeightedAverageForZeroLogits) {
    sto::SoftmaxRegressor m(2);
    EXPECT_NEAR(m.predict({10.0, 20.0}), 15.0, 1e-12);
}

TEST(OptimizerTest, SearchFindsBestCandidate) {
    const auto best = sto::search_x1_x2(
        [](double a, double b) { return -(a - 2.0) * (a - 2.0) - (b - 3.0) * (b - 3.0); },
        {1.0, 2.0, 3.0, 4.0});

    EXPECT_DOUBLE_EQ(best.x1, 2.0);
    EXPECT_DOUBLE_EQ(best.x2, 3.0);
}

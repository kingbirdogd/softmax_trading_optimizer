add_test([=[SoftmaxTest.BasicSoftmaxIsSymmetric]=]  /Users/kingbirdogd/softmax_trading_optimizer/build/release/bin/unit_tests [==[--gtest_filter=SoftmaxTest.BasicSoftmaxIsSymmetric]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[SoftmaxTest.BasicSoftmaxIsSymmetric]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[/Users/kingbirdogd/softmax_trading_optimizer/tests/test_core.cpp:7]==]
    WORKING_DIRECTORY [==[/Users/kingbirdogd/softmax_trading_optimizer/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[SoftmaxRegressorTest.PredictsWeightedAverageForZeroLogits]=]  /Users/kingbirdogd/softmax_trading_optimizer/build/release/bin/unit_tests [==[--gtest_filter=SoftmaxRegressorTest.PredictsWeightedAverageForZeroLogits]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[SoftmaxRegressorTest.PredictsWeightedAverageForZeroLogits]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[/Users/kingbirdogd/softmax_trading_optimizer/tests/test_core.cpp:13]==]
    WORKING_DIRECTORY [==[/Users/kingbirdogd/softmax_trading_optimizer/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[OptimizerTest.SearchFindsBestCandidate]=]  /Users/kingbirdogd/softmax_trading_optimizer/build/release/bin/unit_tests [==[--gtest_filter=OptimizerTest.SearchFindsBestCandidate]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[OptimizerTest.SearchFindsBestCandidate]=]
  PROPERTIES
    
    DEF_SOURCE_LINE [==[/Users/kingbirdogd/softmax_trading_optimizer/tests/test_core.cpp:18]==]
    WORKING_DIRECTORY [==[/Users/kingbirdogd/softmax_trading_optimizer/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
set(unit_tests_TESTS [==[SoftmaxTest.BasicSoftmaxIsSymmetric]==] [==[SoftmaxRegressorTest.PredictsWeightedAverageForZeroLogits]==] [==[OptimizerTest.SearchFindsBestCandidate]==])

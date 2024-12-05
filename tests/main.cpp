#include "test_framework.hpp"
#include "transaction_tests.hpp"
#include "fuse_server_tests.hpp"
#include "nfs_server_tests.hpp"
#include <iostream>

using namespace fused::test;

int main(int argc, char* argv[]) {
    TestRunner runner;

    // Add test suites
    runner.add_test_suite(TransactionTests::create_suite());
    runner.add_test_suite(FuseServerTests::create_suite());
    runner.add_test_suite(NFSServerTests::create_suite());
    // Add more test suites here as they are implemented

    // Run tests
    bool success = runner.run_all_tests();

    // Print results
    auto results = runner.get_results();
    size_t total = results.size();
    size_t passed = std::count_if(results.begin(), results.end(),
                                 [](const TestResult& r) { return r.passed; });

    std::cout << "\nTest Results: " << passed << "/" << total 
              << " tests passed" << std::endl;

    return success ? 0 : 1;
} 
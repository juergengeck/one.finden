#include "test_framework.hpp"
#include <algorithm>
#include <iostream>
#include <iomanip>

namespace fused {
namespace test {

void TestRunner::add_test_suite(const TestSuite& suite) {
    test_suites_.push_back(suite);
    LOG_INFO("Added test suite: {}", suite.name);
}

void TestRunner::add_test_case(const std::string& suite_name, const TestCase& test) {
    auto it = std::find_if(test_suites_.begin(), test_suites_.end(),
                          [&suite_name](const TestSuite& suite) {
                              return suite.name == suite_name;
                          });
    
    if (it != test_suites_.end()) {
        it->test_cases.push_back(test);
        LOG_INFO("Added test case {} to suite {}", test.name, suite_name);
    } else {
        LOG_ERROR("Test suite not found: {}", suite_name);
    }
}

bool TestRunner::run_all_tests() {
    results_.clear();
    bool all_passed = true;
    
    for (const auto& suite : test_suites_) {
        if (!run_test_suite(suite.name)) {
            all_passed = false;
            if (std::any_of(suite.test_cases.begin(), suite.test_cases.end(),
                           [](const TestCase& test) { return test.required; })) {
                LOG_ERROR("Required tests failed in suite: {}", suite.name);
                return false;
            }
        }
    }

    print_summary();
    return all_passed;
}

bool TestRunner::run_test_suite(const std::string& suite_name) {
    auto it = std::find_if(test_suites_.begin(), test_suites_.end(),
                          [&suite_name](const TestSuite& suite) {
                              return suite.name == suite_name;
                          });
    
    if (it == test_suites_.end()) {
        LOG_ERROR("Test suite not found: {}", suite_name);
        return false;
    }

    LOG_INFO("Running test suite: {}", suite_name);
    bool suite_passed = true;

    // Run suite setup
    if (it->setup && !it->setup()) {
        LOG_ERROR("Suite setup failed: {}", suite_name);
        return false;
    }

    // Run test cases
    for (const auto& test : it->test_cases) {
        if (!run_test_case(test, suite_name)) {
            suite_passed = false;
            if (test.required) {
                LOG_ERROR("Required test failed: {}", test.name);
                break;
            }
        }
    }

    // Run suite teardown
    if (it->teardown && !it->teardown()) {
        LOG_ERROR("Suite teardown failed: {}", suite_name);
        return false;
    }

    return suite_passed;
}

bool TestRunner::run_single_test(const std::string& suite_name, 
                               const std::string& test_name) {
    auto suite_it = std::find_if(test_suites_.begin(), test_suites_.end(),
                                [&suite_name](const TestSuite& suite) {
                                    return suite.name == suite_name;
                                });
    
    if (suite_it == test_suites_.end()) {
        LOG_ERROR("Test suite not found: {}", suite_name);
        return false;
    }

    auto test_it = std::find_if(suite_it->test_cases.begin(), 
                               suite_it->test_cases.end(),
                               [&test_name](const TestCase& test) {
                                   return test.name == test_name;
                               });
    
    if (test_it == suite_it->test_cases.end()) {
        LOG_ERROR("Test case not found: {}", test_name);
        return false;
    }

    // Run suite setup
    if (suite_it->setup && !suite_it->setup()) {
        LOG_ERROR("Suite setup failed: {}", suite_name);
        return false;
    }

    bool result = run_test_case(*test_it, suite_name);

    // Run suite teardown
    if (suite_it->teardown && !suite_it->teardown()) {
        LOG_ERROR("Suite teardown failed: {}", suite_name);
        return false;
    }

    return result;
}

std::vector<TestRunner::TestResult> TestRunner::get_results() const {
    return results_;
}

bool TestRunner::run_test_case(const TestCase& test, const std::string& suite_name) {
    LOG_INFO("Running test: {}.{}", suite_name, test.name);
    
    TestResult result;
    result.suite_name = suite_name;
    result.test_name = test.name;

    auto start = std::chrono::steady_clock::now();

    try {
        // Run test with timeout
        auto future = std::async(std::launch::async, test.test_func);
        auto status = future.wait_for(test.timeout);

        if (status == std::future_status::timeout) {
            result.error_message = "Test timed out";
            result.passed = false;
        } else {
            result.passed = future.get();
        }
    } catch (const std::exception& e) {
        result.error_message = e.what();
        result.passed = false;
    } catch (...) {
        result.error_message = "Unknown error";
        result.passed = false;
    }

    auto end = std::chrono::steady_clock::now();
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    log_result(result);
    results_.push_back(result);

    return result.passed;
}

void TestRunner::log_result(const TestResult& result) {
    if (result.passed) {
        LOG_INFO("Test passed: {}.{} ({}ms)", 
                result.suite_name, result.test_name, result.duration.count());
    } else {
        LOG_ERROR("Test failed: {}.{} ({}ms) - {}", 
                 result.suite_name, result.test_name, 
                 result.duration.count(), result.error_message);
    }
}

void TestRunner::print_summary() {
    size_t total = results_.size();
    size_t passed = std::count_if(results_.begin(), results_.end(),
                                 [](const TestResult& r) { return r.passed; });
    size_t failed = total - passed;

    std::cout << "\nTest Summary\n";
    std::cout << "============\n";
    std::cout << "Total:  " << total << "\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";

    if (failed > 0) {
        std::cout << "\nFailed Tests:\n";
        for (const auto& result : results_) {
            if (!result.passed) {
                std::cout << result.suite_name << "." << result.test_name 
                         << " - " << result.error_message << "\n";
            }
        }
    }
}

} // namespace test
} // namespace fused 
#pragma once
#include <string>
#include <functional>
#include <vector>
#include <chrono>
#include "util/logger.hpp"

namespace fused {
namespace test {

struct TestCase {
    std::string name;
    std::string description;
    std::function<bool()> test_func;
    bool required{true};
    std::chrono::milliseconds timeout{5000};  // 5 seconds default
};

struct TestSuite {
    std::string name;
    std::vector<TestCase> test_cases;
    std::function<bool()> setup;
    std::function<bool()> teardown;
};

class TestRunner {
public:
    // Test registration
    void add_test_suite(const TestSuite& suite);
    void add_test_case(const std::string& suite_name, const TestCase& test);

    // Test execution
    bool run_all_tests();
    bool run_test_suite(const std::string& suite_name);
    bool run_single_test(const std::string& suite_name, const std::string& test_name);

    // Test results
    struct TestResult {
        std::string suite_name;
        std::string test_name;
        bool passed{false};
        std::string error_message;
        std::chrono::milliseconds duration{0};
    };
    std::vector<TestResult> get_results() const;

private:
    std::vector<TestSuite> test_suites_;
    std::vector<TestResult> results_;

    bool run_test_case(const TestCase& test, const std::string& suite_name);
    void log_result(const TestResult& result);
};

// Test assertion macros
#define TEST_ASSERT(condition) \
    if (!(condition)) { \
        LOG_ERROR("Assertion failed: {}", #condition); \
        return false; \
    }

#define TEST_ASSERT_EQ(expected, actual) \
    if ((expected) != (actual)) { \
        LOG_ERROR("Expected {} but got {}", expected, actual); \
        return false; \
    }

#define TEST_ASSERT_THROWS(expr, exception_type) \
    try { \
        expr; \
        LOG_ERROR("Expected exception {} not thrown", #exception_type); \
        return false; \
    } catch (const exception_type&) { \
    } catch (...) { \
        LOG_ERROR("Wrong exception type caught"); \
        return false; \
    }

} // namespace test
} // namespace fused 
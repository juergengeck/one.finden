#include "test_framework.hpp"
#include "transaction/transaction_log.hpp"
#include <memory>

namespace fused {
namespace test {

class TransactionTests {
public:
    static TestSuite create_suite() {
        TestSuite suite;
        suite.name = "Transaction";
        suite.setup = []() { return setup_suite(); };
        suite.teardown = []() { return teardown_suite(); };

        // Basic transaction tests
        suite.test_cases.push_back({"basic_transaction",
            "Test basic transaction operations",
            []() { return test_basic_transaction(); }
        });

        // Atomic operations
        suite.test_cases.push_back({"atomic_operations",
            "Test atomic operation guarantees",
            []() { return test_atomic_operations(); }
        });

        // Rollback tests
        suite.test_cases.push_back({"rollback",
            "Test transaction rollback",
            []() { return test_rollback(); }
        });

        // Recovery tests
        suite.test_cases.push_back({"recovery",
            "Test transaction recovery",
            []() { return test_recovery(); }
        });

        return suite;
    }

private:
    static bool setup_suite() {
        // Setup test environment
        return true;
    }

    static bool teardown_suite() {
        // Cleanup test environment
        return true;
    }

    static bool test_basic_transaction() {
        TransactionLog log;
        TEST_ASSERT(log.initialize());

        // Test begin transaction
        TEST_ASSERT(log.begin_transaction());

        // Create test operation
        Operation op{
            1,                      // operation_id
            NFSProcedure::CREATE,   // procedure
            {1, 2, 3},             // arguments
            {4, 5, 6},             // pre_state
            std::time(nullptr),     // timestamp
            0                       // flags
        };

        // Test log operation
        TEST_ASSERT(log.log_operation(op));

        // Test commit
        TEST_ASSERT(log.commit_transaction());

        return true;
    }

    static bool test_atomic_operations() {
        TransactionLog log;
        TEST_ASSERT(log.initialize());

        // Test atomic write
        TEST_ASSERT(log.begin_transaction());
        
        Operation write_op{
            1,
            NFSProcedure::WRITE,
            {1, 2, 3},
            {4, 5, 6},
            std::time(nullptr),
            0
        };

        TEST_ASSERT(log.log_operation(write_op));
        TEST_ASSERT(log.commit_transaction());

        // Verify operation was atomic
        TEST_ASSERT(log.verify_operation(1));

        return true;
    }

    static bool test_rollback() {
        TransactionLog log;
        TEST_ASSERT(log.initialize());

        // Start transaction
        TEST_ASSERT(log.begin_transaction());

        // Log multiple operations
        Operation op1{1, NFSProcedure::CREATE, {}, {}, std::time(nullptr), 0};
        Operation op2{2, NFSProcedure::WRITE, {}, {}, std::time(nullptr), 0};

        TEST_ASSERT(log.log_operation(op1));
        TEST_ASSERT(log.log_operation(op2));

        // Test rollback
        TEST_ASSERT(log.rollback_transaction());

        // Verify operations were rolled back
        TEST_ASSERT(!log.verify_operation(1));
        TEST_ASSERT(!log.verify_operation(2));

        return true;
    }

    static bool test_recovery() {
        TransactionLog log;
        TEST_ASSERT(log.initialize());

        // Create and commit transaction
        TEST_ASSERT(log.begin_transaction());
        Operation op{1, NFSProcedure::CREATE, {}, {}, std::time(nullptr), 0};
        TEST_ASSERT(log.log_operation(op));
        TEST_ASSERT(log.commit_transaction());

        // Simulate crash by creating new log instance
        TransactionLog recovered_log;
        TEST_ASSERT(recovered_log.initialize());

        // Verify operation can be recovered
        std::vector<uint64_t> ops_to_recover{1};
        TEST_ASSERT(recovered_log.replay_operations(ops_to_recover));
        TEST_ASSERT(recovered_log.verify_operation(1));

        return true;
    }
};

} // namespace test
} // namespace fused 
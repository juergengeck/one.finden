#include "test_framework.hpp"
#include "nfs_server.hpp"
#include <filesystem>
#include <fstream>

namespace fused {
namespace test {

class NFSServerTests {
public:
    static TestSuite create_suite() {
        TestSuite suite;
        suite.name = "NFSServer";
        suite.setup = []() { return setup_suite(); };
        suite.teardown = []() { return teardown_suite(); };

        // Basic operations
        suite.test_cases.push_back({"server_initialization",
            "Test NFS server initialization",
            []() { return test_initialization(); }
        });

        // File operations
        suite.test_cases.push_back({"file_operations",
            "Test basic file operations",
            []() { return test_file_operations(); }
        });

        // Directory operations
        suite.test_cases.push_back({"directory_operations",
            "Test directory operations",
            []() { return test_directory_operations(); }
        });

        // Lock operations
        suite.test_cases.push_back({"lock_operations",
            "Test file locking operations",
            []() { return test_lock_operations(); }
        });

        // Error handling
        suite.test_cases.push_back({"error_handling",
            "Test error handling",
            []() { return test_error_handling(); }
        });

        // Metrics
        suite.test_cases.push_back({"metrics",
            "Test metrics collection",
            []() { return test_metrics(); }
        });

        return suite;
    }

private:
    static std::string test_root_dir;
    static std::unique_ptr<NFSServer> server;

    static bool setup_suite() {
        test_root_dir = std::string(getenv("FUSED_TEST_DIR")) + "/nfs";
        std::filesystem::create_directories(test_root_dir);
        server = std::make_unique<NFSServer>();
        return true;
    }

    static bool teardown_suite() {
        server.reset();
        std::filesystem::remove_all(test_root_dir);
        return true;
    }

    static bool test_initialization() {
        TEST_ASSERT(server->initialize());
        
        // Test invalid initialization
        NFSServer invalid_server;
        invalid_server.set_root_path("/nonexistent");
        TEST_ASSERT(!invalid_server.initialize());

        return true;
    }

    static bool test_file_operations() {
        // Create file
        std::string test_file = test_root_dir + "/test.txt";
        NFSFileHandle handle;
        TEST_ASSERT(server->create_file(test_file, 0644, handle));

        // Write to file
        std::vector<uint8_t> write_data = {'t', 'e', 's', 't'};
        TEST_ASSERT(server->write_file(handle, 0, write_data));

        // Read from file
        std::vector<uint8_t> read_data;
        TEST_ASSERT(server->read_file(handle, 0, write_data.size(), read_data));
        TEST_ASSERT(read_data == write_data);

        // Get attributes
        NFSFattr4 attrs;
        TEST_ASSERT(server->get_attributes(handle, attrs));
        TEST_ASSERT(attrs.size == write_data.size());

        // Remove file
        TEST_ASSERT(server->remove_file(test_file));

        return true;
    }

    static bool test_directory_operations() {
        // Create directory
        std::string test_dir = test_root_dir + "/testdir";
        NFSFileHandle dir_handle;
        TEST_ASSERT(server->create_directory(test_dir, 0755, dir_handle));

        // Create file in directory
        std::string test_file = test_dir + "/file.txt";
        NFSFileHandle file_handle;
        TEST_ASSERT(server->create_file(test_file, 0644, file_handle));

        // Read directory
        std::vector<std::string> entries;
        TEST_ASSERT(server->read_directory(dir_handle, entries));
        TEST_ASSERT(entries.size() == 1);
        TEST_ASSERT(entries[0] == "file.txt");

        // Remove file and directory
        TEST_ASSERT(server->remove_file(test_file));
        TEST_ASSERT(server->remove_directory(test_dir));

        return true;
    }

    static bool test_lock_operations() {
        // Create test file
        std::string test_file = test_root_dir + "/locktest.txt";
        NFSFileHandle handle;
        TEST_ASSERT(server->create_file(test_file, 0644, handle));

        // Test write lock
        TEST_ASSERT(server->lock_file(handle, 0, 1024, true));  // write lock
        
        // Test lock conflict
        TEST_ASSERT(!server->lock_file(handle, 0, 1024, false)); // read lock should fail
        
        // Test unlock
        TEST_ASSERT(server->unlock_file(handle, 0, 1024));
        
        // Test read lock after unlock
        TEST_ASSERT(server->lock_file(handle, 0, 1024, false));  // read lock
        
        // Cleanup
        TEST_ASSERT(server->unlock_file(handle, 0, 1024));
        TEST_ASSERT(server->remove_file(test_file));

        return true;
    }

    static bool test_error_handling() {
        // Test nonexistent file
        NFSFileHandle handle;
        TEST_ASSERT(!server->read_file(handle, 0, 1024, std::vector<uint8_t>()));

        // Test invalid handle
        NFSFattr4 attrs;
        TEST_ASSERT(!server->get_attributes(handle, attrs));

        // Test permission denied
        std::string root_file = "/root/test.txt";
        TEST_ASSERT(!server->create_file(root_file, 0644, handle));

        return true;
    }

    static bool test_metrics() {
        // Get initial metrics
        LockStats initial_stats;
        server->get_lock_stats(initial_stats);

        // Perform some operations
        std::string test_file = test_root_dir + "/metrics.txt";
        NFSFileHandle handle;
        server->create_file(test_file, 0644, handle);
        server->lock_file(handle, 0, 1024, true);
        server->unlock_file(handle, 0, 1024);
        server->remove_file(test_file);

        // Check metrics were updated
        LockStats current_stats;
        server->get_lock_stats(current_stats);
        TEST_ASSERT(current_stats.lock_attempts > initial_stats.lock_attempts);
        TEST_ASSERT(current_stats.lock_successes > initial_stats.lock_successes);

        return true;
    }
};

std::string NFSServerTests::test_root_dir;
std::unique_ptr<NFSServer> NFSServerTests::server;

} // namespace test
} // namespace fused 
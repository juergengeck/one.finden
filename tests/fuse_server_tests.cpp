#include "test_framework.hpp"
#include "fuse_server.hpp"
#include <filesystem>

namespace fused {
namespace test {

class FuseServerTests {
public:
    static TestSuite create_suite() {
        TestSuite suite;
        suite.name = "FuseServer";
        suite.setup = []() { return setup_suite(); };
        suite.teardown = []() { return teardown_suite(); };

        // Basic server tests
        suite.test_cases.push_back({"initialization",
            "Test server initialization",
            []() { return test_initialization(); }
        });

        suite.test_cases.push_back({"mount_point",
            "Test mount point validation",
            []() { return test_mount_point(); }
        });

        suite.test_cases.push_back({"start_stop",
            "Test server start/stop",
            []() { return test_start_stop(); }
        });

        return suite;
    }

private:
    static std::string test_mount_dir;

    static bool setup_suite() {
        test_mount_dir = std::string(getenv("FUSED_TEST_DIR")) + "/mount";
        std::filesystem::create_directories(test_mount_dir);
        return true;
    }

    static bool teardown_suite() {
        std::filesystem::remove_all(test_mount_dir);
        return true;
    }

    static bool test_initialization() {
        FuseServer server;
        TEST_ASSERT(server.initialize(test_mount_dir));
        return true;
    }

    static bool test_mount_point() {
        FuseServer server;

        // Test invalid mount point
        TEST_ASSERT(!server.initialize("/nonexistent/path"));

        // Test valid mount point
        TEST_ASSERT(server.initialize(test_mount_dir));

        // Test already mounted
        FuseServer server2;
        TEST_ASSERT(!server2.initialize(test_mount_dir));

        return true;
    }

    static bool test_start_stop() {
        FuseServer server;
        TEST_ASSERT(server.initialize(test_mount_dir));

        // Test start
        TEST_ASSERT(server.start());

        // Test double start
        TEST_ASSERT(server.start());  // Should succeed but do nothing

        // Test stop
        server.stop();

        // Test restart
        TEST_ASSERT(server.start());

        return true;
    }
};

std::string FuseServerTests::test_mount_dir;

} // namespace test
} // namespace fused 
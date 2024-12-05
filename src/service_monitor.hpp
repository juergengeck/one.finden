#pragma once
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include "util/logger.hpp"

namespace fused {

struct ServiceStatus {
    bool running{false};
    bool healthy{false};
    std::chrono::steady_clock::time_point last_check;
    uint64_t active_connections{0};
    uint64_t total_operations{0};
    std::string error_message;
};

class ServiceMonitor {
public:
    ServiceMonitor();
    ~ServiceMonitor();

    // Start monitoring
    bool start();

    // Stop monitoring
    void stop();

    // Get current status
    ServiceStatus get_status() const;

    // Check if service is healthy
    bool is_healthy() const;

    // Write status to file
    bool write_status_file() const;

private:
    std::atomic<bool> running_{false};
    std::thread monitor_thread_;
    ServiceStatus current_status_;
    mutable std::mutex status_mutex_;

    static constexpr auto CHECK_INTERVAL = std::chrono::seconds(30);
    static constexpr auto STATUS_FILE = "/var/run/fused-nfs.status";

    // Monitor thread function
    void monitor_loop();

    // Check service health
    bool check_service_health();

    // Check individual components
    bool check_network_health();
    bool check_filesystem_health();
    bool check_resource_usage();
    bool check_client_connections();
};

} // namespace fused 
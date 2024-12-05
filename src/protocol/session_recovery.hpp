#pragma once
#include <chrono>
#include <unordered_map>
#include <mutex>
#include "session.hpp"
#include "util/logger.hpp"

namespace fused {

struct RecoveryState {
    uint32_t session_id;
    std::chrono::steady_clock::time_point start_time;
    std::vector<uint8_t> last_sequence_id;
    std::vector<uint32_t> recovered_operations;
    uint32_t recovery_attempts{0};
    uint32_t recovered_count{0};
    bool completed{false};
};

struct RecoveryMetrics {
    std::atomic<uint64_t> total_recoveries{0};
    std::atomic<uint64_t> successful_recoveries{0};
    std::atomic<uint64_t> failed_recoveries{0};
    std::atomic<uint64_t> expired_recoveries{0};
    std::atomic<uint64_t> total_recovery_time_ms{0};
    std::atomic<uint64_t> operations_recovered{0};
};

class SessionRecoveryManager {
public:
    // Initialize recovery manager
    bool initialize();

    // Start session recovery
    bool start_recovery(const std::string& client_id, uint32_t session_id);

    // Complete session recovery
    bool complete_recovery(const std::string& client_id);

    // Check if client is in recovery
    bool is_in_recovery(const std::string& client_id) const;

    // Get recovery state
    const RecoveryState* get_recovery_state(const std::string& client_id) const;

    // Cleanup expired recovery states
    void cleanup_expired_states();

    // Add recovery tracking methods
    void record_operation(uint32_t session_id, uint32_t operation_id);
    bool was_operation_recovered(uint32_t session_id, uint32_t operation_id) const;
    
    // Add metrics methods
    const RecoveryMetrics& get_metrics() const { return metrics_; }
    void log_metrics() const;

private:
    std::mutex mutex_;
    std::unordered_map<std::string, RecoveryState> recovery_states_;
    static constexpr auto RECOVERY_TIMEOUT = std::chrono::minutes(5);
    RecoveryMetrics metrics_;
};

} // namespace fuse_t 
#pragma once
#include <unordered_map>
#include <vector>
#include <mutex>
#include <chrono>
#include "nfs_protocol.hpp"
#include "util/logger.hpp"

namespace fused {

struct LoggedOperation {
    uint32_t operation_id;
    NFSProcedure procedure;
    std::vector<uint8_t> arguments;
    std::chrono::steady_clock::time_point timestamp;
    bool completed{false};
    NFSStatus result{NFSStatus::OK};
    std::vector<uint32_t> dependencies;
    std::string target_path;
    bool replay_attempted{false};
    bool replay_succeeded{false};
    std::string replay_error;
};

class OperationLog {
public:
    // Add operation to log
    uint32_t log_operation(NFSProcedure proc, const std::vector<uint8_t>& args);

    // Mark operation as completed
    void complete_operation(uint32_t op_id, NFSStatus result);

    // Get operations for recovery
    std::vector<LoggedOperation> get_incomplete_operations(
        const std::string& client_id,
        uint32_t session_id) const;

    // Check if operation needs replay
    bool needs_replay(uint32_t op_id) const;

    // Replay operation
    NFSStatus replay_operation(const LoggedOperation& op);

    // Dependency tracking
    void add_dependency(uint32_t op_id, uint32_t depends_on);
    bool check_dependencies(uint32_t op_id) const;
    std::vector<uint32_t> get_dependencies(uint32_t op_id) const;

    // Path tracking
    void set_operation_path(uint32_t op_id, const std::string& path);
    bool has_path_conflict(uint32_t op1, uint32_t op2) const;

    // Replay verification
    bool verify_replay_order(const std::vector<LoggedOperation>& ops) const;
    bool verify_replay_result(uint32_t op_id, NFSStatus result);
    std::vector<uint32_t> get_failed_replays() const;
    void record_replay_attempt(uint32_t op_id, bool success, const std::string& error = "");

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, LoggedOperation> operations_;
    uint32_t next_op_id_{1};

    // Helper to decode and replay specific operations
    NFSStatus replay_write(const std::vector<uint8_t>& args);
    NFSStatus replay_create(const std::vector<uint8_t>& args);
    NFSStatus replay_remove(const std::vector<uint8_t>& args);
    // Add more replay handlers as needed

    // Helper methods for dependency analysis
    bool is_dependent_operation(const LoggedOperation& op1, 
                              const LoggedOperation& op2) const;
    bool can_run_concurrently(const LoggedOperation& op1,
                             const LoggedOperation& op2) const;
    std::vector<uint32_t> get_conflicting_operations(uint32_t op_id) const;
};

// Global operation log instance
inline OperationLog& get_operation_log() {
    static OperationLog log;
    return log;
}

} // namespace fuse_t 
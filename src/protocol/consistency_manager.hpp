#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include "nfs_protocol.hpp"
#include "operation_journal.hpp"
#include "util/logger.hpp"

namespace fused {

enum class ConsistencyLevel {
    STRICT,      // Immediate consistency, synchronous operations
    SEQUENTIAL,  // Sequential consistency, ordered operations
    EVENTUAL,    // Eventual consistency, asynchronous operations
    RELAXED      // Relaxed consistency, best effort
};

struct ConsistencyGuarantee {
    ConsistencyLevel level;
    std::chrono::milliseconds timeout;
    bool require_fsync{false};
    bool require_verification{false};
};

struct ConsistencyPoint {
    uint64_t sequence_id;
    std::chrono::steady_clock::time_point timestamp;
    ConsistencyLevel level;
    bool verified{false};
    bool synced{false};
};

class ConsistencyManager {
public:
    ConsistencyManager(OperationJournal& journal);
    ~ConsistencyManager();

    // Initialize consistency manager
    bool initialize();

    // Consistency operations
    bool set_consistency_level(const std::string& path, ConsistencyLevel level);
    bool enforce_consistency(const std::string& path, const ConsistencyGuarantee& guarantee);
    bool verify_consistency(const std::string& path);

    // Consistency points
    uint64_t create_consistency_point(const std::string& path);
    bool verify_consistency_point(uint64_t point_id);
    bool wait_for_consistency(uint64_t point_id, std::chrono::milliseconds timeout);

    // State verification
    bool verify_file_state(const std::string& path);
    bool verify_directory_state(const std::string& path);
    bool verify_handle_state(const NFSFileHandle& handle);

private:
    OperationJournal& journal_;
    std::mutex mutex_;
    std::unordered_map<std::string, ConsistencyLevel> consistency_levels_;
    std::unordered_map<uint64_t, ConsistencyPoint> consistency_points_;
    std::atomic<uint64_t> next_point_id_{1};
    std::thread verification_thread_;
    std::atomic<bool> running_{true};

    static constexpr auto VERIFICATION_INTERVAL = std::chrono::seconds(1);
    static constexpr auto DEFAULT_TIMEOUT = std::chrono::seconds(5);

    // Consistency verification
    bool verify_operation_order(const std::string& path);
    bool verify_data_consistency(const std::string& path);
    bool verify_metadata_consistency(const std::string& path);

    // State management
    bool sync_file_state(const std::string& path);
    bool sync_directory_state(const std::string& path);
    bool repair_inconsistency(const std::string& path);

    // Verification thread
    void run_verification_loop();
    void verify_pending_points();
    void cleanup_expired_points();

    // Helper methods
    bool is_strict_consistency_required(const std::string& path) const;
    bool wait_for_sync(const std::string& path, std::chrono::milliseconds timeout);
    void log_consistency_violation(const std::string& path, const std::string& reason);
};

} // namespace fuse_t 
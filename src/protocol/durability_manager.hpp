#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include "nfs_protocol.hpp"
#include "transaction_log.hpp"
#include "operation_journal.hpp"
#include "util/logger.hpp"

namespace fused {

enum class SyncPoint {
    IMMEDIATE,    // Sync immediately after operation
    TRANSACTION,  // Sync at transaction boundaries
    PERIODIC,     // Sync at regular intervals
    LAZY         // Sync only when necessary
};

struct DurabilityGuarantee {
    SyncPoint sync_point;
    bool require_fsync{false};
    bool require_journal{true};
    bool require_verification{false};
    std::chrono::milliseconds timeout{5000};  // 5 seconds default
};

class DurabilityManager {
public:
    DurabilityManager(TransactionLog& txn_log, OperationJournal& journal);
    ~DurabilityManager();

    // Initialize manager
    bool initialize();

    // Durability control
    bool enforce_durability(const std::string& path, const DurabilityGuarantee& guarantee);
    bool wait_for_durability(const std::string& path, std::chrono::milliseconds timeout);
    bool verify_durability(const std::string& path);

    // Sync points
    bool create_sync_point(const std::string& path);
    bool wait_for_sync(uint64_t sync_id);
    bool verify_sync_point(uint64_t sync_id);

    // Write ordering
    bool order_write(const std::string& path, uint64_t sequence_id);
    bool verify_write_order(const std::string& path);
    bool wait_for_ordered_writes(const std::string& path);

    // Fsync handling
    bool fsync_path(const std::string& path);
    bool fsync_directory(const std::string& path);
    bool fsync_metadata(const std::string& path);

private:
    TransactionLog& txn_log_;
    OperationJournal& journal_;
    std::mutex mutex_;
    std::thread sync_thread_;
    std::atomic<bool> running_{true};
    std::atomic<uint64_t> next_sync_id_{1};

    static constexpr auto SYNC_INTERVAL = std::chrono::seconds(1);
    static constexpr auto MAX_SYNC_ATTEMPTS = 3;

    // Sync point tracking
    struct SyncState {
        uint64_t sync_id;
        std::string path;
        std::chrono::steady_clock::time_point timestamp;
        bool synced{false};
        bool verified{false};
        uint32_t attempts{0};
    };
    std::unordered_map<uint64_t, SyncState> sync_points_;

    // Write ordering
    struct WriteOrder {
        uint64_t sequence_id;
        std::chrono::steady_clock::time_point timestamp;
        bool completed{false};
    };
    std::unordered_map<std::string, std::vector<WriteOrder>> write_orders_;

    // Sync thread
    void run_sync_loop();
    void process_sync_points();
    void cleanup_expired_points();

    // Helper methods
    bool sync_file_state(const std::string& path);
    bool verify_file_state(const std::string& path);
    bool handle_sync_failure(const std::string& path, const std::string& error);
    bool retry_sync(uint64_t sync_id);
};

} // namespace fuse_t 
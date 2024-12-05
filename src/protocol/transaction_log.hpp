#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include "nfs_protocol.hpp"
#include "util/logger.hpp"

namespace fused {

struct TransactionEntry {
    uint64_t transaction_id;
    NFSProcedure procedure;
    std::vector<uint8_t> arguments;
    std::vector<uint8_t> pre_state;  // State before operation
    std::chrono::steady_clock::time_point timestamp;
    bool committed{false};
    bool synced{false};
};

class TransactionLog {
public:
    explicit TransactionLog(const std::string& log_path);
    ~TransactionLog();

    // Transaction operations
    uint64_t begin_transaction(NFSProcedure proc, 
                             const std::vector<uint8_t>& args);
    bool commit_transaction(uint64_t txn_id);
    bool rollback_transaction(uint64_t txn_id);
    
    // State management
    bool save_pre_state(uint64_t txn_id, const std::vector<uint8_t>& state);
    bool sync_to_disk();
    
    // Recovery
    bool recover_from_log();
    std::vector<TransactionEntry> get_uncommitted_transactions() const;

private:
    const std::string log_path_;
    std::mutex mutex_;
    std::atomic<uint64_t> next_txn_id_{1};
    int log_fd_{-1};
    
    // In-memory transaction tracking
    std::unordered_map<uint64_t, TransactionEntry> active_transactions_;
    
    // Helper methods
    bool write_entry(const TransactionEntry& entry);
    bool read_entry(TransactionEntry& entry);
    bool truncate_log();
    
    // Recovery helpers
    bool replay_transaction(const TransactionEntry& entry);
    bool verify_transaction(const TransactionEntry& entry);
};

} // namespace fuse_t 
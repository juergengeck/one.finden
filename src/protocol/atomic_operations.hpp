#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include "nfs_protocol.hpp"
#include "transaction_log.hpp"
#include "operation_journal.hpp"
#include "util/logger.hpp"

namespace fused {

struct AtomicOperation {
    uint64_t operation_id;
    NFSProcedure procedure;
    std::vector<uint8_t> arguments;
    std::string target_path;
    std::vector<uint8_t> pre_state;
    bool completed{false};
    bool rolled_back{false};
    std::chrono::steady_clock::time_point timestamp;
};

class AtomicOperationHandler {
public:
    AtomicOperationHandler(TransactionLog& txn_log, OperationJournal& journal);
    ~AtomicOperationHandler();

    // Initialize handler
    bool initialize();

    // Atomic operation control
    uint64_t begin_atomic_operation(NFSProcedure proc, 
                                  const std::vector<uint8_t>& args,
                                  const std::string& path);
    bool commit_operation(uint64_t op_id);
    bool rollback_operation(uint64_t op_id);

    // Transaction boundaries
    bool begin_transaction();
    bool commit_transaction();
    bool rollback_transaction();

    // State management
    bool save_operation_state(uint64_t op_id);
    bool verify_operation_state(uint64_t op_id);
    bool restore_operation_state(uint64_t op_id);

    // Status checks
    bool is_operation_active(uint64_t op_id) const;
    bool is_operation_completed(uint64_t op_id) const;
    bool is_operation_rolled_back(uint64_t op_id) const;

private:
    TransactionLog& txn_log_;
    OperationJournal& journal_;
    std::mutex mutex_;
    std::unordered_map<uint64_t, AtomicOperation> active_operations_;
    std::atomic<uint64_t> next_op_id_{1};
    bool in_transaction_{false};

    // Error handling
    bool handle_operation_error(uint64_t op_id, const std::string& error);
    bool attempt_error_recovery(uint64_t op_id);

    // State verification
    bool verify_pre_state(const AtomicOperation& op);
    bool verify_post_state(const AtomicOperation& op);
    bool verify_rollback_state(const AtomicOperation& op);

    // Helper methods
    bool save_pre_state(AtomicOperation& op);
    bool execute_operation(const AtomicOperation& op);
    bool rollback_to_pre_state(const AtomicOperation& op);
};

} // namespace fuse_t 
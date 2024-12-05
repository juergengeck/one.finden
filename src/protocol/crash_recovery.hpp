#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include "operation_journal.hpp"
#include "transaction_log.hpp"
#include "util/logger.hpp"

namespace fused {

enum class RecoveryPhase {
    SCAN,           // Scanning logs
    ANALYZE,        // Analyzing operations
    REDO,           // Redoing operations
    UNDO,           // Undoing operations
    VERIFY          // Verifying consistency
};

struct RecoveryState {
    RecoveryPhase phase{RecoveryPhase::SCAN};
    std::chrono::steady_clock::time_point start_time;
    uint64_t operations_processed{0};
    uint64_t operations_redone{0};
    uint64_t operations_undone{0};
    uint64_t consistency_violations{0};
    bool success{false};
};

class CrashRecoveryManager {
public:
    CrashRecoveryManager(TransactionLog& txn_log, OperationJournal& journal);

    // Initialize recovery manager
    bool initialize();

    // Start recovery process
    bool start_recovery();

    // Check recovery status
    bool is_recovery_complete() const;
    const RecoveryState& get_recovery_state() const;

    // Recovery verification
    bool verify_consistency();
    bool verify_invariants();

private:
    TransactionLog& txn_log_;
    OperationJournal& journal_;
    std::mutex mutex_;
    std::atomic<bool> recovery_in_progress_{false};
    RecoveryState state_;

    // Recovery phases
    bool scan_logs();
    bool analyze_operations();
    bool redo_operations();
    bool undo_operations();
    bool verify_recovery();

    // State reconstruction
    bool reconstruct_state();
    bool verify_state_consistency();
    bool repair_inconsistencies();

    // Recovery helpers
    struct RecoveryOperation {
        uint64_t sequence_id;
        uint64_t transaction_id;
        NFSProcedure procedure;
        std::string target_path;
        bool needs_redo{false};
        bool needs_undo{false};
    };

    std::vector<RecoveryOperation> recovery_ops_;
    
    // Invariant checking
    bool check_file_system_invariants();
    bool check_directory_invariants();
    bool check_handle_invariants();
    
    // Error handling
    void handle_recovery_error(const std::string& error);
    bool attempt_error_recovery();
};

} // namespace fuse_t 
#include "crash_recovery.hpp"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

namespace fused {

CrashRecoveryManager::CrashRecoveryManager(TransactionLog& txn_log, 
                                         OperationJournal& journal)
    : txn_log_(txn_log)
    , journal_(journal) {
}

bool CrashRecoveryManager::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    LOG_INFO("Initializing crash recovery manager");
    state_ = RecoveryState{};
    return true;
}

bool CrashRecoveryManager::start_recovery() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (recovery_in_progress_) {
        LOG_ERROR("Recovery already in progress");
        return false;
    }

    LOG_INFO("Starting crash recovery");
    recovery_in_progress_ = true;
    state_.start_time = std::chrono::steady_clock::now();
    state_.success = false;

    try {
        // Execute recovery phases
        if (!scan_logs()) {
            handle_recovery_error("Failed to scan logs");
            return false;
        }
        state_.phase = RecoveryPhase::ANALYZE;

        if (!analyze_operations()) {
            handle_recovery_error("Failed to analyze operations");
            return false;
        }
        state_.phase = RecoveryPhase::REDO;

        if (!redo_operations()) {
            handle_recovery_error("Failed to redo operations");
            return false;
        }
        state_.phase = RecoveryPhase::UNDO;

        if (!undo_operations()) {
            handle_recovery_error("Failed to undo operations");
            return false;
        }
        state_.phase = RecoveryPhase::VERIFY;

        if (!verify_recovery()) {
            handle_recovery_error("Failed to verify recovery");
            return false;
        }

        // Recovery successful
        state_.success = true;
        LOG_INFO("Crash recovery completed successfully");
        return true;

    } catch (const std::exception& e) {
        handle_recovery_error(e.what());
        return false;
    }
}

bool CrashRecoveryManager::scan_logs() {
    LOG_INFO("Scanning transaction and operation logs");
    
    // Get incomplete operations from journal
    auto journal_ops = journal_.get_incomplete_operations();
    
    // Get uncommitted transactions
    auto txn_ops = txn_log_.get_uncommitted_transactions();
    
    // Build recovery operation list
    recovery_ops_.clear();
    for (const auto& op : journal_ops) {
        RecoveryOperation rec_op{
            op.sequence_id,
            op.transaction_id,
            op.procedure,
            op.target_path,
            true,   // Needs redo by default
            false   // Doesn't need undo yet
        };
        recovery_ops_.push_back(rec_op);
    }

    // Mark operations that need undo
    for (const auto& txn : txn_ops) {
        auto it = std::find_if(recovery_ops_.begin(), recovery_ops_.end(),
            [&](const RecoveryOperation& op) {
                return op.transaction_id == txn.transaction_id;
            });
            
        if (it != recovery_ops_.end()) {
            it->needs_undo = true;
        }
    }

    state_.operations_processed = recovery_ops_.size();
    LOG_INFO("Found {} operations requiring recovery", recovery_ops_.size());
    return true;
}

bool CrashRecoveryManager::analyze_operations() {
    LOG_INFO("Analyzing operations for recovery");
    
    // Sort operations by dependencies
    std::sort(recovery_ops_.begin(), recovery_ops_.end(),
        [](const RecoveryOperation& a, const RecoveryOperation& b) {
            return a.sequence_id < b.sequence_id;
        });

    // Analyze operation dependencies
    for (auto& op : recovery_ops_) {
        // Check if operation can be safely redone
        struct stat st;
        bool exists = stat(op.target_path.c_str(), &st) == 0;

        switch (op.procedure) {
            case NFSProcedure::CREATE:
                op.needs_redo = !exists;  // Only redo if file doesn't exist
                break;
                
            case NFSProcedure::REMOVE:
                op.needs_redo = exists;   // Only redo if file exists
                break;
                
            case NFSProcedure::WRITE:
                // Always redo writes to ensure consistency
                op.needs_redo = true;
                break;
                
            // Add more cases as needed
        }
    }

    return true;
}

bool CrashRecoveryManager::redo_operations() {
    LOG_INFO("Redoing operations");
    
    for (const auto& op : recovery_ops_) {
        if (!op.needs_redo) continue;

        try {
            // Replay operation through journal
            if (journal_.replay_operation(op.sequence_id)) {
                state_.operations_redone++;
                LOG_DEBUG("Redone operation {}", op.sequence_id);
            } else {
                LOG_ERROR("Failed to redo operation {}", op.sequence_id);
                return false;
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Exception during redo of operation {}: {}", 
                op.sequence_id, e.what());
            return false;
        }
    }

    LOG_INFO("Redone {} operations", state_.operations_redone);
    return true;
}

bool CrashRecoveryManager::undo_operations() {
    LOG_INFO("Undoing operations");
    
    // Process operations in reverse order
    for (auto it = recovery_ops_.rbegin(); it != recovery_ops_.rend(); ++it) {
        if (!it->needs_undo) continue;

        try {
            // Rollback operation through transaction log
            if (txn_log_.rollback_transaction(it->transaction_id)) {
                state_.operations_undone++;
                LOG_DEBUG("Undone operation {}", it->sequence_id);
            } else {
                LOG_ERROR("Failed to undo operation {}", it->sequence_id);
                return false;
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Exception during undo of operation {}: {}", 
                it->sequence_id, e.what());
            return false;
        }
    }

    LOG_INFO("Undone {} operations", state_.operations_undone);
    return true;
}

bool CrashRecoveryManager::verify_recovery() {
    LOG_INFO("Verifying recovery");
    
    // Reconstruct and verify state
    if (!reconstruct_state()) {
        LOG_ERROR("Failed to reconstruct state");
        return false;
    }

    // Verify consistency
    if (!verify_consistency()) {
        LOG_ERROR("Consistency verification failed");
        return attempt_error_recovery();
    }

    // Check invariants
    if (!verify_invariants()) {
        LOG_ERROR("Invariant verification failed");
        return attempt_error_recovery();
    }

    return true;
}

bool CrashRecoveryManager::verify_consistency() {
    // Check file system consistency
    if (!check_file_system_invariants()) {
        state_.consistency_violations++;
        return false;
    }

    // Check directory structure
    if (!check_directory_invariants()) {
        state_.consistency_violations++;
        return false;
    }

    // Check handle mappings
    if (!check_handle_invariants()) {
        state_.consistency_violations++;
        return false;
    }

    return true;
}

// ... implement remaining methods ... 
#pragma once
#include <string>
#include <vector>
#include <mutex>
#include "nfs_protocol.hpp"
#include "operation_journal.hpp"
#include "state_validator.hpp"
#include "util/logger.hpp"

namespace fused {

struct EmergencyRecoveryOptions {
    bool force_fsck{false};
    bool ignore_journal{false};
    bool rebuild_metadata{false};
    bool allow_data_loss{false};
    std::chrono::seconds timeout{600};  // 10 minutes default
};

class EmergencyRecovery {
public:
    EmergencyRecovery(OperationJournal& journal, StateValidator& validator);
    ~EmergencyRecovery();

    // Emergency recovery operations
    bool attempt_recovery(const std::string& path, const EmergencyRecoveryOptions& options);
    bool verify_emergency_state();
    bool abort_emergency_recovery();

private:
    OperationJournal& journal_;
    StateValidator& validator_;
    std::mutex mutex_;
    bool recovery_in_progress_{false};
    EmergencyRecoveryOptions current_options_;

    // Recovery phases
    bool perform_fsck();
    bool rebuild_journal();
    bool rebuild_metadata();
    bool verify_recovery_state();

    // Recovery operations
    bool recover_directory_structure(const std::string& path);
    bool recover_file_content(const std::string& path);
    bool recover_metadata(const std::string& path);
    bool verify_recovered_state(const std::string& path);

    // Helper methods
    bool backup_corrupted_state();
    bool restore_from_backup();
    void log_emergency_status(const std::string& status);
};

} // namespace fuse_t 
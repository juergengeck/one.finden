#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include "nfs_protocol.hpp"
#include "operation_journal.hpp"
#include "state_validator.hpp"
#include "util/logger.hpp"

namespace fused {

enum class SystemRecoveryPhase {
    SCAN,           // Scan system state
    ANALYZE,        // Analyze inconsistencies
    REPAIR,         // Repair system state
    VERIFY,         // Verify repairs
    COMPLETE        // Recovery complete
};

struct SystemRecoveryState {
    SystemRecoveryPhase phase{SystemRecoveryPhase::SCAN};
    std::chrono::steady_clock::time_point start_time;
    std::vector<std::string> affected_paths;
    std::vector<std::string> repaired_paths;
    bool completed{false};
    bool successful{false};
    std::string error_message;
};

class SystemStateRecovery {
public:
    SystemStateRecovery(OperationJournal& journal, StateValidator& validator);
    ~SystemStateRecovery();

    // Initialize recovery system
    bool initialize();

    // Recovery operations
    bool start_recovery();
    bool complete_recovery();
    bool verify_recovery();
    bool abort_recovery();

    // State repair
    bool repair_consistency(const std::string& path);
    bool repair_data_integrity(const std::string& path);
    bool repair_metadata(const std::string& path);

    // Status checks
    bool is_recovery_in_progress() const;
    bool is_recovery_completed() const;
    SystemRecoveryPhase get_recovery_phase() const;

private:
    OperationJournal& journal_;
    StateValidator& validator_;
    std::mutex mutex_;
    std::thread recovery_thread_;
    std::atomic<bool> running_{true};
    SystemRecoveryState state_;

    static constexpr auto RECOVERY_TIMEOUT = std::chrono::minutes(30);
    static constexpr auto RETRY_INTERVAL = std::chrono::seconds(1);
    static constexpr auto MAX_RETRY_COUNT = 3;

    // Recovery thread
    void run_recovery_loop();
    void process_recovery_state();
    void cleanup_recovery_state();

    // Recovery phases
    bool scan_system_state();
    bool analyze_inconsistencies();
    bool repair_system_state();
    bool verify_system_state();
    bool complete_system_recovery();

    // Recovery helpers
    bool verify_filesystem_state();
    bool verify_data_integrity();
    bool verify_metadata_consistency();
    bool repair_filesystem_state();
    bool repair_data_integrity();
    bool repair_metadata_consistency();

    // Error handling
    void handle_recovery_error(const std::string& error);
    bool attempt_recovery_retry();

    // State tracking
    struct RepairOperation {
        std::string path;
        std::string repair_type;
        std::chrono::steady_clock::time_point timestamp;
        bool completed{false};
        bool successful{false};
        std::string error_message;
    };
    std::vector<RepairOperation> repair_operations_;

    // Helper methods
    bool verify_repair_operation(const RepairOperation& op);
    bool execute_repair_operation(RepairOperation& op);
    void log_repair_result(const RepairOperation& op);
};

} // namespace fuse_t 
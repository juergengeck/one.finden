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
#include "consistency_manager.hpp"
#include "util/logger.hpp"

namespace fused {

enum class SystemRecoveryType {
    FULL,           // Complete system recovery
    INCREMENTAL,    // Incremental state recovery
    PARTIAL,        // Partial state recovery
    EMERGENCY      // Emergency recovery mode
};

struct SystemRecoveryOptions {
    SystemRecoveryType type{SystemRecoveryType::INCREMENTAL};
    bool force_repair{false};
    bool verify_after_repair{true};
    bool backup_before_repair{true};
    std::chrono::seconds timeout{300};  // 5 minutes default
};

class SystemStateRecovery {
public:
    SystemStateRecovery(OperationJournal& journal, 
                       StateValidator& validator,
                       ConsistencyManager& consistency);
    ~SystemStateRecovery();

    // Initialize recovery system
    bool initialize();

    // Recovery operations
    bool start_recovery(const SystemRecoveryOptions& options);
    bool verify_system_state();
    bool repair_system_state();
    bool abort_recovery();

    // State repair
    bool repair_filesystem_state();
    bool repair_metadata_state();
    bool repair_consistency_state();

    // Status checks
    bool is_recovery_needed() const;
    bool is_recovery_in_progress() const;
    bool is_system_consistent() const;

private:
    OperationJournal& journal_;
    StateValidator& validator_;
    ConsistencyManager& consistency_;
    std::mutex mutex_;
    std::atomic<bool> recovery_in_progress_{false};
    SystemRecoveryOptions current_options_;

    static constexpr auto REPAIR_TIMEOUT = std::chrono::minutes(5);
    static constexpr auto VERIFY_INTERVAL = std::chrono::seconds(1);
    static constexpr auto MAX_REPAIR_ATTEMPTS = 3;

    // Recovery phases
    bool scan_system_state();
    bool analyze_inconsistencies();
    bool plan_recovery();
    bool execute_recovery_plan();
    bool verify_recovery();

    // Recovery helpers
    struct RepairPlan {
        std::vector<std::string> repair_paths;
        std::vector<std::string> verify_paths;
        std::vector<std::string> skip_paths;
        bool requires_fsync{false};
        bool requires_journal_replay{false};
    };
    RepairPlan current_plan_;

    // State tracking
    struct RecoveryState {
        std::chrono::steady_clock::time_point start_time;
        uint32_t attempt_count{0};
        std::vector<std::string> repaired_paths;
        std::vector<std::string> failed_paths;
        bool successful{false};
    };
    RecoveryState recovery_state_;

    // Recovery operations
    bool create_recovery_plan();
    bool execute_repair_operations();
    bool verify_repair_results();
    bool handle_repair_failure(const std::string& path);

    // Filesystem operations
    bool repair_directory_structure(const std::string& path);
    bool repair_file_state(const std::string& path);
    bool repair_metadata(const std::string& path);
    bool verify_filesystem_integrity(const std::string& path);

    // Error handling
    void log_recovery_error(const std::string& error);
    bool attempt_emergency_recovery();
    void notify_recovery_status(const std::string& status);
};

} // namespace fuse_t 
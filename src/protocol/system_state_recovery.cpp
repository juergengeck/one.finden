#include "system_state_recovery.hpp"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

namespace fused {

SystemStateRecovery::SystemStateRecovery(OperationJournal& journal,
                                       StateValidator& validator,
                                       ConsistencyManager& consistency)
    : journal_(journal)
    , validator_(validator)
    , consistency_(consistency) {
}

SystemStateRecovery::~SystemStateRecovery() = default;

bool SystemStateRecovery::initialize() {
    LOG_INFO("Initializing system state recovery");
    return true;
}

bool SystemStateRecovery::start_recovery(const SystemRecoveryOptions& options) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (recovery_in_progress_) {
        LOG_ERROR("Recovery already in progress");
        return false;
    }

    current_options_ = options;
    recovery_state_ = RecoveryState{
        std::chrono::steady_clock::now(),
        0,  // attempt_count
        {}, // repaired_paths
        {}, // failed_paths
        false // not successful
    };

    // Backup if requested
    if (options.backup_before_repair) {
        // TODO: Implement backup
    }

    recovery_in_progress_ = true;
    LOG_INFO("Starting system recovery with type: {}", 
        static_cast<int>(options.type));

    // Execute recovery phases
    bool success = scan_system_state() &&
                  analyze_inconsistencies() &&
                  plan_recovery() &&
                  execute_recovery_plan();

    if (success && options.verify_after_repair) {
        success = verify_recovery();
    }

    recovery_state_.successful = success;
    recovery_in_progress_ = false;

    LOG_INFO("System recovery completed: {}", success ? "success" : "failure");
    return success;
}

bool SystemStateRecovery::verify_system_state() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    LOG_INFO("Verifying system state");

    // Verify filesystem state
    if (!verify_filesystem_integrity("/")) {
        LOG_ERROR("Filesystem integrity check failed");
        return false;
    }

    // Verify metadata consistency
    if (!validator_.verify_metadata_consistency("/")) {
        LOG_ERROR("Metadata consistency check failed");
        return false;
    }

    // Verify journal consistency
    if (!journal_.verify_consistency()) {
        LOG_ERROR("Journal consistency check failed");
        return false;
    }

    return true;
}

bool SystemStateRecovery::repair_system_state() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!create_recovery_plan()) {
        LOG_ERROR("Failed to create recovery plan");
        return false;
    }

    return execute_repair_operations();
}

bool SystemStateRecovery::abort_recovery() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!recovery_in_progress_) {
        return false;
    }

    LOG_INFO("Aborting recovery");
    recovery_in_progress_ = false;
    return true;
}

bool SystemStateRecovery::repair_filesystem_state() {
    for (const auto& path : current_plan_.repair_paths) {
        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (!repair_directory_structure(path)) {
                return false;
            }
        } else if (S_ISREG(st.st_mode)) {
            if (!repair_file_state(path)) {
                return false;
            }
        }
    }

    return true;
}

bool SystemStateRecovery::repair_metadata_state() {
    for (const auto& path : current_plan_.repair_paths) {
        if (!repair_metadata(path)) {
            return false;
        }
    }

    return true;
}

bool SystemStateRecovery::repair_consistency_state() {
    return consistency_.repair_inconsistencies(current_plan_.repair_paths);
}

bool SystemStateRecovery::is_recovery_needed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !verify_system_state();
}

bool SystemStateRecovery::is_recovery_in_progress() const {
    return recovery_in_progress_;
}

bool SystemStateRecovery::is_system_consistent() const {
    return verify_system_state();
}

// Private methods
bool SystemStateRecovery::scan_system_state() {
    LOG_INFO("Scanning system state");
    
    // Scan filesystem
    if (!verify_filesystem_integrity("/")) {
        current_plan_.repair_paths.push_back("/");
    }

    // Scan journal
    auto corrupted_paths = journal_.find_corrupted_entries();
    current_plan_.repair_paths.insert(
        current_plan_.repair_paths.end(),
        corrupted_paths.begin(),
        corrupted_paths.end()
    );

    return true;
}

bool SystemStateRecovery::analyze_inconsistencies() {
    LOG_INFO("Analyzing inconsistencies");
    
    for (const auto& path : current_plan_.repair_paths) {
        // Check if path needs journal replay
        if (journal_.needs_replay(path)) {
            current_plan_.requires_journal_replay = true;
        }

        // Check if path needs fsync
        if (consistency_.needs_sync(path)) {
            current_plan_.requires_fsync = true;
        }

        // Determine verification requirements
        if (validator_.requires_verification(path)) {
            current_plan_.verify_paths.push_back(path);
        }
    }

    return true;
}

bool SystemStateRecovery::plan_recovery() {
    LOG_INFO("Planning recovery");
    
    // Sort paths by recovery priority
    std::sort(current_plan_.repair_paths.begin(), 
              current_plan_.repair_paths.end(),
              [this](const std::string& a, const std::string& b) {
                  return get_recovery_priority(a) > get_recovery_priority(b);
              });

    // Filter out paths that can't be repaired
    current_plan_.repair_paths.erase(
        std::remove_if(current_plan_.repair_paths.begin(),
                      current_plan_.repair_paths.end(),
                      [this](const std::string& path) {
                          return !can_repair_path(path);
                      }),
        current_plan_.repair_paths.end()
    );

    return !current_plan_.repair_paths.empty();
}

bool SystemStateRecovery::execute_recovery_plan() {
    LOG_INFO("Executing recovery plan");
    
    // Journal replay if needed
    if (current_plan_.requires_journal_replay) {
        if (!journal_.replay_operations(current_plan_.repair_paths)) {
            return false;
        }
    }

    // Repair filesystem state
    if (!repair_filesystem_state()) {
        return false;
    }

    // Repair metadata
    if (!repair_metadata_state()) {
        return false;
    }

    // Repair consistency
    if (!repair_consistency_state()) {
        return false;
    }

    // Fsync if needed
    if (current_plan_.requires_fsync) {
        for (const auto& path : current_plan_.repair_paths) {
            if (!consistency_.sync_path(path)) {
                return false;
            }
        }
    }

    return true;
}

bool SystemStateRecovery::verify_recovery() {
    LOG_INFO("Verifying recovery");
    
    for (const auto& path : current_plan_.verify_paths) {
        if (!verify_filesystem_integrity(path)) {
            return false;
        }
    }

    return true;
}

bool SystemStateRecovery::create_recovery_plan() {
    current_plan_ = RepairPlan{};
    return scan_system_state() && 
           analyze_inconsistencies() && 
           plan_recovery();
}

bool SystemStateRecovery::execute_repair_operations() {
    recovery_state_.attempt_count++;
    
    for (const auto& path : current_plan_.repair_paths) {
        if (!repair_path(path)) {
            if (!handle_repair_failure(path)) {
                return false;
            }
        } else {
            recovery_state_.repaired_paths.push_back(path);
        }
    }

    return verify_repair_results();
}

bool SystemStateRecovery::verify_repair_results() {
    for (const auto& path : recovery_state_.repaired_paths) {
        if (!verify_filesystem_integrity(path)) {
            return false;
        }
    }
    return true;
}

bool SystemStateRecovery::handle_repair_failure(const std::string& path) {
    recovery_state_.failed_paths.push_back(path);
    
    if (recovery_state_.attempt_count >= MAX_REPAIR_ATTEMPTS) {
        log_recovery_error("Max repair attempts reached for path: " + path);
        return false;
    }

    if (current_options_.type == SystemRecoveryType::EMERGENCY) {
        return attempt_emergency_recovery();
    }

    return false;
}

void SystemStateRecovery::log_recovery_error(const std::string& error) {
    LOG_ERROR("Recovery error: {}", error);
    notify_recovery_status("error: " + error);
}

bool SystemStateRecovery::attempt_emergency_recovery() {
    LOG_WARN("Attempting emergency recovery");
    // TODO: Implement emergency recovery
    return false;
}

void SystemStateRecovery::notify_recovery_status(const std::string& status) {
    LOG_INFO("Recovery status: {}", status);
    // TODO: Implement notification system
}

} // namespace fuse_t 
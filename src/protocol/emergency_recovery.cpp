#include "emergency_recovery.hpp"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

namespace fused {

EmergencyRecovery::EmergencyRecovery(OperationJournal& journal, 
                                   StateValidator& validator)
    : journal_(journal)
    , validator_(validator) {
}

EmergencyRecovery::~EmergencyRecovery() = default;

bool EmergencyRecovery::attempt_recovery(const std::string& path,
                                       const EmergencyRecoveryOptions& options) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (recovery_in_progress_) {
        LOG_ERROR("Emergency recovery already in progress");
        return false;
    }

    current_options_ = options;
    recovery_in_progress_ = true;

    LOG_WARN("Starting emergency recovery for path: {}", path);

    // Backup current state
    if (!backup_corrupted_state()) {
        LOG_ERROR("Failed to backup corrupted state");
        return false;
    }

    bool success = true;

    // Perform fsck if requested
    if (options.force_fsck) {
        success = perform_fsck();
    }

    // Rebuild journal if needed
    if (success && !options.ignore_journal) {
        success = rebuild_journal();
    }

    // Rebuild metadata if requested
    if (success && options.rebuild_metadata) {
        success = rebuild_metadata();
    }

    // Verify final state
    if (success) {
        success = verify_recovery_state();
    }

    recovery_in_progress_ = false;
    LOG_INFO("Emergency recovery completed: {}", success ? "success" : "failure");
    return success;
}

bool EmergencyRecovery::verify_emergency_state() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Verify basic filesystem integrity
    if (!validator_.verify_filesystem_integrity("/")) {
        return false;
    }

    // Verify journal if not ignored
    if (!current_options_.ignore_journal) {
        if (!journal_.verify_emergency_state()) {
            return false;
        }
    }

    return true;
}

bool EmergencyRecovery::abort_emergency_recovery() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!recovery_in_progress_) {
        return false;
    }

    LOG_WARN("Aborting emergency recovery");
    recovery_in_progress_ = false;

    // Restore from backup
    return restore_from_backup();
}

bool EmergencyRecovery::perform_fsck() {
    LOG_INFO("Running fsck on filesystem");
    
    // Execute fsck with appropriate options
    std::string cmd = "fsck -f -y " + std::string(getenv("FUSED_NFS_MOUNT"));
    int result = system(cmd.c_str());

    if (result != 0) {
        LOG_ERROR("fsck failed with code: {}", result);
        return false;
    }

    return true;
}

bool EmergencyRecovery::rebuild_journal() {
    LOG_INFO("Rebuilding operation journal");
    
    // Clear corrupted journal entries
    if (!journal_.clear_corrupted_entries()) {
        return false;
    }

    // Scan filesystem and rebuild journal entries
    return journal_.rebuild_from_filesystem();
}

bool EmergencyRecovery::rebuild_metadata() {
    LOG_INFO("Rebuilding metadata");
    
    // Scan filesystem
    std::vector<std::string> paths;
    if (!scan_filesystem("/", paths)) {
        return false;
    }

    // Rebuild metadata for each path
    for (const auto& path : paths) {
        if (!recover_metadata(path)) {
            LOG_ERROR("Failed to rebuild metadata for: {}", path);
            return false;
        }
    }

    return true;
}

bool EmergencyRecovery::verify_recovery_state() {
    LOG_INFO("Verifying recovered state");
    
    // Verify filesystem state
    if (!validator_.verify_filesystem_integrity("/")) {
        return false;
    }

    // Verify metadata if rebuilt
    if (current_options_.rebuild_metadata) {
        if (!validator_.verify_metadata_consistency("/")) {
            return false;
        }
    }

    // Verify journal if not ignored
    if (!current_options_.ignore_journal) {
        if (!journal_.verify_consistency()) {
            return false;
        }
    }

    return true;
}

bool EmergencyRecovery::recover_directory_structure(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }

    // Fix directory permissions
    if (S_ISDIR(st.st_mode)) {
        chmod(path.c_str(), 0755);
    }

    // Recursively process subdirectories
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return false;
    }

    bool success = true;
    while (struct dirent* entry = readdir(dir)) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        std::string entry_path = path + "/" + entry->d_name;
        if (!recover_directory_structure(entry_path)) {
            success = false;
            break;
        }
    }

    closedir(dir);
    return success;
}

bool EmergencyRecovery::recover_file_content(const std::string& path) {
    // Try to recover from journal first
    if (!current_options_.ignore_journal) {
        if (journal_.recover_file_content(path)) {
            return true;
        }
    }

    // If allowed to lose data, truncate corrupted files
    if (current_options_.allow_data_loss) {
        int fd = open(path.c_str(), O_WRONLY | O_TRUNC);
        if (fd >= 0) {
            close(fd);
            return true;
        }
    }

    return false;
}

bool EmergencyRecovery::recover_metadata(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }

    // Fix basic metadata
    if (S_ISREG(st.st_mode)) {
        chmod(path.c_str(), 0644);
    } else if (S_ISDIR(st.st_mode)) {
        chmod(path.c_str(), 0755);
    }

    // Try to recover extended attributes
    if (!validator_.recover_extended_attributes(path)) {
        LOG_WARN("Failed to recover extended attributes for: {}", path);
        // Continue anyway
    }

    return true;
}

bool EmergencyRecovery::backup_corrupted_state() {
    LOG_INFO("Backing up corrupted state");
    
    std::string backup_cmd = "tar czf /var/lib/fused-nfs/emergency_backup.tar.gz "
                            "/var/lib/fused-nfs/journal "
                            "/var/lib/fused-nfs/metadata";
    
    return system(backup_cmd.c_str()) == 0;
}

bool EmergencyRecovery::restore_from_backup() {
    LOG_INFO("Restoring from backup");
    
    std::string restore_cmd = "tar xzf /var/lib/fused-nfs/emergency_backup.tar.gz "
                             "-C /";
    
    return system(restore_cmd.c_str()) == 0;
}

void EmergencyRecovery::log_emergency_status(const std::string& status) {
    LOG_WARN("Emergency recovery status: {}", status);
    // TODO: Implement emergency notification system
}

} // namespace fuse_t 
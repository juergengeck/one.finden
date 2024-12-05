#include "state_validator.hpp"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

namespace fused {

StateValidator::StateValidator(OperationJournal& journal)
    : journal_(journal) {
    // Start validation thread
    validation_thread_ = std::thread([this]() {
        run_validation_loop();
    });
}

StateValidator::~StateValidator() {
    running_ = false;
    if (validation_thread_.joinable()) {
        validation_thread_.join();
    }
}

bool StateValidator::initialize() {
    LOG_INFO("Initializing state validator");
    return true;
}

void StateValidator::add_validation_rule(const StateValidationRule& rule) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& state = validation_states_[rule.path];
    state.path = rule.path;
    state.rules.push_back(rule);
    
    LOG_INFO("Added validation rule for path: {}", rule.path);
}

bool StateValidator::remove_validation_rule(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = validation_states_.find(path);
    if (it == validation_states_.end()) {
        return false;
    }

    validation_states_.erase(it);
    LOG_INFO("Removed validation rules for path: {}", path);
    return true;
}

bool StateValidator::update_validation_rule(const std::string& path,
                                         const StateValidationRule& rule) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = validation_states_.find(path);
    if (it == validation_states_.end()) {
        return false;
    }

    // Find and update matching rule
    for (auto& existing : it->second.rules) {
        if (existing.type == rule.type) {
            existing = rule;
            LOG_INFO("Updated validation rule for path: {}", path);
            return true;
        }
    }

    return false;
}

bool StateValidator::validate_state(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = validation_states_.find(path);
    if (it == validation_states_.end()) {
        return true;  // No rules to validate
    }

    bool valid = true;
    for (const auto& rule : it->second.rules) {
        switch (rule.type) {
            case StateValidationType::INVARIANT:
                if (!check_invariants(path)) {
                    valid = false;
                    log_validation_error(path, "Invariant check failed");
                }
                break;

            case StateValidationType::CONSISTENCY:
                if (!check_data_consistency(path)) {
                    valid = false;
                    log_validation_error(path, "Consistency check failed");
                }
                break;

            case StateValidationType::INTEGRITY:
                if (!verify_data_integrity(path)) {
                    valid = false;
                    log_validation_error(path, "Integrity check failed");
                }
                break;

            default:
                break;
        }

        if (!valid && rule.critical) {
            trigger_recovery(path);
            break;
        }
    }

    it->second.last_check = std::chrono::steady_clock::now();
    return valid;
}

bool StateValidator::validate_transition(const std::string& path, uint64_t operation_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Create transition state if not exists
    auto& transition = transitions_[operation_id];
    transition.operation_id = operation_id;
    transition.path = path;
    transition.timestamp = std::chrono::steady_clock::now();

    if (!verify_state_transition(transition)) {
        log_validation_error(path, "State transition validation failed");
        transition.successful = false;
        return false;
    }

    transition.validated = true;
    transition.successful = true;
    return true;
}

bool StateValidator::validate_consistency(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!check_data_consistency(path)) {
        log_validation_error(path, "Data consistency validation failed");
        return false;
    }

    return true;
}

bool StateValidator::detect_errors(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = validation_states_.find(path);
    if (it == validation_states_.end()) {
        return false;
    }

    // Check for error threshold
    if (it->second.error_count >= ERROR_THRESHOLD) {
        it->second.needs_recovery = true;
        return true;
    }

    return false;
}

bool StateValidator::handle_error(const std::string& path, const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = validation_states_.find(path);
    if (it == validation_states_.end()) {
        return false;
    }

    // Increment error count and log
    it->second.error_count++;
    log_validation_error(path, error);

    // Trigger recovery if needed
    if (detect_errors(path)) {
        return trigger_recovery(path);
    }

    return true;
}

bool StateValidator::trigger_recovery(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    LOG_INFO("Triggering recovery for path: {}", path);
    
    if (!attempt_error_recovery(path)) {
        LOG_ERROR("Recovery failed for path: {}", path);
        return false;
    }

    // Verify recovery was successful
    if (!verify_recovery_success(path)) {
        LOG_ERROR("Recovery verification failed for path: {}", path);
        return false;
    }

    // Reset error state
    auto it = validation_states_.find(path);
    if (it != validation_states_.end()) {
        it->second.error_count = 0;
        it->second.needs_recovery = false;
    }

    return true;
}

void StateValidator::run_validation_loop() {
    while (running_) {
        process_validation_states();
        cleanup_old_states();
        std::this_thread::sleep_for(VALIDATION_INTERVAL);
    }
}

void StateValidator::process_validation_states() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& [path, state] : validation_states_) {
        if (state.needs_recovery) {
            trigger_recovery(path);
            continue;
        }

        // Perform periodic validation
        if (!validate_state(path)) {
            handle_error(path, "Periodic validation failed");
        }
    }
}

void StateValidator::cleanup_old_states() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    
    // Cleanup old transitions
    for (auto it = transitions_.begin(); it != transitions_.end();) {
        if (now - it->second.timestamp > std::chrono::minutes(5)) {
            it = transitions_.erase(it);
        } else {
            ++it;
        }
    }
}

bool StateValidator::check_invariants(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }

    // Check basic invariants
    if (S_ISREG(st.st_mode)) {
        // File invariants
        if (access(path.c_str(), R_OK) != 0) {
            return false;
        }
    } else if (S_ISDIR(st.st_mode)) {
        // Directory invariants
        if (access(path.c_str(), X_OK) != 0) {
            return false;
        }
    }

    return true;
}

bool StateValidator::verify_state_transition(const TransitionState& transition) {
    // Get operation details from journal
    auto op = journal_.get_operation(transition.operation_id);
    if (!op) {
        return false;
    }

    // Verify state matches expected post-operation state
    struct stat st;
    if (stat(transition.path.c_str(), &st) != 0) {
        return false;
    }

    // Verify based on operation type
    switch (op->procedure) {
        case NFSProcedure::CREATE:
            // Should exist and be the correct type
            if (!S_ISREG(st.st_mode)) {
                log_validation_error(transition.path, "Created file has wrong type");
                return false;
            }
            break;

        case NFSProcedure::MKDIR:
            if (!S_ISDIR(st.st_mode)) {
                log_validation_error(transition.path, "Created directory has wrong type");
                return false;
            }
            break;

        case NFSProcedure::REMOVE:
            // Should not exist
            if (access(transition.path.c_str(), F_OK) == 0) {
                log_validation_error(transition.path, "Removed file still exists");
                return false;
            }
            break;

        case NFSProcedure::WRITE:
            // Verify file size and modification time
            if (!verify_write_state(transition.path, op)) {
                return false;
            }
            break;

        case NFSProcedure::SETATTR:
            // Verify attributes were set correctly
            if (!verify_attributes(transition.path, op)) {
                return false;
            }
            break;
    }

    return true;
}

bool StateValidator::verify_write_state(const std::string& path, const Operation* op) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }

    // Decode write arguments
    XDRDecoder decoder(op->arguments.data(), op->arguments.size());
    uint64_t offset, length;
    if (!decoder.decode(offset) || !decoder.decode(length)) {
        return false;
    }

    // Verify file size is at least offset + length
    if (st.st_size < static_cast<off_t>(offset + length)) {
        log_validation_error(path, "File size mismatch after write");
        return false;
    }

    return true;
}

bool StateValidator::verify_attributes(const std::string& path, const Operation* op) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }

    // Decode setattr arguments
    XDRDecoder decoder(op->arguments.data(), op->arguments.size());
    uint32_t mode;
    uint32_t uid, gid;
    if (!decoder.decode(mode) || !decoder.decode(uid) || !decoder.decode(gid)) {
        return false;
    }

    // Verify attributes match
    if ((st.st_mode & 07777) != (mode & 07777) ||
        st.st_uid != uid ||
        st.st_gid != gid) {
        log_validation_error(path, "Attributes mismatch after setattr");
        return false;
    }

    return true;
}

bool StateValidator::check_data_consistency(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }

    if (S_ISREG(st.st_mode)) {
        // Verify file content consistency
        int fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) {
            return false;
        }

        std::vector<uint8_t> buffer(4096);
        ssize_t bytes_read;
        uint64_t total_bytes = 0;
        uint32_t checksum = 0;

        while ((bytes_read = read(fd, buffer.data(), buffer.size())) > 0) {
            // Calculate running checksum
            for (ssize_t i = 0; i < bytes_read; i++) {
                checksum = ((checksum << 8) | buffer[i]) ^ (checksum >> 24);
            }
            total_bytes += bytes_read;
        }

        close(fd);

        // Verify total bytes match file size
        if (total_bytes != static_cast<uint64_t>(st.st_size)) {
            log_validation_error(path, "File size mismatch during consistency check");
            return false;
        }

        // Store/verify checksum
        return verify_file_checksum(path, checksum);
    }

    return true;
}

bool StateValidator::verify_data_integrity(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }

    // For regular files
    if (S_ISREG(st.st_mode)) {
        // Verify file system metadata
        if (!verify_fs_metadata(path)) {
            return false;
        }

        // Verify file content integrity
        if (!verify_file_integrity(path)) {
            return false;
        }
    }
    // For directories
    else if (S_ISDIR(st.st_mode)) {
        // Verify directory structure
        if (!verify_directory_integrity(path)) {
            return false;
        }
    }

    return true;
}

bool StateValidator::verify_fs_metadata(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }

    // Verify basic metadata consistency
    if (st.st_nlink == 0 || st.st_size < 0) {
        log_validation_error(path, "Invalid file system metadata");
        return false;
    }

    // Verify permissions are valid
    if ((st.st_mode & ~07777) != S_IFREG) {
        log_validation_error(path, "Invalid file permissions");
        return false;
    }

    return true;
}

bool StateValidator::verify_file_integrity(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }

    // Read file in chunks and verify each chunk
    std::vector<uint8_t> buffer(4096);
    ssize_t bytes_read;
    uint64_t offset = 0;
    bool valid = true;

    while ((bytes_read = read(fd, buffer.data(), buffer.size())) > 0) {
        // Verify chunk integrity
        if (!verify_chunk_integrity(path, offset, buffer.data(), bytes_read)) {
            valid = false;
            break;
        }
        offset += bytes_read;
    }

    close(fd);
    return valid && bytes_read >= 0;
}

bool StateValidator::verify_directory_integrity(const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return false;
    }

    bool valid = true;
    std::unordered_set<std::string> entries;

    while (struct dirent* entry = readdir(dir)) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Check for duplicate entries
        if (entries.find(entry->d_name) != entries.end()) {
            log_validation_error(path, "Duplicate directory entry: " + std::string(entry->d_name));
            valid = false;
            break;
        }
        entries.insert(entry->d_name);

        // Verify entry integrity
        std::string entry_path = path + "/" + entry->d_name;
        if (!verify_data_integrity(entry_path)) {
            valid = false;
            break;
        }
    }

    closedir(dir);
    return valid;
}

void StateValidator::log_validation_error(const std::string& path,
                                       const std::string& error) {
    LOG_ERROR("Validation error for {}: {}", path, error);
}

bool StateValidator::attempt_error_recovery(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }

    // For regular files
    if (S_ISREG(st.st_mode)) {
        return recover_file(path);
    }
    // For directories
    else if (S_ISDIR(st.st_mode)) {
        return recover_directory(path);
    }

    return false;
}

bool StateValidator::recover_file(const std::string& path) {
    // Try to restore from journal if available
    if (auto op = journal_.get_last_operation(path)) {
        if (op->pre_state.size() > 0) {
            // Restore pre-state
            int fd = open(path.c_str(), O_WRONLY | O_TRUNC);
            if (fd >= 0) {
                bool success = write(fd, op->pre_state.data(), op->pre_state.size()) == 
                             static_cast<ssize_t>(op->pre_state.size());
                close(fd);
                if (success) {
                    LOG_INFO("Recovered file from journal: {}", path);
                    return true;
                }
            }
        }
    }

    // If journal recovery fails, try to fix permissions
    chmod(path.c_str(), 0644);
    return verify_recovery_success(path);
}

bool StateValidator::recover_directory(const std::string& path) {
    // Try to fix directory permissions
    chmod(path.c_str(), 0755);

    // Verify directory structure
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
        if (detect_errors(entry_path)) {
            if (!attempt_error_recovery(entry_path)) {
                success = false;
                break;
            }
        }
    }

    closedir(dir);
    return success && verify_recovery_success(path);
}

bool StateValidator::verify_recovery_success(const std::string& path) {
    // Verify state after recovery
    return validate_state(path);
}

} // namespace fuse_t 
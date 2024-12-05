#include "state_verifier.hpp"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

namespace fused {

StateVerifier::StateVerifier() {
    // Add default invariants
    add_file_invariant({
        "file_readable",
        "",
        [](const std::string& path) {
            return access(path.c_str(), R_OK) == 0;
        },
        "File is not readable"
    });

    add_directory_invariant({
        "directory_traversable",
        "",
        [](const std::string& path) {
            return access(path.c_str(), X_OK) == 0;
        },
        "Directory is not traversable"
    });

    add_handle_invariant({
        "handle_valid",
        "",
        [](const std::string& path) {
            struct stat st;
            return stat(path.c_str(), &st) == 0;
        },
        "Handle points to invalid path"
    });
}

StateVerifier::~StateVerifier() = default;

bool StateVerifier::initialize() {
    LOG_INFO("Initializing state verifier");
    return true;
}

void StateVerifier::add_file_invariant(const StateInvariant& invariant) {
    std::lock_guard<std::mutex> lock(mutex_);
    file_invariants_.push_back(invariant);
}

void StateVerifier::add_directory_invariant(const StateInvariant& invariant) {
    std::lock_guard<std::mutex> lock(mutex_);
    directory_invariants_.push_back(invariant);
}

void StateVerifier::add_handle_invariant(const StateInvariant& invariant) {
    std::lock_guard<std::mutex> lock(mutex_);
    handle_invariants_.push_back(invariant);
}

StateValidationResult StateVerifier::verify_state() const {
    StateValidationResult result;
    result.timestamp = std::chrono::steady_clock::now();

    // Start from root directory
    std::string root = "/";
    if (!check_directory_invariants(root, result)) {
        result.valid = false;
    }

    // Recursively verify all paths
    std::function<void(const std::string&)> verify_recursive = 
        [&](const std::string& path) {
            DIR* dir = opendir(path.c_str());
            if (!dir) return;

            while (struct dirent* entry = readdir(dir)) {
                if (strcmp(entry->d_name, ".") == 0 || 
                    strcmp(entry->d_name, "..") == 0) {
                    continue;
                }

                std::string full_path = path + "/" + entry->d_name;
                struct stat st;
                if (stat(full_path.c_str(), &st) != 0) {
                    result.violations.push_back(
                        "Failed to stat path: " + full_path);
                    result.valid = false;
                    continue;
                }

                if (S_ISDIR(st.st_mode)) {
                    if (!check_directory_invariants(full_path, result)) {
                        result.valid = false;
                    }
                    verify_recursive(full_path);
                } else if (S_ISREG(st.st_mode)) {
                    if (!check_file_invariants(full_path, result)) {
                        result.valid = false;
                    }
                }
            }
            closedir(dir);
        };

    verify_recursive(root);
    return result;
}

StateValidationResult StateVerifier::verify_path(const std::string& path) const {
    StateValidationResult result;
    result.timestamp = std::chrono::steady_clock::now();

    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        result.valid = false;
        result.violations.push_back("Path does not exist: " + path);
        return result;
    }

    if (S_ISDIR(st.st_mode)) {
        if (!check_directory_invariants(path, result)) {
            result.valid = false;
        }
    } else if (S_ISREG(st.st_mode)) {
        if (!check_file_invariants(path, result)) {
            result.valid = false;
        }
    }

    return result;
}

StateValidationResult StateVerifier::verify_handle(const NFSFileHandle& handle) const {
    StateValidationResult result;
    result.timestamp = std::chrono::steady_clock::now();

    if (!check_handle_invariants(handle, result)) {
        result.valid = false;
    }

    return result;
}

bool StateVerifier::detect_corruption(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        log_corruption(path, "Path does not exist");
        return true;
    }

    if (S_ISREG(st.st_mode)) {
        if (!verify_file_integrity(path)) {
            log_corruption(path, "File integrity check failed");
            return true;
        }
    } else if (S_ISDIR(st.st_mode)) {
        if (!verify_directory_integrity(path)) {
            log_corruption(path, "Directory integrity check failed");
            return true;
        }
    }

    return false;
}

bool StateVerifier::repair_corruption(const std::string& path) {
    if (!detect_corruption(path)) {
        return true;  // No corruption detected
    }

    // Attempt repair
    if (!attempt_repair(path)) {
        LOG_ERROR("Failed to repair corruption at path: {}", path);
        return false;
    }

    // Verify repair was successful
    if (detect_corruption(path)) {
        LOG_ERROR("Corruption persists after repair at path: {}", path);
        return false;
    }

    LOG_INFO("Successfully repaired corruption at path: {}", path);
    return true;
}

void StateVerifier::set_recovery_trigger(const std::string& path,
                                       std::function<void()> trigger) {
    std::lock_guard<std::mutex> lock(mutex_);
    recovery_triggers_[path] = std::move(trigger);
}

void StateVerifier::trigger_recovery(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = recovery_triggers_.find(path);
    if (it != recovery_triggers_.end()) {
        LOG_INFO("Triggering recovery for path: {}", path);
        it->second();
    }
}

// Private helper methods
bool StateVerifier::check_file_invariants(const std::string& path,
                                        StateValidationResult& result) const {
    bool valid = true;
    result.objects_checked++;

    for (const auto& invariant : file_invariants_) {
        result.invariants_checked++;
        if (!invariant.check(path)) {
            valid = false;
            result.violations.push_back(
                invariant.name + ": " + invariant.error_message + 
                " (path: " + path + ")");
        }
    }

    return valid;
}

bool StateVerifier::check_directory_invariants(const std::string& path,
                                             StateValidationResult& result) const {
    bool valid = true;
    result.objects_checked++;

    for (const auto& invariant : directory_invariants_) {
        result.invariants_checked++;
        if (!invariant.check(path)) {
            valid = false;
            result.violations.push_back(
                invariant.name + ": " + invariant.error_message + 
                " (path: " + path + ")");
        }
    }

    return valid;
}

bool StateVerifier::check_handle_invariants(const NFSFileHandle& handle,
                                          StateValidationResult& result) const {
    bool valid = true;
    result.objects_checked++;

    std::string path = translate_handle_to_path(handle.handle);
    if (path.empty()) {
        result.violations.push_back("Invalid handle: cannot translate to path");
        return false;
    }

    for (const auto& invariant : handle_invariants_) {
        result.invariants_checked++;
        if (!invariant.check(path)) {
            valid = false;
            result.violations.push_back(
                invariant.name + ": " + invariant.error_message + 
                " (handle path: " + path + ")");
        }
    }

    return valid;
}

bool StateVerifier::verify_file_integrity(const std::string& path) {
    // Basic integrity checks
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return false;
    }

    // Read file content to verify it's readable
    std::vector<uint8_t> buffer(4096);
    ssize_t bytes_read;
    while ((bytes_read = read(fd, buffer.data(), buffer.size())) > 0) {
        // Just verify we can read the file
    }

    close(fd);
    return bytes_read >= 0;
}

bool StateVerifier::verify_directory_integrity(const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return false;
    }

    bool valid = true;
    while (struct dirent* entry = readdir(dir)) {
        std::string entry_path = path + "/" + entry->d_name;
        struct stat st;
        
        if (stat(entry_path.c_str(), &st) != 0) {
            valid = false;
            break;
        }
    }

    closedir(dir);
    return valid;
}

bool StateVerifier::verify_handle_integrity(const NFSFileHandle& handle) {
    std::string path = translate_handle_to_path(handle.handle);
    if (path.empty()) {
        return false;
    }

    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool StateVerifier::attempt_repair(const std::string& path) {
    // Basic repair attempts
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        if (S_ISREG(st.st_mode)) {
            // Try to repair file permissions
            chmod(path.c_str(), 0644);
        } else if (S_ISDIR(st.st_mode)) {
            // Try to repair directory permissions
            chmod(path.c_str(), 0755);
        }
    }

    // Trigger recovery if available
    trigger_recovery(path);
    return true;
}

void StateVerifier::log_corruption(const std::string& path, 
                                 const std::string& details) {
    LOG_ERROR("Corruption detected at {}: {}", path, details);
}

} // namespace fuse_t 
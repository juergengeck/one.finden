#include "consistency_manager.hpp"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

namespace fused {

ConsistencyManager::ConsistencyManager(OperationJournal& journal)
    : journal_(journal) {
    // Start verification thread
    verification_thread_ = std::thread([this]() {
        run_verification_loop();
    });
}

ConsistencyManager::~ConsistencyManager() {
    running_ = false;
    if (verification_thread_.joinable()) {
        verification_thread_.join();
    }
}

bool ConsistencyManager::initialize() {
    LOG_INFO("Initializing consistency manager");
    return true;
}

bool ConsistencyManager::set_consistency_level(const std::string& path, 
                                            ConsistencyLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    consistency_levels_[path] = level;
    LOG_INFO("Set consistency level for {}: {}", path, static_cast<int>(level));
    return true;
}

bool ConsistencyManager::enforce_consistency(const std::string& path,
                                          const ConsistencyGuarantee& guarantee) {
    // Check if path exists
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        LOG_ERROR("Path does not exist: {}", path);
        return false;
    }

    // Create consistency point
    uint64_t point_id = create_consistency_point(path);
    if (point_id == 0) {
        return false;
    }

    // Enforce consistency based on level
    switch (guarantee.level) {
        case ConsistencyLevel::STRICT:
            // Immediate consistency - sync and verify
            if (!sync_file_state(path)) {
                return false;
            }
            if (guarantee.require_verification && !verify_consistency(path)) {
                return false;
            }
            break;

        case ConsistencyLevel::SEQUENTIAL:
            // Ensure operations are ordered
            if (!verify_operation_order(path)) {
                return false;
            }
            break;

        case ConsistencyLevel::EVENTUAL:
            // Schedule verification
            if (!wait_for_consistency(point_id, guarantee.timeout)) {
                return false;
            }
            break;

        case ConsistencyLevel::RELAXED:
            // Best effort - no additional checks
            break;
    }

    // Perform fsync if required
    if (guarantee.require_fsync) {
        int fd = open(path.c_str(), O_RDONLY);
        if (fd >= 0) {
            fsync(fd);
            close(fd);
        }
    }

    return true;
}

bool ConsistencyManager::verify_consistency(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }

    // Verify based on file type
    if (S_ISREG(st.st_mode)) {
        return verify_file_state(path);
    } else if (S_ISDIR(st.st_mode)) {
        return verify_directory_state(path);
    }

    return true;
}

uint64_t ConsistencyManager::create_consistency_point(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    uint64_t point_id = next_point_id_++;
    
    ConsistencyPoint point{
        point_id,
        std::chrono::steady_clock::now(),
        consistency_levels_[path],
        false,  // not verified
        false   // not synced
    };

    consistency_points_[point_id] = std::move(point);
    return point_id;
}

bool ConsistencyManager::verify_consistency_point(uint64_t point_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = consistency_points_.find(point_id);
    if (it == consistency_points_.end()) {
        return false;
    }

    // Mark as verified if all checks pass
    it->second.verified = true;
    return true;
}

bool ConsistencyManager::wait_for_consistency(uint64_t point_id,
                                           std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = consistency_points_.find(point_id);
            if (it != consistency_points_.end() && it->second.verified) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return false;
}

bool ConsistencyManager::verify_file_state(const std::string& path) {
    // Verify file data consistency
    if (!verify_data_consistency(path)) {
        log_consistency_violation(path, "Data inconsistency detected");
        return false;
    }

    // Verify file metadata
    if (!verify_metadata_consistency(path)) {
        log_consistency_violation(path, "Metadata inconsistency detected");
        return false;
    }

    return true;
}

bool ConsistencyManager::verify_directory_state(const std::string& path) {
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
            log_consistency_violation(entry_path, "Failed to stat entry");
            break;
        }
    }

    closedir(dir);
    return valid;
}

bool ConsistencyManager::verify_handle_state(const NFSFileHandle& handle) {
    // Verify handle points to valid path
    std::string path = translate_handle_to_path(handle.handle);
    if (path.empty()) {
        return false;
    }

    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

// Private helper methods
bool ConsistencyManager::verify_operation_order(const std::string& path) {
    // Get operations from journal and verify order
    auto ops = journal_.get_operations_for_path(path);
    
    // Check sequence numbers are monotonically increasing
    for (size_t i = 1; i < ops.size(); i++) {
        if (ops[i].sequence_id <= ops[i-1].sequence_id) {
            log_consistency_violation(path, "Operation order violation");
            return false;
        }
    }

    return true;
}

bool ConsistencyManager::verify_data_consistency(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }

    // Read file content to verify it's consistent
    std::vector<uint8_t> buffer(4096);
    ssize_t bytes_read;
    bool valid = true;

    while ((bytes_read = read(fd, buffer.data(), buffer.size())) > 0) {
        // Verify data integrity
        // TODO: Add checksum verification
    }

    close(fd);
    return valid && bytes_read >= 0;
}

bool ConsistencyManager::verify_metadata_consistency(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }

    // Verify metadata is consistent
    // TODO: Add metadata verification
    return true;
}

bool ConsistencyManager::sync_file_state(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }

    bool success = fsync(fd) == 0;
    close(fd);
    return success;
}

bool ConsistencyManager::sync_directory_state(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }

    bool success = fsync(fd) == 0;
    close(fd);
    return success;
}

bool ConsistencyManager::repair_inconsistency(const std::string& path) {
    // Basic repair - try to sync state
    if (!sync_file_state(path)) {
        return false;
    }

    // Verify repair was successful
    return verify_consistency(path);
}

void ConsistencyManager::run_verification_loop() {
    while (running_) {
        verify_pending_points();
        cleanup_expired_points();
        std::this_thread::sleep_for(VERIFICATION_INTERVAL);
    }
}

void ConsistencyManager::verify_pending_points() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& [id, point] : consistency_points_) {
        if (!point.verified) {
            verify_consistency_point(id);
        }
    }
}

void ConsistencyManager::cleanup_expired_points() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    
    for (auto it = consistency_points_.begin(); it != consistency_points_.end();) {
        if (now - it->second.timestamp > DEFAULT_TIMEOUT) {
            it = consistency_points_.erase(it);
        } else {
            ++it;
        }
    }
}

bool ConsistencyManager::is_strict_consistency_required(const std::string& path) const {
    auto it = consistency_levels_.find(path);
    return it != consistency_levels_.end() && 
           it->second == ConsistencyLevel::STRICT;
}

bool ConsistencyManager::wait_for_sync(const std::string& path,
                                     std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    
    while (std::chrono::steady_clock::now() < deadline) {
        if (verify_consistency(path)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return false;
}

void ConsistencyManager::log_consistency_violation(const std::string& path,
                                                const std::string& reason) {
    LOG_ERROR("Consistency violation for {}: {}", path, reason);
}

} // namespace fuse_t 
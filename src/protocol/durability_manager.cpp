#include "durability_manager.hpp"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

namespace fused {

DurabilityManager::DurabilityManager(TransactionLog& txn_log, 
                                   OperationJournal& journal)
    : txn_log_(txn_log)
    , journal_(journal) {
    // Start sync thread
    sync_thread_ = std::thread([this]() {
        run_sync_loop();
    });
}

DurabilityManager::~DurabilityManager() {
    running_ = false;
    if (sync_thread_.joinable()) {
        sync_thread_.join();
    }
}

bool DurabilityManager::initialize() {
    LOG_INFO("Initializing durability manager");
    return true;
}

bool DurabilityManager::enforce_durability(const std::string& path,
                                         const DurabilityGuarantee& guarantee) {
    // Create sync point if needed
    uint64_t sync_id = 0;
    if (guarantee.sync_point != SyncPoint::LAZY) {
        sync_id = create_sync_point(path) ? next_sync_id_ - 1 : 0;
        if (sync_id == 0) {
            return false;
        }
    }

    // Handle based on sync point type
    switch (guarantee.sync_point) {
        case SyncPoint::IMMEDIATE:
            // Immediate sync
            if (!sync_file_state(path)) {
                return false;
            }
            if (guarantee.require_verification && !verify_file_state(path)) {
                return false;
            }
            break;

        case SyncPoint::TRANSACTION:
            // Wait for transaction boundary
            if (!wait_for_sync(sync_id)) {
                return false;
            }
            break;

        case SyncPoint::PERIODIC:
            // Will be handled by sync thread
            break;

        case SyncPoint::LAZY:
            // No immediate action needed
            break;
    }

    // Perform fsync if required
    if (guarantee.require_fsync) {
        if (!fsync_path(path)) {
            return false;
        }
    }

    return true;
}

bool DurabilityManager::wait_for_durability(const std::string& path,
                                          std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    
    while (std::chrono::steady_clock::now() < deadline) {
        if (verify_durability(path)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return false;
}

bool DurabilityManager::verify_durability(const std::string& path) {
    // Verify file state
    if (!verify_file_state(path)) {
        return false;
    }

    // Verify write ordering
    if (!verify_write_order(path)) {
        return false;
    }

    return true;
}

bool DurabilityManager::create_sync_point(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    uint64_t sync_id = next_sync_id_++;
    
    SyncState state{
        sync_id,
        path,
        std::chrono::steady_clock::now(),
        false,  // not synced
        false,  // not verified
        0       // no attempts yet
    };

    sync_points_[sync_id] = std::move(state);
    return true;
}

bool DurabilityManager::wait_for_sync(uint64_t sync_id) {
    auto deadline = std::chrono::steady_clock::now() + 
                   std::chrono::seconds(5);  // 5 second timeout
    
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = sync_points_.find(sync_id);
            if (it != sync_points_.end() && it->second.synced && it->second.verified) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return false;
}

bool DurabilityManager::verify_sync_point(uint64_t sync_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sync_points_.find(sync_id);
    if (it == sync_points_.end()) {
        return false;
    }

    if (!verify_file_state(it->second.path)) {
        return false;
    }

    it->second.verified = true;
    return true;
}

bool DurabilityManager::order_write(const std::string& path, uint64_t sequence_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    WriteOrder order{
        sequence_id,
        std::chrono::steady_clock::now(),
        false
    };

    write_orders_[path].push_back(std::move(order));
    return true;
}

bool DurabilityManager::verify_write_order(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = write_orders_.find(path);
    if (it == write_orders_.end()) {
        return true;  // No writes to verify
    }

    // Verify writes are ordered by sequence ID
    const auto& writes = it->second;
    for (size_t i = 1; i < writes.size(); i++) {
        if (writes[i].sequence_id <= writes[i-1].sequence_id) {
            return false;
        }
    }

    return true;
}

bool DurabilityManager::wait_for_ordered_writes(const std::string& path) {
    auto deadline = std::chrono::steady_clock::now() + 
                   std::chrono::seconds(5);  // 5 second timeout
    
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = write_orders_.find(path);
            if (it == write_orders_.end()) {
                return true;
            }

            bool all_completed = std::all_of(it->second.begin(), it->second.end(),
                [](const WriteOrder& order) { return order.completed; });
            
            if (all_completed) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return false;
}

bool DurabilityManager::fsync_path(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }

    bool success = fsync(fd) == 0;
    close(fd);
    return success;
}

bool DurabilityManager::fsync_directory(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }

    bool success = fsync(fd) == 0;
    close(fd);
    return success;
}

bool DurabilityManager::fsync_metadata(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }

    // Sync parent directory to ensure metadata durability
    size_t last_slash = path.find_last_of('/');
    if (last_slash != std::string::npos) {
        std::string parent = path.substr(0, last_slash);
        if (!fsync_directory(parent)) {
            return false;
        }
    }

    return true;
}

void DurabilityManager::run_sync_loop() {
    while (running_) {
        process_sync_points();
        cleanup_expired_points();
        std::this_thread::sleep_for(SYNC_INTERVAL);
    }
}

void DurabilityManager::process_sync_points() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& [id, point] : sync_points_) {
        if (!point.synced) {
            if (sync_file_state(point.path)) {
                point.synced = true;
                point.attempts++;
            } else if (point.attempts >= MAX_SYNC_ATTEMPTS) {
                handle_sync_failure(point.path, "Max sync attempts reached");
            }
        }

        if (point.synced && !point.verified) {
            verify_sync_point(id);
        }
    }
}

void DurabilityManager::cleanup_expired_points() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    
    for (auto it = sync_points_.begin(); it != sync_points_.end();) {
        if (now - it->second.timestamp > std::chrono::seconds(60)) {  // 1 minute timeout
            it = sync_points_.erase(it);
        } else {
            ++it;
        }
    }

    // Also cleanup completed write orders
    for (auto it = write_orders_.begin(); it != write_orders_.end();) {
        auto& writes = it->second;
        writes.erase(
            std::remove_if(writes.begin(), writes.end(),
                [&now](const WriteOrder& order) {
                    return order.completed && 
                           now - order.timestamp > std::chrono::seconds(60);
                }),
            writes.end()
        );

        if (writes.empty()) {
            it = write_orders_.erase(it);
        } else {
            ++it;
        }
    }
}

bool DurabilityManager::sync_file_state(const std::string& path) {
    // Sync file content
    if (!fsync_path(path)) {
        return false;
    }

    // Sync metadata
    if (!fsync_metadata(path)) {
        return false;
    }

    return true;
}

bool DurabilityManager::verify_file_state(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }

    // TODO: Add more state verification
    return true;
}

bool DurabilityManager::handle_sync_failure(const std::string& path,
                                          const std::string& error) {
    LOG_ERROR("Sync failure for {}: {}", path, error);
    return retry_sync(next_sync_id_++);
}

bool DurabilityManager::retry_sync(uint64_t sync_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sync_points_.find(sync_id);
    if (it == sync_points_.end()) {
        return false;
    }

    if (it->second.attempts >= MAX_SYNC_ATTEMPTS) {
        return false;
    }

    it->second.attempts++;
    return sync_file_state(it->second.path);
}

} // namespace fuse_t 
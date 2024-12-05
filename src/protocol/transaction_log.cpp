#include "transaction_log.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "xdr.hpp"

namespace fused {

TransactionLog::TransactionLog(const std::string& log_path)
    : log_path_(log_path) {
    // Open log file with append and sync flags
    log_fd_ = open(log_path_.c_str(), O_CREAT | O_RDWR | O_APPEND | O_SYNC, 0644);
    if (log_fd_ < 0) {
        LOG_ERROR("Failed to open transaction log: {}", log_path_);
        throw std::runtime_error("Failed to open transaction log");
    }

    // Recover any pending transactions
    recover_from_log();
}

TransactionLog::~TransactionLog() {
    if (log_fd_ >= 0) {
        close(log_fd_);
    }
}

uint64_t TransactionLog::begin_transaction(NFSProcedure proc, 
                                         const std::vector<uint8_t>& args) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    uint64_t txn_id = next_txn_id_++;
    
    // Create transaction entry
    TransactionEntry entry{
        txn_id,
        proc,
        args,
        {},  // pre_state will be set later
        std::chrono::steady_clock::now(),
        false,  // not committed
        false   // not synced
    };
    
    // Write to log
    if (!write_entry(entry)) {
        LOG_ERROR("Failed to write transaction entry for txn {}", txn_id);
        return 0;
    }
    
    // Store in memory
    active_transactions_[txn_id] = std::move(entry);
    
    LOG_DEBUG("Started transaction {}", txn_id);
    return txn_id;
}

bool TransactionLog::commit_transaction(uint64_t txn_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_transactions_.find(txn_id);
    if (it == active_transactions_.end()) {
        LOG_ERROR("Transaction {} not found", txn_id);
        return false;
    }
    
    // Mark as committed
    it->second.committed = true;
    
    // Write updated entry
    if (!write_entry(it->second)) {
        LOG_ERROR("Failed to write commit record for txn {}", txn_id);
        return false;
    }
    
    // Sync to disk
    if (!sync_to_disk()) {
        LOG_ERROR("Failed to sync commit for txn {}", txn_id);
        return false;
    }
    
    // Remove from active transactions
    active_transactions_.erase(it);
    
    LOG_DEBUG("Committed transaction {}", txn_id);
    return true;
}

bool TransactionLog::rollback_transaction(uint64_t txn_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_transactions_.find(txn_id);
    if (it == active_transactions_.end()) {
        LOG_ERROR("Transaction {} not found", txn_id);
        return false;
    }
    
    // If we have pre-state, restore it
    if (!it->second.pre_state.empty()) {
        // TODO: Implement state restoration
        LOG_DEBUG("Restoring pre-state for txn {}", txn_id);
    }
    
    // Remove transaction
    active_transactions_.erase(it);
    
    LOG_DEBUG("Rolled back transaction {}", txn_id);
    return true;
}

bool TransactionLog::save_pre_state(uint64_t txn_id, const std::vector<uint8_t>& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_transactions_.find(txn_id);
    if (it == active_transactions_.end()) {
        LOG_ERROR("Transaction {} not found", txn_id);
        return false;
    }
    
    // Save pre-state
    it->second.pre_state = state;
    
    // Write updated entry
    if (!write_entry(it->second)) {
        LOG_ERROR("Failed to write pre-state for txn {}", txn_id);
        return false;
    }
    
    return true;
}

bool TransactionLog::sync_to_disk() {
    return fsync(log_fd_) == 0;
}

bool TransactionLog::recover_from_log() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Seek to start of log
    if (lseek(log_fd_, 0, SEEK_SET) < 0) {
        LOG_ERROR("Failed to seek to start of log");
        return false;
    }
    
    // Read and replay transactions
    TransactionEntry entry;
    uint64_t max_txn_id = 0;
    
    while (read_entry(entry)) {
        // Track highest transaction ID
        max_txn_id = std::max(max_txn_id, entry.transaction_id);
        
        if (!entry.committed) {
            // Uncommitted transaction - needs recovery
            if (verify_transaction(entry)) {
                if (replay_transaction(entry)) {
                    LOG_INFO("Recovered transaction {}", entry.transaction_id);
                } else {
                    LOG_ERROR("Failed to recover transaction {}", entry.transaction_id);
                }
            } else {
                LOG_ERROR("Failed to verify transaction {}", entry.transaction_id);
            }
        }
    }
    
    // Update next transaction ID
    next_txn_id_ = max_txn_id + 1;
    
    // Truncate log to remove recovered transactions
    return truncate_log();
}

std::vector<TransactionEntry> TransactionLog::get_uncommitted_transactions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<TransactionEntry> result;
    for (const auto& [txn_id, entry] : active_transactions_) {
        if (!entry.committed) {
            result.push_back(entry);
        }
    }
    return result;
}

bool TransactionLog::write_entry(const TransactionEntry& entry) {
    // Encode entry
    XDREncoder encoder;
    encoder.encode(entry.transaction_id);
    encoder.encode(static_cast<uint32_t>(entry.procedure));
    encoder.encode_opaque(entry.arguments);
    encoder.encode_opaque(entry.pre_state);
    encoder.encode(static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            entry.timestamp.time_since_epoch()).count()));
    encoder.encode(entry.committed);
    encoder.encode(entry.synced);
    
    auto buffer = encoder.get_buffer();
    
    // Write length prefix
    uint32_t length = buffer.size();
    if (write(log_fd_, &length, sizeof(length)) != sizeof(length)) {
        return false;
    }
    
    // Write entry data
    return write(log_fd_, buffer.data(), buffer.size()) == 
        static_cast<ssize_t>(buffer.size());
}

bool TransactionLog::read_entry(TransactionEntry& entry) {
    // Read length prefix
    uint32_t length;
    if (read(log_fd_, &length, sizeof(length)) != sizeof(length)) {
        if (errno == 0) {  // EOF
            return false;
        }
        LOG_ERROR("Failed to read entry length: {}", strerror(errno));
        return false;
    }
    
    // Read entry data
    std::vector<uint8_t> buffer(length);
    if (read(log_fd_, buffer.data(), length) != static_cast<ssize_t>(length)) {
        LOG_ERROR("Failed to read entry data: {}", strerror(errno));
        return false;
    }
    
    // Decode entry
    try {
        XDRDecoder decoder(buffer.data(), buffer.size());
        entry.transaction_id = decoder.decode_uint64();
        entry.procedure = static_cast<NFSProcedure>(decoder.decode_uint32());
        entry.arguments = decoder.decode_opaque();
        entry.pre_state = decoder.decode_opaque();
        uint64_t timestamp = decoder.decode_uint64();
        entry.timestamp = std::chrono::steady_clock::time_point(
            std::chrono::microseconds(timestamp));
        entry.committed = decoder.decode_bool();
        entry.synced = decoder.decode_bool();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to decode entry: {}", e.what());
        return false;
    }
}

bool TransactionLog::truncate_log() {
    // Write all active transactions
    for (const auto& [txn_id, entry] : active_transactions_) {
        if (!write_entry(entry)) {
            LOG_ERROR("Failed to write entry during truncation");
            return false;
        }
    }
    
    // Truncate to current position
    off_t pos = lseek(log_fd_, 0, SEEK_CUR);
    if (pos < 0 || ftruncate(log_fd_, pos) < 0) {
        LOG_ERROR("Failed to truncate log");
        return false;
    }
    
    return sync_to_disk();
}

bool TransactionLog::replay_transaction(const TransactionEntry& entry) {
    LOG_INFO("Replaying transaction {}: procedure {}", 
        entry.transaction_id, static_cast<int>(entry.procedure));

    try {
        XDRDecoder decoder(entry.arguments.data(), entry.arguments.size());
        
        switch (entry.procedure) {
            case NFSProcedure::WRITE: {
                NFSFileHandle handle;
                uint64_t offset;
                std::vector<uint8_t> data;
                decoder.decode_opaque(handle.handle);
                decoder.decode(offset);
                decoder.decode_opaque(data);

                // Get file path
                std::string path = translate_handle_to_path(handle.handle);
                if (path.empty()) {
                    LOG_ERROR("Invalid file handle in transaction {}", entry.transaction_id);
                    return false;
                }

                // Open file and write data
                int fd = open(path.c_str(), O_WRONLY);
                if (fd < 0) {
                    LOG_ERROR("Failed to open file for replay: {}", path);
                    return false;
                }

                if (lseek(fd, offset, SEEK_SET) < 0) {
                    LOG_ERROR("Failed to seek to offset {} in file {}", offset, path);
                    close(fd);
                    return false;
                }

                ssize_t written = write(fd, data.data(), data.size());
                fsync(fd);  // Ensure durability
                close(fd);

                if (written != static_cast<ssize_t>(data.size())) {
                    LOG_ERROR("Failed to write data during replay");
                    return false;
                }
                break;
            }

            case NFSProcedure::CREATE: {
                NFSFileHandle dir_handle;
                std::string name;
                uint32_t mode;
                decoder.decode_opaque(dir_handle.handle);
                decoder.decode_string(name);
                decoder.decode(mode);

                // Get directory path
                std::string dir_path = translate_handle_to_path(dir_handle.handle);
                if (dir_path.empty()) {
                    LOG_ERROR("Invalid directory handle in transaction {}", entry.transaction_id);
                    return false;
                }

                // Create file
                std::string file_path = dir_path + "/" + name;
                int fd = open(file_path.c_str(), O_CREAT | O_WRONLY | O_EXCL, mode);
                if (fd < 0) {
                    if (errno == EEXIST) {
                        // File already exists - this is okay for idempotency
                        LOG_INFO("File {} already exists during replay", file_path);
                        return true;
                    }
                    LOG_ERROR("Failed to create file during replay: {}", file_path);
                    return false;
                }
                close(fd);
                break;
            }

            case NFSProcedure::REMOVE: {
                NFSFileHandle dir_handle;
                std::string name;
                decoder.decode_opaque(dir_handle.handle);
                decoder.decode_string(name);

                std::string dir_path = translate_handle_to_path(dir_handle.handle);
                if (dir_path.empty()) {
                    LOG_ERROR("Invalid directory handle in transaction {}", entry.transaction_id);
                    return false;
                }

                std::string file_path = dir_path + "/" + name;
                if (unlink(file_path.c_str()) != 0 && errno != ENOENT) {
                    // ENOENT is okay - file already removed
                    LOG_ERROR("Failed to remove file during replay: {}", file_path);
                    return false;
                }
                break;
            }

            // Add more procedure replay implementations...

            default:
                LOG_ERROR("Unsupported procedure for replay: {}", 
                    static_cast<int>(entry.procedure));
                return false;
        }

        LOG_INFO("Successfully replayed transaction {}", entry.transaction_id);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to replay transaction {}: {}", entry.transaction_id, e.what());
        return false;
    }
}

bool TransactionLog::verify_transaction(const TransactionEntry& entry) {
    // Basic validity checks
    if (entry.transaction_id == 0 || entry.arguments.empty()) {
        LOG_ERROR("Invalid transaction {}: missing ID or arguments", entry.transaction_id);
        return false;
    }

    // Check timestamp is reasonable
    auto now = std::chrono::steady_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::hours>(now - entry.timestamp);
    if (age > std::chrono::hours(24)) {
        LOG_ERROR("Transaction {} is too old: {} hours", entry.transaction_id, age.count());
        return false;
    }

    // Verify procedure is valid
    switch (entry.procedure) {
        case NFSProcedure::WRITE:
        case NFSProcedure::CREATE:
        case NFSProcedure::REMOVE:
        case NFSProcedure::RENAME:
        case NFSProcedure::SETATTR:
            break;
        default:
            LOG_ERROR("Transaction {} has invalid procedure: {}", 
                entry.transaction_id, static_cast<int>(entry.procedure));
            return false;
    }

    // Verify arguments format based on procedure
    try {
        XDRDecoder decoder(entry.arguments.data(), entry.arguments.size());
        
        switch (entry.procedure) {
            case NFSProcedure::WRITE: {
                NFSFileHandle handle;
                uint64_t offset;
                std::vector<uint8_t> data;
                if (!decoder.decode_opaque(handle.handle) ||
                    !decoder.decode(offset) ||
                    !decoder.decode_opaque(data)) {
                    throw std::runtime_error("Invalid WRITE arguments");
                }
                break;
            }
            case NFSProcedure::CREATE: {
                NFSFileHandle dir_handle;
                std::string name;
                uint32_t mode;
                if (!decoder.decode_opaque(dir_handle.handle) ||
                    !decoder.decode_string(name) ||
                    !decoder.decode(mode)) {
                    throw std::runtime_error("Invalid CREATE arguments");
                }
                break;
            }
            // Add more procedure verifications...
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to verify transaction {} arguments: {}", 
            entry.transaction_id, e.what());
        return false;
    }

    // If pre-state exists, verify it's valid
    if (!entry.pre_state.empty()) {
        try {
            XDRDecoder decoder(entry.pre_state.data(), entry.pre_state.size());
            // Verify pre-state format based on procedure
            // TODO: Add procedure-specific pre-state verification
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to verify transaction {} pre-state: {}", 
                entry.transaction_id, e.what());
            return false;
        }
    }

    return true;
}

} // namespace fuse_t
#include "operation_journal.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "xdr.hpp"

namespace fused {

OperationJournal::OperationJournal(TransactionLog& txn_log)
    : txn_log_(txn_log) {
}

OperationJournal::~OperationJournal() {
    if (journal_fd_ >= 0) {
        close(journal_fd_);
    }
}

bool OperationJournal::initialize(const std::string& journal_path) {
    journal_path_ = journal_path;
    
    // Open journal file with append and sync flags
    journal_fd_ = open(journal_path_.c_str(), O_CREAT | O_RDWR | O_APPEND | O_SYNC, 0644);
    if (journal_fd_ < 0) {
        LOG_ERROR("Failed to open operation journal: {}", journal_path_);
        return false;
    }

    // Recover any incomplete operations
    if (!recover_journal()) {
        LOG_ERROR("Failed to recover operation journal");
        return false;
    }

    LOG_INFO("Operation journal initialized: {}", journal_path_);
    return true;
}

uint64_t OperationJournal::append_operation(NFSProcedure proc,
                                          const std::vector<uint8_t>& args,
                                          const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    uint64_t sequence_id = next_sequence_id_++;
    
    // Start transaction in transaction log
    uint64_t txn_id = txn_log_.begin_transaction(proc, args);
    if (txn_id == 0) {
        LOG_ERROR("Failed to begin transaction for operation {}", sequence_id);
        return 0;
    }
    
    // Create journal entry
    JournalEntry entry{
        sequence_id,
        txn_id,
        proc,
        args,
        {},  // dependencies will be added later
        path,
        std::chrono::steady_clock::now(),
        false,  // not completed
        NFSStatus::OK
    };
    
    // Write to journal
    if (!write_entry(entry)) {
        LOG_ERROR("Failed to write journal entry for operation {}", sequence_id);
        txn_log_.rollback_transaction(txn_id);
        return 0;
    }
    
    // Store in memory
    active_operations_[sequence_id] = std::move(entry);
    
    LOG_DEBUG("Appended operation {} (txn {})", sequence_id, txn_id);
    return sequence_id;
}

bool OperationJournal::complete_operation(uint64_t sequence_id, NFSStatus result) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_operations_.find(sequence_id);
    if (it == active_operations_.end()) {
        LOG_ERROR("Operation {} not found", sequence_id);
        return false;
    }
    
    // Mark as completed
    it->second.completed = true;
    it->second.result = result;
    
    // Write updated entry
    if (!write_entry(it->second)) {
        LOG_ERROR("Failed to write completion record for operation {}", sequence_id);
        return false;
    }
    
    // Complete transaction
    if (result == NFSStatus::OK) {
        if (!txn_log_.commit_transaction(it->second.transaction_id)) {
            LOG_ERROR("Failed to commit transaction {} for operation {}", 
                it->second.transaction_id, sequence_id);
            return false;
        }
    } else {
        if (!txn_log_.rollback_transaction(it->second.transaction_id)) {
            LOG_ERROR("Failed to rollback transaction {} for operation {}", 
                it->second.transaction_id, sequence_id);
            return false;
        }
    }
    
    // Remove from active operations
    active_operations_.erase(it);
    
    LOG_DEBUG("Completed operation {} with result {}", 
        sequence_id, static_cast<int>(result));
    return true;
}

void OperationJournal::add_dependency(uint64_t sequence_id, uint64_t depends_on) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_operations_.find(sequence_id);
    if (it != active_operations_.end()) {
        // Add dependency if not already present
        auto& deps = it->second.dependencies;
        if (std::find(deps.begin(), deps.end(), depends_on) == deps.end()) {
            deps.push_back(depends_on);
            
            // Write updated entry
            if (!write_entry(it->second)) {
                LOG_ERROR("Failed to write dependency for operation {}", sequence_id);
            }
            
            LOG_DEBUG("Added dependency: {} depends on {}", sequence_id, depends_on);
        }
    }
}

bool OperationJournal::check_dependencies(uint64_t sequence_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_operations_.find(sequence_id);
    if (it == active_operations_.end()) {
        return false;
    }
    
    // Check if all dependencies are completed
    for (uint32_t dep_id : it->second.dependencies) {
        auto dep_it = active_operations_.find(dep_id);
        if (dep_it != active_operations_.end() && !dep_it->second.completed) {
            LOG_DEBUG("Dependency {} not satisfied for operation {}", dep_id, sequence_id);
            return false;
        }
    }
    
    return true;
}

std::vector<uint64_t> OperationJournal::get_dependencies(uint64_t sequence_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_operations_.find(sequence_id);
    if (it != active_operations_.end()) {
        return std::vector<uint64_t>(it->second.dependencies.begin(),
                                   it->second.dependencies.end());
    }
    return {};
}

bool OperationJournal::begin_state_transition(uint64_t sequence_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_operations_.find(sequence_id);
    if (it == active_operations_.end()) {
        LOG_ERROR("Operation {} not found", sequence_id);
        return false;
    }
    
    // Save pre-state
    if (!save_pre_state(sequence_id)) {
        LOG_ERROR("Failed to save pre-state for operation {}", sequence_id);
        return false;
    }
    
    return true;
}

bool OperationJournal::commit_state_transition(uint64_t sequence_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_operations_.find(sequence_id);
    if (it == active_operations_.end()) {
        LOG_ERROR("Operation {} not found", sequence_id);
        return false;
    }
    
    // Verify state transition
    if (!verify_state_transition(sequence_id)) {
        LOG_ERROR("State transition verification failed for operation {}", sequence_id);
        return false;
    }
    
    return complete_operation(sequence_id, NFSStatus::OK);
}

bool OperationJournal::rollback_state_transition(uint64_t sequence_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_operations_.find(sequence_id);
    if (it == active_operations_.end()) {
        LOG_ERROR("Operation {} not found", sequence_id);
        return false;
    }
    
    // Rollback transaction will restore pre-state
    return complete_operation(sequence_id, NFSStatus::SERVERFAULT);
}

bool OperationJournal::recover_journal() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Seek to start of journal
    if (lseek(journal_fd_, 0, SEEK_SET) < 0) {
        LOG_ERROR("Failed to seek to start of journal");
        return false;
    }
    
    // Read and verify entries
    JournalEntry entry;
    uint64_t max_sequence_id = 0;
    std::vector<JournalEntry> incomplete_ops;
    
    while (read_entry(entry)) {
        max_sequence_id = std::max(max_sequence_id, entry.sequence_id);
        
        if (!entry.completed) {
            if (verify_operation(entry)) {
                incomplete_ops.push_back(entry);
            }
        }
    }
    
    // Update next sequence ID
    next_sequence_id_ = max_sequence_id + 1;
    
    // Sort operations by dependencies
    std::sort(incomplete_ops.begin(), incomplete_ops.end(),
        [this](const JournalEntry& a, const JournalEntry& b) {
            return check_operation_order(a, b);
        });
    
    // Replay incomplete operations
    for (const auto& op : incomplete_ops) {
        if (replay_operation(op)) {
            LOG_INFO("Recovered operation {}", op.sequence_id);
        } else {
            LOG_ERROR("Failed to recover operation {}", op.sequence_id);
        }
    }
    
    // Truncate journal
    return truncate_journal();
}

std::vector<JournalEntry> OperationJournal::get_incomplete_operations() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<JournalEntry> result;
    for (const auto& [seq_id, entry] : active_operations_) {
        if (!entry.completed) {
            result.push_back(entry);
        }
    }
    return result;
}

bool OperationJournal::write_entry(const JournalEntry& entry) {
    // Encode entry
    XDREncoder encoder;
    encoder.encode(entry.sequence_id);
    encoder.encode(entry.transaction_id);
    encoder.encode(static_cast<uint32_t>(entry.procedure));
    encoder.encode_opaque(entry.arguments);
    encoder.encode_string(entry.target_path);
    
    // Encode dependencies
    encoder.encode(static_cast<uint32_t>(entry.dependencies.size()));
    for (uint32_t dep : entry.dependencies) {
        encoder.encode(dep);
    }
    
    // Encode timestamp and status
    encoder.encode(static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            entry.timestamp.time_since_epoch()).count()));
    encoder.encode(entry.completed);
    encoder.encode(static_cast<uint32_t>(entry.result));
    
    auto buffer = encoder.get_buffer();
    
    // Write length prefix
    uint32_t length = buffer.size();
    if (write(journal_fd_, &length, sizeof(length)) != sizeof(length)) {
        return false;
    }
    
    // Write entry data
    return write(journal_fd_, buffer.data(), buffer.size()) == 
        static_cast<ssize_t>(buffer.size());
}

bool OperationJournal::read_entry(JournalEntry& entry) {
    // Read length prefix
    uint32_t length;
    if (read(journal_fd_, &length, sizeof(length)) != sizeof(length)) {
        if (errno == 0) {  // EOF
            return false;
        }
        LOG_ERROR("Failed to read entry length: {}", strerror(errno));
        return false;
    }
    
    // Read entry data
    std::vector<uint8_t> buffer(length);
    if (read(journal_fd_, buffer.data(), length) != static_cast<ssize_t>(length)) {
        LOG_ERROR("Failed to read entry data: {}", strerror(errno));
        return false;
    }
    
    // Decode entry
    try {
        XDRDecoder decoder(buffer.data(), buffer.size());
        entry.sequence_id = decoder.decode_uint64();
        entry.transaction_id = decoder.decode_uint64();
        entry.procedure = static_cast<NFSProcedure>(decoder.decode_uint32());
        entry.arguments = decoder.decode_opaque();
        entry.target_path = decoder.decode_string();
        
        // Decode dependencies
        uint32_t dep_count = decoder.decode_uint32();
        entry.dependencies.resize(dep_count);
        for (uint32_t i = 0; i < dep_count; i++) {
            entry.dependencies[i] = decoder.decode_uint32();
        }
        
        // Decode timestamp and status
        uint64_t timestamp = decoder.decode_uint64();
        entry.timestamp = std::chrono::steady_clock::time_point(
            std::chrono::microseconds(timestamp));
        entry.completed = decoder.decode_bool();
        entry.result = static_cast<NFSStatus>(decoder.decode_uint32());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to decode entry: {}", e.what());
        return false;
    }
}

bool OperationJournal::truncate_journal() {
    // Write all active operations
    for (const auto& [seq_id, entry] : active_operations_) {
        if (!write_entry(entry)) {
            LOG_ERROR("Failed to write entry during truncation");
            return false;
        }
    }
    
    // Truncate to current position
    off_t pos = lseek(journal_fd_, 0, SEEK_CUR);
    if (pos < 0 || ftruncate(journal_fd_, pos) < 0) {
        LOG_ERROR("Failed to truncate journal");
        return false;
    }
    
    return fsync(journal_fd_) == 0;
}

bool OperationJournal::save_pre_state(uint64_t sequence_id) {
    auto it = active_operations_.find(sequence_id);
    if (it == active_operations_.end()) {
        return false;
    }
    
    // Get current state based on operation type
    std::vector<uint8_t> pre_state;
    const auto& entry = it->second;
    
    try {
        switch (entry.procedure) {
            case NFSProcedure::WRITE: {
                // Save current file content for the affected region
                XDRDecoder decoder(entry.arguments.data(), entry.arguments.size());
                NFSFileHandle handle;
                uint64_t offset;
                uint32_t length;
                decoder.decode_opaque(handle.handle);
                decoder.decode(offset);
                decoder.decode(length);
                
                // Read current content
                int fd = open(entry.target_path.c_str(), O_RDONLY);
                if (fd < 0) return false;
                
                if (lseek(fd, offset, SEEK_SET) < 0) {
                    close(fd);
                    return false;
                }
                
                pre_state.resize(length);
                ssize_t bytes_read = read(fd, pre_state.data(), length);
                close(fd);
                
                if (bytes_read < 0) return false;
                break;
            }
            
            case NFSProcedure::CREATE:
                // Nothing to save for create
                break;
                
            case NFSProcedure::REMOVE: {
                // Save file attributes and content for restore
                struct stat st;
                if (stat(entry.target_path.c_str(), &st) != 0) {
                    return false;
                }
                
                XDREncoder encoder;
                encoder.encode(st.st_mode);
                encoder.encode(st.st_uid);
                encoder.encode(st.st_gid);
                
                // Save file content if it's a regular file
                if (S_ISREG(st.st_mode)) {
                    int fd = open(entry.target_path.c_str(), O_RDONLY);
                    if (fd < 0) return false;
                    
                    pre_state.resize(st.st_size);
                    ssize_t bytes_read = read(fd, pre_state.data(), st.st_size);
                    close(fd);
                    
                    if (bytes_read < 0) return false;
                    encoder.encode_opaque(pre_state);
                }
                
                pre_state = encoder.get_buffer();
                break;
            }
            
            // Add more cases as needed...
        }
        
        // Save pre-state in transaction log
        return txn_log_.save_pre_state(entry.transaction_id, pre_state);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to save pre-state: {}", e.what());
        return false;
    }
}

bool OperationJournal::verify_state_transition(uint64_t sequence_id) {
    auto it = active_operations_.find(sequence_id);
    if (it == active_operations_.end()) {
        return false;
    }
    
    // Verify based on operation type
    const auto& entry = it->second;
    struct stat st;
    
    switch (entry.procedure) {
        case NFSProcedure::WRITE:
            // Verify file exists and is writable
            if (stat(entry.target_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
                return false;
            }
            break;
            
        case NFSProcedure::CREATE:
            // Verify parent directory exists and is writable
            {
                size_t slash = entry.target_path.find_last_of('/');
                if (slash == std::string::npos) return false;
                std::string parent = entry.target_path.substr(0, slash);
                if (stat(parent.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
                    return false;
                }
            }
            break;
            
        case NFSProcedure::REMOVE:
            // Verify file exists
            if (stat(entry.target_path.c_str(), &st) != 0) {
                return false;
            }
            break;
    }
    
    return true;
}

bool OperationJournal::verify_operation(const JournalEntry& entry) {
    // Basic validity checks
    if (entry.sequence_id == 0 || entry.transaction_id == 0 || 
        entry.arguments.empty() || entry.target_path.empty()) {
        LOG_ERROR("Invalid operation {}: missing required fields", entry.sequence_id);
        return false;
    }
    
    // Check timestamp is reasonable
    auto now = std::chrono::steady_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::hours>(now - entry.timestamp);
    if (age > std::chrono::hours(24)) {
        LOG_ERROR("Operation {} is too old: {} hours", entry.sequence_id, age.count());
        return false;
    }
    
    // Verify dependencies exist and are valid
    for (uint32_t dep_id : entry.dependencies) {
        if (dep_id >= entry.sequence_id) {
            LOG_ERROR("Invalid dependency {} for operation {}", dep_id, entry.sequence_id);
            return false;
        }
    }
    
    return true;
}

bool OperationJournal::check_operation_order(const JournalEntry& op1, 
                                           const JournalEntry& op2) const {
    // If operations have explicit dependency, respect it
    if (std::find(op2.dependencies.begin(), op2.dependencies.end(), 
                  op1.sequence_id) != op2.dependencies.end()) {
        return true;
    }
    
    // If operations are on same path, maintain temporal order
    if (op1.target_path == op2.target_path) {
        return op1.timestamp < op2.timestamp;
    }
    
    // Otherwise, operations can be reordered
    return true;
}

} // namespace fuse_t
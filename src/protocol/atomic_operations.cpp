#include "atomic_operations.hpp"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

namespace fused {

AtomicOperationHandler::AtomicOperationHandler(TransactionLog& txn_log, 
                                             OperationJournal& journal)
    : txn_log_(txn_log)
    , journal_(journal) {
}

AtomicOperationHandler::~AtomicOperationHandler() = default;

bool AtomicOperationHandler::initialize() {
    LOG_INFO("Initializing atomic operation handler");
    return true;
}

uint64_t AtomicOperationHandler::begin_atomic_operation(NFSProcedure proc,
                                                      const std::vector<uint8_t>& args,
                                                      const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!in_transaction_) {
        LOG_ERROR("Cannot begin atomic operation outside transaction");
        return 0;
    }

    uint64_t op_id = next_op_id_++;
    
    AtomicOperation op{
        op_id,
        proc,
        args,
        path,
        {},  // pre_state will be set later
        false,  // not completed
        false,  // not rolled back
        std::chrono::steady_clock::now()
    };

    // Save pre-state
    if (!save_pre_state(op)) {
        LOG_ERROR("Failed to save pre-state for operation {}", op_id);
        return 0;
    }

    active_operations_[op_id] = std::move(op);
    LOG_DEBUG("Started atomic operation {}", op_id);
    return op_id;
}

bool AtomicOperationHandler::commit_operation(uint64_t op_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_operations_.find(op_id);
    if (it == active_operations_.end()) {
        LOG_ERROR("Operation {} not found", op_id);
        return false;
    }

    // Execute operation
    if (!execute_operation(it->second)) {
        LOG_ERROR("Failed to execute operation {}", op_id);
        return false;
    }

    // Verify post-state
    if (!verify_post_state(it->second)) {
        LOG_ERROR("Post-state verification failed for operation {}", op_id);
        return rollback_operation(op_id);
    }

    it->second.completed = true;
    LOG_DEBUG("Committed operation {}", op_id);
    return true;
}

bool AtomicOperationHandler::rollback_operation(uint64_t op_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_operations_.find(op_id);
    if (it == active_operations_.end()) {
        LOG_ERROR("Operation {} not found", op_id);
        return false;
    }

    // Restore pre-state
    if (!rollback_to_pre_state(it->second)) {
        LOG_ERROR("Failed to rollback operation {}", op_id);
        return false;
    }

    // Verify rollback state
    if (!verify_rollback_state(it->second)) {
        LOG_ERROR("Rollback state verification failed for operation {}", op_id);
        return false;
    }

    it->second.rolled_back = true;
    LOG_DEBUG("Rolled back operation {}", op_id);
    return true;
}

bool AtomicOperationHandler::begin_transaction() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (in_transaction_) {
        LOG_ERROR("Transaction already in progress");
        return false;
    }

    in_transaction_ = true;
    LOG_DEBUG("Started new transaction");
    return true;
}

bool AtomicOperationHandler::commit_transaction() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!in_transaction_) {
        LOG_ERROR("No transaction in progress");
        return false;
    }

    // Verify all operations in transaction are completed
    for (const auto& [op_id, op] : active_operations_) {
        if (!op.completed && !op.rolled_back) {
            LOG_ERROR("Operation {} not completed", op_id);
            return false;
        }
    }

    in_transaction_ = false;
    active_operations_.clear();
    LOG_DEBUG("Committed transaction");
    return true;
}

bool AtomicOperationHandler::rollback_transaction() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!in_transaction_) {
        LOG_ERROR("No transaction in progress");
        return false;
    }

    // Rollback all operations in reverse order
    std::vector<uint64_t> ops;
    for (const auto& [op_id, _] : active_operations_) {
        ops.push_back(op_id);
    }

    bool success = true;
    for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
        if (!rollback_operation(*it)) {
            success = false;
        }
    }

    in_transaction_ = false;
    active_operations_.clear();
    LOG_DEBUG("Rolled back transaction");
    return success;
}

bool AtomicOperationHandler::save_operation_state(uint64_t op_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_operations_.find(op_id);
    if (it == active_operations_.end()) {
        return false;
    }

    return save_pre_state(it->second);
}

bool AtomicOperationHandler::verify_operation_state(uint64_t op_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_operations_.find(op_id);
    if (it == active_operations_.end()) {
        return false;
    }

    return verify_post_state(it->second);
}

bool AtomicOperationHandler::restore_operation_state(uint64_t op_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_operations_.find(op_id);
    if (it == active_operations_.end()) {
        return false;
    }

    return rollback_to_pre_state(it->second);
}

bool AtomicOperationHandler::is_operation_active(uint64_t op_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_operations_.find(op_id) != active_operations_.end();
}

bool AtomicOperationHandler::is_operation_completed(uint64_t op_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_operations_.find(op_id);
    return it != active_operations_.end() && it->second.completed;
}

bool AtomicOperationHandler::is_operation_rolled_back(uint64_t op_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_operations_.find(op_id);
    return it != active_operations_.end() && it->second.rolled_back;
}

// Private helper methods
bool AtomicOperationHandler::handle_operation_error(uint64_t op_id, 
                                                  const std::string& error) {
    LOG_ERROR("Operation {} error: {}", op_id, error);
    return attempt_error_recovery(op_id);
}

bool AtomicOperationHandler::attempt_error_recovery(uint64_t op_id) {
    return rollback_operation(op_id);
}

bool AtomicOperationHandler::verify_pre_state(const AtomicOperation& op) {
    // Verify pre-state exists
    if (op.pre_state.empty()) {
        return false;
    }

    // TODO: Add more pre-state verification
    return true;
}

bool AtomicOperationHandler::verify_post_state(const AtomicOperation& op) {
    struct stat st;
    if (stat(op.target_path.c_str(), &st) != 0) {
        return false;
    }

    // TODO: Add more post-state verification
    return true;
}

bool AtomicOperationHandler::verify_rollback_state(const AtomicOperation& op) {
    // Verify state matches pre-state
    // TODO: Add more rollback state verification
    return true;
}

bool AtomicOperationHandler::save_pre_state(AtomicOperation& op) {
    // Read current state
    struct stat st;
    if (stat(op.target_path.c_str(), &st) == 0) {
        // Save file content for regular files
        if (S_ISREG(st.st_mode)) {
            int fd = open(op.target_path.c_str(), O_RDONLY);
            if (fd >= 0) {
                std::vector<uint8_t> buffer(st.st_size);
                if (read(fd, buffer.data(), buffer.size()) == st.st_size) {
                    op.pre_state = std::move(buffer);
                }
                close(fd);
            }
        }
    }

    return !op.pre_state.empty();
}

bool AtomicOperationHandler::execute_operation(const AtomicOperation& op) {
    // Execute through journal
    return journal_.replay_operation(op.operation_id);
}

bool AtomicOperationHandler::rollback_to_pre_state(const AtomicOperation& op) {
    if (op.pre_state.empty()) {
        return false;
    }

    // Restore file content
    int fd = open(op.target_path.c_str(), O_WRONLY | O_TRUNC);
    if (fd < 0) {
        return false;
    }

    bool success = write(fd, op.pre_state.data(), op.pre_state.size()) == 
                  static_cast<ssize_t>(op.pre_state.size());
    close(fd);

    return success;
}

} // namespace fuse_t 
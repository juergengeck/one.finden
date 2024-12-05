#include "operation_log.hpp"
#include "xdr.hpp"
#include "util/replay_metrics.hpp"

namespace fused {

uint32_t OperationLog::log_operation(NFSProcedure proc, 
                                   const std::vector<uint8_t>& args) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    uint32_t op_id = next_op_id_++;
    LoggedOperation op{
        op_id,
        proc,
        args,
        std::chrono::steady_clock::now(),
        false,
        NFSStatus::OK
    };
    
    operations_[op_id] = std::move(op);
    return op_id;
}

void OperationLog::complete_operation(uint32_t op_id, NFSStatus result) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = operations_.find(op_id);
    if (it != operations_.end()) {
        it->second.completed = true;
        it->second.result = result;
    }
}

std::vector<LoggedOperation> OperationLog::get_incomplete_operations(
    const std::string& client_id,
    uint32_t session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<LoggedOperation> incomplete;
    for (const auto& [op_id, op] : operations_) {
        if (!op.completed) {
            incomplete.push_back(op);
        }
    }
    return sort_operations_for_replay(std::move(incomplete));
}

bool OperationLog::needs_replay(uint32_t op_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = operations_.find(op_id);
    if (it == operations_.end()) {
        return false;
    }
    
    // Only replay modifying operations that weren't completed
    return !it->second.completed && (
        it->second.procedure == NFSProcedure::WRITE ||
        it->second.procedure == NFSProcedure::CREATE ||
        it->second.procedure == NFSProcedure::REMOVE ||
        it->second.procedure == NFSProcedure::RENAME ||
        it->second.procedure == NFSProcedure::SETATTR
    );
}

NFSStatus OperationLog::replay_operation(const LoggedOperation& op) {
    auto start_time = std::chrono::steady_clock::now();
    bool success = false;
    
    // Verify dependencies before replay
    if (!check_dependencies(op.operation_id)) {
        std::string error = "Dependencies not satisfied";
        record_replay_attempt(op.operation_id, false, error);
        get_replay_metrics().record_dependency_violation(op.operation_id);
        return NFSStatus::SERVERFAULT;
    }

    NFSStatus result;
    try {
        switch (op.procedure) {
            case NFSProcedure::WRITE:
                result = replay_write(op.arguments);
                break;
            case NFSProcedure::CREATE:
                result = replay_create(op.arguments);
                break;
            case NFSProcedure::REMOVE:
                result = replay_remove(op.arguments);
                break;
            default:
                LOG_ERROR("Cannot replay operation type {}", 
                    static_cast<int>(op.procedure));
                result = NFSStatus::NOTSUPP;
                break;
        }

        // Verify and record replay result
        success = verify_replay_result(op.operation_id, result);
        record_replay_attempt(op.operation_id, success);

        // Record metrics
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_time);
        get_replay_metrics().record_replay(op.procedure, duration, success);

        return result;

    } catch (const std::exception& e) {
        record_replay_attempt(op.operation_id, false, e.what());
        get_replay_metrics().record_verification_failure(op.operation_id, e.what());
        return NFSStatus::SERVERFAULT;
    }
}

NFSStatus OperationLog::replay_write(const std::vector<uint8_t>& args) {
    XDRDecoder decoder(args);
    NFSWriteArgs write_args;
    
    if (!decoder.decode_opaque(write_args.file_handle.handle) ||
        !decoder.decode(write_args.offset) ||
        !decoder.decode_opaque(write_args.data)) {
        return NFSStatus::INVAL;
    }
    
    // Replay write operation
    std::string path = translate_handle_to_path(write_args.file_handle.handle);
    int fd = open(path.c_str(), O_WRONLY);
    if (fd < 0) {
        return NFSStatus::IO;
    }
    
    if (lseek(fd, write_args.offset, SEEK_SET) < 0) {
        close(fd);
        return NFSStatus::IO;
    }
    
    ssize_t bytes_written = write(fd, write_args.data.data(), write_args.data.size());
    fsync(fd);
    close(fd);
    
    return bytes_written < 0 ? NFSStatus::IO : NFSStatus::OK;
}

NFSStatus OperationLog::replay_create(const std::vector<uint8_t>& args) {
    XDRDecoder decoder(args);
    NFSCreateArgs create_args;
    
    // Decode create arguments
    if (!decoder.decode_opaque(create_args.dir_handle.handle) ||
        !decoder.decode_string(create_args.name) ||
        !decoder.decode(create_args.mode)) {
        return NFSStatus::INVAL;
    }
    
    // Get directory path
    std::string dir_path = translate_handle_to_path(create_args.dir_handle.handle);
    std::string file_path = dir_path + "/" + create_args.name;
    
    // Check if file already exists (idempotency)
    struct stat st;
    if (stat(file_path.c_str(), &st) == 0) {
        // File exists - this is okay for recovery
        LOG_INFO("File {} already exists during create replay", file_path);
        return NFSStatus::OK;
    }
    
    // Create the file
    int fd = open(file_path.c_str(), O_CREAT | O_WRONLY, create_args.mode);
    if (fd < 0) {
        LOG_ERROR("Failed to create file {} during replay: {}", 
            file_path, std::strerror(errno));
        return NFSStatus::IO;
    }
    
    close(fd);
    return NFSStatus::OK;
}

NFSStatus OperationLog::replay_remove(const std::vector<uint8_t>& args) {
    XDRDecoder decoder(args);
    NFSRemoveArgs remove_args;
    
    // Decode remove arguments
    if (!decoder.decode_opaque(remove_args.dir_handle.handle) ||
        !decoder.decode_string(remove_args.name)) {
        return NFSStatus::INVAL;
    }
    
    // Get file path
    std::string dir_path = translate_handle_to_path(remove_args.dir_handle.handle);
    std::string file_path = dir_path + "/" + remove_args.name;
    
    // Check file type
    struct stat st;
    if (stat(file_path.c_str(), &st) != 0) {
        // File doesn't exist - this is okay for recovery
        LOG_INFO("File {} already removed during remove replay", file_path);
        return NFSStatus::OK;
    }
    
    // Remove file or directory
    int result;
    if (S_ISDIR(st.st_mode)) {
        result = rmdir(file_path.c_str());
    } else {
        result = unlink(file_path.c_str());
    }
    
    if (result != 0) {
        LOG_ERROR("Failed to remove {} during replay: {}", 
            file_path, std::strerror(errno));
        
        if (errno == ENOTEMPTY) {
            return NFSStatus::NOTEMPTY;
        }
        return NFSStatus::IO;
    }
    
    return NFSStatus::OK;
}

// Add helper method to check operation ordering
bool OperationLog::check_operation_order(const LoggedOperation& op1, 
                                       const LoggedOperation& op2) {
    // If operations are on different files, they can be replayed in any order
    if (get_file_path(op1) != get_file_path(op2)) {
        return true;
    }
    
    // For same file, maintain temporal ordering
    return op1.timestamp <= op2.timestamp;
}

std::string OperationLog::get_file_path(const LoggedOperation& op) {
    XDRDecoder decoder(op.arguments);
    NFSFileHandle handle;
    std::string name;
    
    switch (op.procedure) {
        case NFSProcedure::WRITE:
            decoder.decode_opaque(handle.handle);
            return translate_handle_to_path(handle.handle);
            
        case NFSProcedure::CREATE:
        case NFSProcedure::REMOVE:
            decoder.decode_opaque(handle.handle);  // dir handle
            decoder.decode_string(name);
            return translate_handle_to_path(handle.handle) + "/" + name;
            
        default:
            return "";
    }
}

// Add method to sort operations for replay
std::vector<LoggedOperation> OperationLog::sort_operations_for_replay(
    std::vector<LoggedOperation> ops) {
    
    // First sort by timestamp
    std::sort(ops.begin(), ops.end(),
        [](const LoggedOperation& a, const LoggedOperation& b) {
            return a.timestamp < b.timestamp;
        });
    
    // Build dependency graph
    std::unordered_map<uint32_t, std::vector<uint32_t>> graph;
    for (const auto& op : ops) {
        graph[op.operation_id] = {};
        for (const auto& other : ops) {
            if (op.operation_id != other.operation_id && 
                is_dependent_operation(op, other)) {
                graph[op.operation_id].push_back(other.operation_id);
            }
        }
    }
    
    // Topological sort
    std::vector<LoggedOperation> result;
    std::unordered_set<uint32_t> visited;
    std::unordered_set<uint32_t> temp_mark;
    
    std::function<void(uint32_t)> visit = [&](uint32_t op_id) {
        if (temp_mark.count(op_id)) {
            // Cycle detected - break dependency
            return;
        }
        if (visited.count(op_id)) {
            return;
        }
        
        temp_mark.insert(op_id);
        for (uint32_t dep : graph[op_id]) {
            visit(dep);
        }
        temp_mark.erase(op_id);
        visited.insert(op_id);
        
        auto it = std::find_if(ops.begin(), ops.end(),
            [op_id](const LoggedOperation& op) {
                return op.operation_id == op_id;
            });
        if (it != ops.end()) {
            result.push_back(*it);
        }
    };
    
    for (const auto& op : ops) {
        if (!visited.count(op.operation_id)) {
            visit(op.operation_id);
        }
    }
    
    return result;
}

bool OperationLog::is_dependent_operation(const LoggedOperation& op1,
                                        const LoggedOperation& op2) const {
    // If operations are on different paths and can run concurrently, no dependency
    if (!has_path_conflict(op1.operation_id, op2.operation_id) &&
        can_run_concurrently(op1, op2)) {
        return false;
    }
    
    // Check specific operation dependencies
    switch (op1.procedure) {
        case NFSProcedure::WRITE:
            // Write depends on CREATE and previous WRITEs
            return op2.procedure == NFSProcedure::CREATE ||
                   (op2.procedure == NFSProcedure::WRITE && 
                    op1.timestamp > op2.timestamp);
            
        case NFSProcedure::REMOVE:
            // REMOVE depends on all operations on the same path
            return has_path_conflict(op1.operation_id, op2.operation_id);
            
        case NFSProcedure::CREATE:
            // CREATE depends on REMOVE of same path
            return op2.procedure == NFSProcedure::REMOVE &&
                   has_path_conflict(op1.operation_id, op2.operation_id);
            
        default:
            // Default to temporal ordering for other operations
            return op1.timestamp > op2.timestamp;
    }
}

bool OperationLog::can_run_concurrently(const LoggedOperation& op1,
                                      const LoggedOperation& op2) const {
    // Read operations can run concurrently
    bool op1_read = op1.procedure == NFSProcedure::GETATTR ||
                    op1.procedure == NFSProcedure::LOOKUP;
    bool op2_read = op2.procedure == NFSProcedure::GETATTR ||
                    op2.procedure == NFSProcedure::LOOKUP;
    
    if (op1_read && op2_read) {
        return true;
    }
    
    // Write operations must be ordered
    bool op1_write = op1.procedure == NFSProcedure::WRITE ||
                     op1.procedure == NFSProcedure::CREATE ||
                     op1.procedure == NFSProcedure::REMOVE;
    bool op2_write = op2.procedure == NFSProcedure::WRITE ||
                     op2.procedure == NFSProcedure::CREATE ||
                     op2.procedure == NFSProcedure::REMOVE;
    
    return !(op1_write || op2_write);
}

// Add dependency tracking methods
void OperationLog::add_dependency(uint32_t op_id, uint32_t depends_on) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = operations_.find(op_id);
    if (it != operations_.end()) {
        // Check if dependency already exists
        auto& deps = it->second.dependencies;
        if (std::find(deps.begin(), deps.end(), depends_on) == deps.end()) {
            deps.push_back(depends_on);
            LOG_DEBUG("Added dependency: {} depends on {}", op_id, depends_on);
        }
    }
}

bool OperationLog::check_dependencies(uint32_t op_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = operations_.find(op_id);
    if (it == operations_.end()) {
        return false;
    }
    
    // Check if all dependencies are completed
    for (uint32_t dep_id : it->second.dependencies) {
        auto dep_it = operations_.find(dep_id);
        if (dep_it == operations_.end() || !dep_it->second.completed) {
            LOG_DEBUG("Dependency {} not satisfied for operation {}", dep_id, op_id);
            return false;
        }
    }
    
    return true;
}

std::vector<uint32_t> OperationLog::get_dependencies(uint32_t op_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = operations_.find(op_id);
    if (it != operations_.end()) {
        return it->second.dependencies;
    }
    return {};
}

// Add path tracking methods
void OperationLog::set_operation_path(uint32_t op_id, const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = operations_.find(op_id);
    if (it != operations_.end()) {
        it->second.target_path = path;
        LOG_DEBUG("Set target path for operation {}: {}", op_id, path);
    }
}

bool OperationLog::has_path_conflict(uint32_t op1, uint32_t op2) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it1 = operations_.find(op1);
    auto it2 = operations_.find(op2);
    
    if (it1 == operations_.end() || it2 == operations_.end()) {
        return false;
    }
    
    // Check if operations target the same path
    if (it1->second.target_path == it2->second.target_path) {
        LOG_DEBUG("Path conflict detected between {} and {}: same path {}",
            op1, op2, it1->second.target_path);
        return true;
    }
    
    // Check if one path is a parent of the other
    bool conflict = it1->second.target_path.find(it2->second.target_path) == 0 ||
                   it2->second.target_path.find(it1->second.target_path) == 0;
    
    if (conflict) {
        LOG_DEBUG("Path conflict detected between {} and {}: parent/child relationship",
            op1, op2);
    }
    
    return conflict;
}

std::vector<uint32_t> OperationLog::get_conflicting_operations(uint32_t op_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint32_t> conflicts;
    
    auto it = operations_.find(op_id);
    if (it == operations_.end()) {
        return conflicts;
    }
    
    for (const auto& [other_id, other_op] : operations_) {
        if (other_id != op_id && 
            has_path_conflict(op_id, other_id) &&
            !can_run_concurrently(it->second, other_op)) {
            conflicts.push_back(other_id);
        }
    }
    
    return conflicts;
}

// Add replay verification methods
bool OperationLog::verify_replay_order(const std::vector<LoggedOperation>& ops) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check that all dependencies are satisfied in the replay order
    for (size_t i = 0; i < ops.size(); i++) {
        const auto& op = ops[i];
        
        // Check each dependency
        for (uint32_t dep_id : op.dependencies) {
            // Find dependency's position in replay order
            auto dep_it = std::find_if(ops.begin(), ops.begin() + i,
                [dep_id](const LoggedOperation& other) {
                    return other.operation_id == dep_id;
                });
            
            // If dependency not found before current operation, order is invalid
            if (dep_it == ops.begin() + i) {
                LOG_ERROR("Invalid replay order: operation {} depends on {} but comes first",
                    op.operation_id, dep_id);
                get_replay_metrics().record_ordering_violation(op.operation_id);
                return false;
            }
        }
        
        // Check path conflicts with previous operations
        for (size_t j = 0; j < i; j++) {
            const auto& prev_op = ops[j];
            if (has_path_conflict(op.operation_id, prev_op.operation_id) &&
                !can_run_concurrently(op, prev_op)) {
                // Verify that the conflicting operation is a dependency
                if (std::find(op.dependencies.begin(), op.dependencies.end(),
                            prev_op.operation_id) == op.dependencies.end()) {
                    LOG_ERROR("Invalid replay order: operation {} conflicts with {} but no dependency",
                        op.operation_id, prev_op.operation_id);
                    get_replay_metrics().record_path_conflict(op.operation_id, prev_op.operation_id);
                    return false;
                }
            }
        }
    }
    
    return true;
}

bool OperationLog::verify_replay_result(uint32_t op_id, NFSStatus result) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = operations_.find(op_id);
    if (it == operations_.end()) {
        return false;
    }
    
    // For idempotent operations, certain "error" results are acceptable
    bool acceptable_result = false;
    switch (it->second.procedure) {
        case NFSProcedure::CREATE:
            // File already exists is okay for CREATE replay
            acceptable_result = (result == NFSStatus::OK || 
                               result == NFSStatus::EXIST);
            if (!acceptable_result) {
                get_replay_metrics().record_idempotency_failure(op_id);
            }
            break;
            
        case NFSProcedure::REMOVE:
            // File not found is okay for REMOVE replay
            acceptable_result = (result == NFSStatus::OK || 
                               result == NFSStatus::NOENT);
            if (!acceptable_result) {
                get_replay_metrics().record_idempotency_failure(op_id);
            }
            break;
            
        default:
            acceptable_result = (result == NFSStatus::OK);
    }
    
    if (!acceptable_result) {
        LOG_ERROR("Replay of operation {} failed with status {}",
            op_id, static_cast<int>(result));
        get_replay_metrics().record_verification_failure(op_id, 
            "Unexpected operation result");
    }
    
    return acceptable_result;
}

std::vector<uint32_t> OperationLog::get_failed_replays() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint32_t> failed;
    
    for (const auto& [op_id, op] : operations_) {
        if (op.replay_attempted && !op.replay_succeeded) {
            failed.push_back(op_id);
        }
    }
    
    return failed;
}

void OperationLog::record_replay_attempt(uint32_t op_id, bool success, 
                                       const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = operations_.find(op_id);
    if (it != operations_.end()) {
        it->second.replay_attempted = true;
        it->second.replay_succeeded = success;
        it->second.replay_error = error;
        
        if (!success) {
            LOG_ERROR("Replay of operation {} failed: {}", op_id, error);
        } else {
            LOG_DEBUG("Replay of operation {} succeeded", op_id);
        }
    }
}

} // namespace fuse_t 
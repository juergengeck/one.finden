#include "operation_replay.hpp"
#include <algorithm>
#include <unordered_set>

namespace fused {

OperationReplaySystem::OperationReplaySystem(OperationJournal& journal)
    : journal_(journal) {
    // Start replay processing thread
    replay_thread_ = std::thread([this]() {
        while (running_) {
            process_replay_queue();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}

OperationReplaySystem::~OperationReplaySystem() {
    running_ = false;
    if (replay_thread_.joinable()) {
        replay_thread_.join();
    }
}

bool OperationReplaySystem::initialize() {
    LOG_INFO("Initializing operation replay system");
    return true;
}

bool OperationReplaySystem::queue_operation(const ReplayOperation& op) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Verify operation hasn't already been processed
    if (active_operations_.find(op.sequence_id) != active_operations_.end()) {
        LOG_ERROR("Operation {} already queued", op.sequence_id);
        return false;
    }

    // Add to dependency graph
    if (!add_to_dependency_graph(op)) {
        LOG_ERROR("Failed to add operation {} to dependency graph", op.sequence_id);
        return false;
    }

    // Add to replay queue if dependencies are satisfied
    if (check_dependencies(op.sequence_id)) {
        replay_queue_.push(op);
        active_operations_[op.sequence_id] = op;
        LOG_DEBUG("Queued operation {} for replay", op.sequence_id);
    }

    return true;
}

bool OperationReplaySystem::replay_operation(uint64_t sequence_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_operations_.find(sequence_id);
    if (it == active_operations_.end()) {
        LOG_ERROR("Operation {} not found", sequence_id);
        return false;
    }

    ReplayStatus status = execute_replay(it->second);
    return handle_replay_result(sequence_id, status);
}

bool OperationReplaySystem::replay_batch(const std::vector<uint64_t>& sequence_ids) {
    if (!verify_replay_order(sequence_ids)) {
        LOG_ERROR("Invalid replay order for batch");
        return false;
    }

    bool success = true;
    for (uint64_t seq_id : sequence_ids) {
        if (!replay_operation(seq_id)) {
            success = false;
            break;
        }
    }
    return success;
}

void OperationReplaySystem::add_dependency(uint64_t op_id, uint64_t depends_on) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& node = dependency_graph_[op_id];
    node.sequence_id = op_id;
    node.dependencies.push_back(depends_on);

    auto& dep_node = dependency_graph_[depends_on];
    dep_node.sequence_id = depends_on;
    dep_node.dependents.push_back(op_id);
}

bool OperationReplaySystem::check_dependencies(uint64_t op_id) const {
    auto it = dependency_graph_.find(op_id);
    if (it == dependency_graph_.end()) {
        return true;  // No dependencies
    }

    for (uint64_t dep_id : it->second.dependencies) {
        auto dep_it = dependency_graph_.find(dep_id);
        if (dep_it == dependency_graph_.end() || !dep_it->second.completed) {
            return false;
        }
    }

    return true;
}

std::vector<uint64_t> OperationReplaySystem::get_dependencies(uint64_t op_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = dependency_graph_.find(op_id);
    if (it != dependency_graph_.end()) {
        return it->second.dependencies;
    }
    return {};
}

bool OperationReplaySystem::verify_replay_result(uint64_t op_id, NFSStatus result) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_operations_.find(op_id);
    if (it == active_operations_.end()) {
        return false;
    }

    // For idempotent operations, certain errors are acceptable
    if (it->second.idempotent) {
        switch (result) {
            case NFSStatus::OK:
            case NFSStatus::EXIST:    // For CREATE
            case NFSStatus::NOENT:    // For REMOVE
                return true;
            default:
                break;
        }
    }

    return result == NFSStatus::OK;
}

bool OperationReplaySystem::verify_replay_order(
    const std::vector<uint64_t>& sequence_ids) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check that dependencies are respected
    for (size_t i = 0; i < sequence_ids.size(); i++) {
        uint64_t op_id = sequence_ids[i];
        auto deps = get_dependencies(op_id);
        
        // All dependencies must appear before this operation
        for (uint64_t dep_id : deps) {
            auto it = std::find(sequence_ids.begin(), 
                              sequence_ids.begin() + i, dep_id);
            if (it == sequence_ids.begin() + i) {
                LOG_ERROR("Invalid replay order: {} depends on {}", op_id, dep_id);
                return false;
            }
        }
    }

    return true;
}

bool OperationReplaySystem::is_operation_completed(uint64_t sequence_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = dependency_graph_.find(sequence_id);
    return it != dependency_graph_.end() && it->second.completed;
}

ReplayStatus OperationReplaySystem::get_operation_status(uint64_t sequence_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_operations_.find(sequence_id);
    if (it == active_operations_.end()) {
        return ReplayStatus::PERMANENT_FAILURE;
    }

    if (!check_dependencies(sequence_id)) {
        return ReplayStatus::DEPENDENCY_FAILED;
    }

    if (it->second.retry_count >= MAX_RETRY_COUNT) {
        return ReplayStatus::PERMANENT_FAILURE;
    }

    return ReplayStatus::RETRY_NEEDED;
}

// Private helper methods
void OperationReplaySystem::process_replay_queue() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    while (!replay_queue_.empty()) {
        auto op = replay_queue_.front();
        replay_queue_.pop();

        if (!check_dependencies(op.sequence_id)) {
            continue;  // Skip if dependencies not met
        }

        ReplayStatus status = execute_replay(op);
        handle_replay_result(op.sequence_id, status);
    }
}

ReplayStatus OperationReplaySystem::execute_replay(const ReplayOperation& op) {
    LOG_DEBUG("Executing replay for operation {}", op.sequence_id);

    try {
        // Verify operation state before replay
        if (!verify_operation_state(op)) {
            return ReplayStatus::PERMANENT_FAILURE;
        }

        // Execute the operation through the journal
        if (!journal_.replay_operation(op.sequence_id)) {
            if (op.retry_count < MAX_RETRY_COUNT) {
                return ReplayStatus::RETRY_NEEDED;
            }
            return ReplayStatus::PERMANENT_FAILURE;
        }

        // Verify idempotency if required
        if (op.idempotent && !verify_idempotency(op)) {
            return ReplayStatus::PERMANENT_FAILURE;
        }

        return ReplayStatus::SUCCESS;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception during replay of operation {}: {}", 
            op.sequence_id, e.what());
        handle_replay_error(op.sequence_id, e.what());
        return ReplayStatus::PERMANENT_FAILURE;
    }
}

bool OperationReplaySystem::handle_replay_result(uint64_t sequence_id, 
                                               ReplayStatus status) {
    auto it = active_operations_.find(sequence_id);
    if (it == active_operations_.end()) {
        return false;
    }

    switch (status) {
        case ReplayStatus::SUCCESS:
            // Mark operation as completed
            dependency_graph_[sequence_id].completed = true;
            active_operations_.erase(it);
            return true;

        case ReplayStatus::RETRY_NEEDED:
            // Increment retry count and requeue
            it->second.retry_count++;
            replay_queue_.push(it->second);
            return true;

        case ReplayStatus::PERMANENT_FAILURE:
            // Remove operation and mark dependencies as failed
            active_operations_.erase(it);
            handle_replay_error(sequence_id, "Permanent replay failure");
            return false;

        case ReplayStatus::DEPENDENCY_FAILED:
            // Remove operation due to failed dependency
            active_operations_.erase(it);
            handle_replay_error(sequence_id, "Dependency failure");
            return false;
    }

    return false;
}

bool OperationReplaySystem::verify_operation_state(const ReplayOperation& op) {
    // Verify basic operation state
    if (op.sequence_id == 0 || op.arguments.empty()) {
        return false;
    }

    // Verify dependencies
    if (!verify_dependencies(op)) {
        return false;
    }

    return true;
}

bool OperationReplaySystem::verify_idempotency(const ReplayOperation& op) {
    // Verify operation can be safely replayed
    struct stat st;
    bool exists = stat(op.target_path.c_str(), &st) == 0;

    switch (op.procedure) {
        case NFSProcedure::CREATE:
            return !exists;  // Should not exist for create

        case NFSProcedure::REMOVE:
            return exists;   // Should exist for remove

        case NFSProcedure::WRITE:
            return exists && S_ISREG(st.st_mode);  // Should be regular file

        default:
            return true;  // Other operations are assumed idempotent
    }
}

bool OperationReplaySystem::verify_dependencies(const ReplayOperation& op) {
    for (uint64_t dep_id : op.dependencies) {
        if (!is_operation_completed(dep_id)) {
            return false;
        }
    }
    return true;
}

void OperationReplaySystem::handle_replay_error(uint64_t sequence_id, 
                                              const std::string& error) {
    LOG_ERROR("Replay error for operation {}: {}", sequence_id, error);
    
    // Try to recover from error
    if (!attempt_error_recovery(sequence_id)) {
        LOG_ERROR("Error recovery failed for operation {}", sequence_id);
    }
}

bool OperationReplaySystem::attempt_error_recovery(uint64_t sequence_id) {
    // Basic error recovery - remove from active operations and dependency graph
    active_operations_.erase(sequence_id);
    dependency_graph_.erase(sequence_id);

    // Mark dependent operations as failed
    std::vector<uint64_t> failed_deps;
    for (const auto& [id, node] : dependency_graph_) {
        if (std::find(node.dependencies.begin(), 
                     node.dependencies.end(), 
                     sequence_id) != node.dependencies.end()) {
            failed_deps.push_back(id);
        }
    }

    for (uint64_t dep_id : failed_deps) {
        handle_replay_error(dep_id, "Dependency failure");
    }

    return true;
}

bool OperationReplaySystem::add_to_dependency_graph(const ReplayOperation& op) {
    // Check for cycles before adding
    if (check_dependency_cycle(op.sequence_id)) {
        LOG_ERROR("Dependency cycle detected for operation {}", op.sequence_id);
        return false;
    }

    DependencyNode node{
        op.sequence_id,
        op.dependencies,
        {},  // dependents will be filled by add_dependency
        false
    };

    dependency_graph_[op.sequence_id] = node;

    // Add dependencies
    for (uint64_t dep_id : op.dependencies) {
        add_dependency(op.sequence_id, dep_id);
    }

    return true;
}

bool OperationReplaySystem::check_dependency_cycle(uint64_t op_id) const {
    std::unordered_set<uint64_t> visited;
    std::function<bool(uint64_t)> has_cycle = [&](uint64_t id) {
        if (visited.find(id) != visited.end()) {
            return true;
        }
        
        visited.insert(id);
        auto it = dependency_graph_.find(id);
        if (it != dependency_graph_.end()) {
            for (uint64_t dep_id : it->second.dependencies) {
                if (has_cycle(dep_id)) {
                    return true;
                }
            }
        }
        visited.erase(id);
        return false;
    };

    return has_cycle(op_id);
}

std::vector<uint64_t> OperationReplaySystem::get_ready_operations() const {
    std::vector<uint64_t> ready_ops;
    
    for (const auto& [id, node] : dependency_graph_) {
        if (!node.completed && check_dependencies(id)) {
            ready_ops.push_back(id);
        }
    }

    return ready_ops;
}

} // namespace fuse_t 
#include "session_recovery.hpp"

namespace fused {

bool SessionRecoveryManager::initialize() {
    LOG_INFO("Initializing session recovery manager");
    return true;
}

bool SessionRecoveryManager::start_recovery(const std::string& client_id, uint32_t session_id) {
    auto start_time = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);

    metrics_.total_recoveries++;

    // Check if client is already in recovery
    auto it = recovery_states_.find(client_id);
    if (it != recovery_states_.end()) {
        if (!it->second.completed) {
            LOG_ERROR("Client {} already in recovery", client_id);
            return false;
        }
        recovery_states_.erase(it);
    }

    // Create new recovery state
    RecoveryState state;
    state.session_id = session_id;
    state.start_time = std::chrono::steady_clock::now();
    state.completed = false;

    recovery_states_[client_id] = state;

    // Update recovery state
    state.recovery_attempts++;
    recovery_states_[client_id] = state;

    // Update metrics
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    metrics_.total_recovery_time_ms += duration.count();

    LOG_INFO("Started recovery for client {} session {}", client_id, session_id);
    return true;
}

bool SessionRecoveryManager::complete_recovery(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = recovery_states_.find(client_id);
    if (it == recovery_states_.end()) {
        LOG_ERROR("No recovery state found for client {}", client_id);
        metrics_.failed_recoveries++;
        return false;
    }

    it->second.completed = true;
    it->second.recovered_count = it->second.recovered_operations.size();
    metrics_.successful_recoveries++;
    metrics_.operations_recovered += it->second.recovered_count;

    LOG_INFO("Completed recovery for client {} session {}, recovered {} operations", 
        client_id, it->second.session_id, it->second.recovered_count);
    return true;
}

bool SessionRecoveryManager::is_in_recovery(const std::string& client_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = recovery_states_.find(client_id);
    return it != recovery_states_.end() && !it->second.completed;
}

const RecoveryState* SessionRecoveryManager::get_recovery_state(
    const std::string& client_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = recovery_states_.find(client_id);
    if (it == recovery_states_.end()) {
        return nullptr;
    }
    return &it->second;
}

void SessionRecoveryManager::cleanup_expired_states() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();

    for (auto it = recovery_states_.begin(); it != recovery_states_.end();) {
        if (now - it->second.start_time > RECOVERY_TIMEOUT) {
            if (!it->second.completed) {
                metrics_.expired_recoveries++;
                metrics_.failed_recoveries++;
            }
            LOG_INFO("Removing expired recovery state for client {}", it->first);
            it = recovery_states_.erase(it);
        } else {
            ++it;
        }
    }
}

void SessionRecoveryManager::record_operation(uint32_t session_id, uint32_t operation_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Find recovery state for this session
    for (auto& [client_id, state] : recovery_states_) {
        if (state.session_id == session_id && !state.completed) {
            state.recovered_operations.push_back(operation_id);
            break;
        }
    }
}

bool SessionRecoveryManager::was_operation_recovered(uint32_t session_id, 
                                                   uint32_t operation_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if operation was recovered in any recovery state for this session
    for (const auto& [client_id, state] : recovery_states_) {
        if (state.session_id == session_id) {
            return std::find(state.recovered_operations.begin(),
                           state.recovered_operations.end(),
                           operation_id) != state.recovered_operations.end();
        }
    }
    return false;
}

void SessionRecoveryManager::log_metrics() const {
    LOG_INFO("Session Recovery Metrics:");
    LOG_INFO("  Total recoveries: {}", metrics_.total_recoveries.load());
    LOG_INFO("  Successful recoveries: {}", metrics_.successful_recoveries.load());
    LOG_INFO("  Failed recoveries: {}", metrics_.failed_recoveries.load());
    LOG_INFO("  Expired recoveries: {}", metrics_.expired_recoveries.load());
    
    uint64_t total = metrics_.total_recoveries.load();
    if (total > 0) {
        double success_rate = 100.0 * metrics_.successful_recoveries.load() / total;
        double avg_time = static_cast<double>(metrics_.total_recovery_time_ms.load()) / total;
        LOG_INFO("  Success rate: {:.1f}%", success_rate);
        LOG_INFO("  Average recovery time: {:.2f}ms", avg_time);
        LOG_INFO("  Operations recovered: {}", metrics_.operations_recovered.load());
    }
}

} // namespace fuse_t 
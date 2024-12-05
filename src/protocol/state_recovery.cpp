#include "state_recovery.hpp"
#include <algorithm>
#include "util/recovery_metrics.hpp"

namespace fused {

void StateRecoveryManager::start_recovery_period() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    LOG_INFO("Starting state recovery period");
    in_recovery_ = true;
    recovery_end_ = std::chrono::steady_clock::now() + RECOVERY_TIMEOUT;
    get_recovery_metrics().recovery_periods++;
}

bool StateRecoveryManager::is_in_recovery() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!in_recovery_) {
        return false;
    }
    
    // Check if recovery period has expired
    if (std::chrono::steady_clock::now() > recovery_end_) {
        const_cast<StateRecoveryManager*>(this)->end_recovery_period();
        return false;
    }
    
    return true;
}

void StateRecoveryManager::end_recovery_period() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!in_recovery_) {
        return;
    }
    
    LOG_INFO("Ending state recovery period");
    in_recovery_ = false;
    
    // Check if period ended due to timeout
    if (std::chrono::steady_clock::now() > recovery_end_) {
        get_recovery_metrics().recovery_period_timeouts++;
    }
    
    cleanup_unrecovered_states();
}

bool StateRecoveryManager::start_client_recovery(
    const std::string& client_id, const std::vector<uint8_t>& verifier) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    auto& metrics = get_recovery_metrics();
    
    metrics.recovery_attempts++;
    
    if (!in_recovery_) {
        LOG_ERROR("Cannot start client recovery - not in recovery period");
        metrics.recovery_failures++;
        return false;
    }
    
    LOG_INFO("Starting recovery for client: {}", client_id);
    
    // Create recovery record
    ClientRecoveryRecord record;
    record.client_id = client_id;
    record.verifier = verifier;
    record.grace_period_end = std::chrono::steady_clock::now() + GRACE_PERIOD;
    
    recovery_records_[client_id] = std::move(record);
    metrics.recovery_successes++;
    return true;
}

bool StateRecoveryManager::complete_client_recovery(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& metrics = get_recovery_metrics();
    
    auto it = recovery_records_.find(client_id);
    if (it == recovery_records_.end()) {
        metrics.recovery_failures++;
        return false;
    }
    
    if (is_grace_period_expired(it->second)) {
        metrics.grace_period_expirations++;
        remove_client_recovery_record(client_id);
        return false;
    }
    
    metrics.clients_recovered++;
    metrics.states_recovered += it->second.states.size();
    
    // Remove recovery record after successful recovery
    recovery_records_.erase(it);
    return true;
}

bool StateRecoveryManager::verify_client_recovery(
    const std::string& client_id, const std::vector<uint8_t>& verifier) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = recovery_records_.find(client_id);
    if (it == recovery_records_.end()) {
        LOG_ERROR("No recovery record found for client: {}", client_id);
        return false;
    }
    
    if (is_grace_period_expired(it->second)) {
        LOG_ERROR("Grace period expired for client: {}", client_id);
        remove_client_recovery_record(client_id);
        return false;
    }
    
    // Verify client verifier matches
    return it->second.verifier == verifier;
}

bool StateRecoveryManager::save_state_for_recovery(
    const std::string& client_id, std::unique_ptr<State> state) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = recovery_records_.find(client_id);
    if (it == recovery_records_.end()) {
        LOG_ERROR("No recovery record found for client: {}", client_id);
        return false;
    }
    
    if (is_grace_period_expired(it->second)) {
        LOG_ERROR("Grace period expired for client: {}", client_id);
        remove_client_recovery_record(client_id);
        return false;
    }
    
    // Create state recovery record
    StateRecoveryRecord record;
    record.type = state->type;
    record.seqid = state->seqid;
    record.timestamp = std::chrono::steady_clock::now();
    record.state = std::move(state);
    
    it->second.states.push_back(std::move(record));
    return true;
}

bool StateRecoveryManager::recover_state(
    const std::string& client_id, StateType type, uint32_t seqid) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = recovery_records_.find(client_id);
    if (it == recovery_records_.end()) {
        LOG_ERROR("No recovery record found for client: {}", client_id);
        return false;
    }
    
    if (is_grace_period_expired(it->second)) {
        LOG_ERROR("Grace period expired for client: {}", client_id);
        remove_client_recovery_record(client_id);
        return false;
    }
    
    // Find matching state
    auto& states = it->second.states;
    auto state_it = std::find_if(states.begin(), states.end(),
        [type, seqid](const StateRecoveryRecord& record) {
            return record.type == type && record.seqid == seqid;
        });
    
    if (state_it == states.end()) {
        LOG_ERROR("No matching state found for recovery: type={}, seqid={}",
            static_cast<int>(type), seqid);
        return false;
    }
    
    // Remove recovered state
    states.erase(state_it);
    return true;
}

void StateRecoveryManager::cleanup_unrecovered_states() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& metrics = get_recovery_metrics();
    
    LOG_INFO("Cleaning up unrecovered states");
    
    // Count unrecovered states before cleanup
    for (const auto& [client_id, record] : recovery_records_) {
        metrics.clients_expired++;
        metrics.states_lost += record.states.size();
    }
    
    // Remove all recovery records
    recovery_records_.clear();
}

bool StateRecoveryManager::is_grace_period_expired(const ClientRecoveryRecord& record) const {
    return std::chrono::steady_clock::now() > record.grace_period_end;
}

void StateRecoveryManager::remove_client_recovery_record(const std::string& client_id) {
    LOG_INFO("Removing recovery record for client: {}", client_id);
    recovery_records_.erase(client_id);
}

} // namespace fuse_t 
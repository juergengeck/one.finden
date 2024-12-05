#include "client_recovery.hpp"
#include <algorithm>

namespace fused {

ClientStateRecovery::ClientStateRecovery(OperationJournal& journal,
                                       ClientSessionManager& session_mgr)
    : journal_(journal)
    , session_mgr_(session_mgr) {
    // Start recovery thread
    recovery_thread_ = std::thread([this]() {
        run_recovery_loop();
    });
}

ClientStateRecovery::~ClientStateRecovery() {
    running_ = false;
    if (recovery_thread_.joinable()) {
        recovery_thread_.join();
    }
}

bool ClientStateRecovery::initialize() {
    LOG_INFO("Initializing client state recovery");
    return true;
}

bool ClientStateRecovery::start_recovery(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (is_recovery_in_progress(client_id)) {
        LOG_ERROR("Recovery already in progress for client: {}", client_id);
        return false;
    }

    // Initialize recovery state
    ClientRecoveryState state{
        client_id,
        0,  // session_id will be set during recovery
        RecoveryPhase::SCAN,
        std::chrono::steady_clock::now(),
        {},  // no recovered operations yet
        false,  // not completed
        false,  // not successful
        ""      // no error message
    };

    recovery_states_[client_id] = std::move(state);
    LOG_INFO("Started recovery for client: {}", client_id);
    return true;
}

bool ClientStateRecovery::complete_recovery(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = recovery_states_.find(client_id);
    if (it == recovery_states_.end()) {
        return false;
    }

    // Complete all recovery phases
    if (!scan_client_state(it->second) ||
        !analyze_client_operations(it->second) ||
        !restore_client_session(it->second) ||
        !verify_client_state(it->second) ||
        !complete_client_recovery(it->second)) {
        return false;
    }

    it->second.completed = true;
    it->second.successful = true;
    LOG_INFO("Completed recovery for client: {}", client_id);
    return true;
}

bool ClientStateRecovery::verify_recovery(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = recovery_states_.find(client_id);
    if (it == recovery_states_.end()) {
        return false;
    }

    return verify_client_state(it->second);
}

bool ClientStateRecovery::abort_recovery(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = recovery_states_.find(client_id);
    if (it == recovery_states_.end()) {
        return false;
    }

    it->second.completed = true;
    it->second.successful = false;
    it->second.error_message = "Recovery aborted";
    LOG_INFO("Aborted recovery for client: {}", client_id);
    return true;
}

bool ClientStateRecovery::restore_session(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = recovery_states_.find(client_id);
    if (it == recovery_states_.end()) {
        return false;
    }

    return restore_client_session(it->second);
}

bool ClientStateRecovery::restore_operations(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = recovery_states_.find(client_id);
    if (it == recovery_states_.end()) {
        return false;
    }

    return replay_operations(client_id, it->second.recovered_operations);
}

bool ClientStateRecovery::verify_restored_state(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = recovery_states_.find(client_id);
    if (it == recovery_states_.end()) {
        return false;
    }

    return verify_operation_results(client_id, it->second.recovered_operations);
}

bool ClientStateRecovery::is_recovery_in_progress(const std::string& client_id) const {
    auto it = recovery_states_.find(client_id);
    return it != recovery_states_.end() && !it->second.completed;
}

bool ClientStateRecovery::is_recovery_completed(const std::string& client_id) const {
    auto it = recovery_states_.find(client_id);
    return it != recovery_states_.end() && it->second.completed;
}

RecoveryPhase ClientStateRecovery::get_recovery_phase(const std::string& client_id) const {
    auto it = recovery_states_.find(client_id);
    if (it != recovery_states_.end()) {
        return it->second.phase;
    }
    return RecoveryPhase::SCAN;
}

void ClientStateRecovery::run_recovery_loop() {
    while (running_) {
        process_recovery_states();
        cleanup_completed_recoveries();
        std::this_thread::sleep_for(RETRY_INTERVAL);
    }
}

void ClientStateRecovery::process_recovery_states() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& [client_id, state] : recovery_states_) {
        if (state.completed) {
            continue;
        }

        // Process based on current phase
        bool success = false;
        switch (state.phase) {
            case RecoveryPhase::SCAN:
                success = scan_client_state(state);
                break;
            case RecoveryPhase::ANALYZE:
                success = analyze_client_operations(state);
                break;
            case RecoveryPhase::RESTORE:
                success = restore_client_session(state);
                break;
            case RecoveryPhase::VERIFY:
                success = verify_client_state(state);
                break;
            case RecoveryPhase::COMPLETE:
                success = complete_client_recovery(state);
                break;
        }

        if (!success) {
            handle_recovery_error(client_id, "Failed in phase: " + 
                std::to_string(static_cast<int>(state.phase)));
        }
    }
}

void ClientStateRecovery::cleanup_completed_recoveries() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    
    for (auto it = recovery_states_.begin(); it != recovery_states_.end();) {
        if (it->second.completed && 
            now - it->second.start_time > RECOVERY_TIMEOUT) {
            it = recovery_states_.erase(it);
        } else {
            ++it;
        }
    }
}

bool ClientStateRecovery::scan_client_state(ClientRecoveryState& state) {
    LOG_INFO("Scanning state for client: {}", state.client_id);
    
    // Get client's last known session
    if (auto session_id = session_mgr_.get_client_session(state.client_id)) {
        state.session_id = *session_id;
    } else {
        state.error_message = "No session found for client";
        return false;
    }

    // Get client's operations from journal
    auto operations = journal_.get_client_operations(state.client_id);
    if (operations.empty()) {
        LOG_WARN("No operations found for client: {}", state.client_id);
    }

    state.recovered_operations = std::move(operations);
    state.phase = RecoveryPhase::ANALYZE;
    return true;
}

bool ClientStateRecovery::analyze_client_operations(ClientRecoveryState& state) {
    LOG_INFO("Analyzing operations for client: {}", state.client_id);
    
    // Verify operation sequence
    if (!verify_operation_sequence(state.recovered_operations)) {
        state.error_message = "Invalid operation sequence";
        return false;
    }

    state.phase = RecoveryPhase::RESTORE;
    return true;
}

bool ClientStateRecovery::restore_client_session(ClientRecoveryState& state) {
    LOG_INFO("Restoring session for client: {}", state.client_id);
    
    // Create new session for client
    uint32_t new_session_id;
    if (!session_mgr_.create_session(state.client_id, new_session_id)) {
        state.error_message = "Failed to create new session";
        return false;
    }

    state.session_id = new_session_id;
    state.phase = RecoveryPhase::VERIFY;
    return true;
}

bool ClientStateRecovery::verify_client_state(ClientRecoveryState& state) {
    LOG_INFO("Verifying state for client: {}", state.client_id);
    
    // Verify session state
    if (!session_mgr_.verify_session_state(state.session_id)) {
        state.error_message = "Session state verification failed";
        return false;
    }

    // Verify operation results
    if (!verify_operation_results(state.client_id, state.recovered_operations)) {
        state.error_message = "Operation results verification failed";
        return false;
    }

    state.phase = RecoveryPhase::COMPLETE;
    return true;
}

bool ClientStateRecovery::complete_client_recovery(ClientRecoveryState& state) {
    LOG_INFO("Completing recovery for client: {}", state.client_id);
    
    // Confirm session
    if (!session_mgr_.confirm_session(state.session_id)) {
        state.error_message = "Failed to confirm session";
        return false;
    }

    state.completed = true;
    state.successful = true;
    return true;
}

bool ClientStateRecovery::verify_operation_sequence(
    const std::vector<uint64_t>& operations) {
    // Verify operations are ordered correctly
    for (size_t i = 1; i < operations.size(); i++) {
        if (operations[i] <= operations[i-1]) {
            LOG_ERROR("Invalid operation sequence: {} <= {}", 
                operations[i], operations[i-1]);
            return false;
        }
    }
    return true;
}

bool ClientStateRecovery::replay_operations(
    const std::string& client_id,
    const std::vector<uint64_t>& operations) {
    LOG_INFO("Replaying {} operations for client: {}", 
        operations.size(), client_id);
    
    for (uint64_t op_id : operations) {
        if (!journal_.replay_operation(op_id)) {
            LOG_ERROR("Failed to replay operation {} for client {}", 
                op_id, client_id);
            return false;
        }
    }
    return true;
}

bool ClientStateRecovery::verify_operation_results(
    const std::string& client_id,
    const std::vector<uint64_t>& operations) {
    LOG_INFO("Verifying {} operation results for client: {}", 
        operations.size(), client_id);
    
    for (uint64_t op_id : operations) {
        if (!journal_.verify_operation_result(op_id)) {
            LOG_ERROR("Operation {} result verification failed for client {}", 
                op_id, client_id);
            return false;
        }
    }
    return true;
}

void ClientStateRecovery::handle_recovery_error(
    const std::string& client_id,
    const std::string& error) {
    LOG_ERROR("Recovery error for client {}: {}", client_id, error);
    
    auto it = recovery_states_.find(client_id);
    if (it != recovery_states_.end()) {
        it->second.error_message = error;
        attempt_recovery_retry(client_id);
    }
}

bool ClientStateRecovery::attempt_recovery_retry(const std::string& client_id) {
    auto it = recovery_states_.find(client_id);
    if (it == recovery_states_.end()) {
        return false;
    }

    // Check retry count
    static thread_local int retry_count = 0;
    if (++retry_count >= MAX_RETRY_COUNT) {
        LOG_ERROR("Max retry count reached for client: {}", client_id);
        it->second.completed = true;
        it->second.successful = false;
        return false;
    }

    LOG_INFO("Retrying recovery for client: {} (attempt {})", 
        client_id, retry_count);
    
    // Reset state for retry
    it->second.phase = RecoveryPhase::SCAN;
    it->second.recovered_operations.clear();
    return true;
}

} // namespace fuse_t 
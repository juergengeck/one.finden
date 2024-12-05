#include "client_session.hpp"
#include <algorithm>

namespace fused {

ClientSessionManager::ClientSessionManager() {
    // Start cleanup thread
    cleanup_thread_ = std::thread([this]() {
        while (running_) {
            cleanup_expired_sessions();
            std::this_thread::sleep_for(CLEANUP_INTERVAL);
        }
    });
}

ClientSessionManager::~ClientSessionManager() {
    running_ = false;
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
}

bool ClientSessionManager::initialize() {
    LOG_INFO("Initializing client session manager");
    return true;
}

bool ClientSessionManager::create_session(const ClientIdentifier& client, 
                                       uint32_t& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto start_time = std::chrono::steady_clock::now();
    
    // Verify client
    if (!verify_client(client)) {
        LOG_ERROR("Invalid client identifier");
        return false;
    }

    // Check if client is in recovery
    auto it = clients_.find(client.client_id);
    if (it != clients_.end()) {
        if (it->second == client) {
            // Client is reconnecting - start recovery
            if (!start_recovery(client)) {
                LOG_ERROR("Failed to start recovery for client: {}", client.client_id);
                return false;
            }
            return true;
        }
        // Different client with same ID - reject
        LOG_ERROR("Client ID conflict: {}", client.client_id);
        return false;
    }

    // Create new session
    session_id = next_session_id_++;
    SessionState session{
        session_id,
        std::chrono::steady_clock::now() + SESSION_TIMEOUT,
        {},  // empty sequence IDs
        false,  // not confirmed
        0,      // initial slot
        0,      // no operations
        std::chrono::steady_clock::now()  // current time
    };

    // Store client and session
    clients_[client.client_id] = client;
    sessions_[session_id] = std::move(session);

    // Record metrics
    get_session_metrics().record_session_creation();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start_time);
    get_session_metrics().record_operation(true, duration);

    LOG_INFO("Created session {} for client {}", session_id, client.client_id);
    return true;
}

bool ClientSessionManager::confirm_session(uint32_t session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        LOG_ERROR("Session {} not found", session_id);
        return false;
    }

    it->second.confirmed = true;
    it->second.expiry = std::chrono::steady_clock::now() + SESSION_TIMEOUT;
    it->second.last_activity = std::chrono::steady_clock::now();

    LOG_INFO("Confirmed session {}", session_id);
    return true;
}

bool ClientSessionManager::destroy_session(uint32_t session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return false;
    }

    // Remove session and associated client
    for (auto client_it = clients_.begin(); client_it != clients_.end(); ++client_it) {
        if (client_it->second.client_id == it->second.session_id) {
            clients_.erase(client_it);
            break;
        }
    }

    sessions_.erase(it);
    LOG_INFO("Destroyed session {}", session_id);
    return true;
}

bool ClientSessionManager::renew_session(uint32_t session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        LOG_ERROR("Session {} not found", session_id);
        return false;
    }

    if (!it->second.confirmed) {
        LOG_ERROR("Cannot renew unconfirmed session {}", session_id);
        return false;
    }

    it->second.expiry = std::chrono::steady_clock::now() + SESSION_TIMEOUT;
    it->second.last_activity = std::chrono::steady_clock::now();

    LOG_DEBUG("Renewed session {}", session_id);
    return true;
}

bool ClientSessionManager::check_sequence(uint32_t session_id, uint32_t sequence_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessions_.find(session_id);
    if (it == sessions_.end() || !it->second.confirmed) {
        return false;
    }

    // Check if sequence ID is valid
    const auto& seq_ids = it->second.sequence_ids;
    if (!seq_ids.empty() && sequence_id <= seq_ids.back()) {
        LOG_ERROR("Invalid sequence ID {} for session {}", sequence_id, session_id);
        get_session_metrics().record_sequence_violation();
        return false;
    }

    return true;
}

void ClientSessionManager::update_sequence(uint32_t session_id, uint32_t sequence_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessions_.find(session_id);
    if (it != sessions_.end() && it->second.confirmed) {
        it->second.sequence_ids.push_back(sequence_id);
        it->second.operations_count++;
        it->second.last_activity = std::chrono::steady_clock::now();
    }
}

bool ClientSessionManager::is_session_valid(uint32_t session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return false;
    }

    return std::chrono::steady_clock::now() < it->second.expiry;
}

bool ClientSessionManager::is_session_confirmed(uint32_t session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessions_.find(session_id);
    return it != sessions_.end() && it->second.confirmed;
}

bool ClientSessionManager::start_recovery(const ClientIdentifier& client) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Find existing session for client
    uint32_t session_id = 0;
    for (const auto& [id, session] : sessions_) {
        if (session.session_id == std::stoul(client.client_id)) {
            session_id = id;
            break;
        }
    }

    if (session_id == 0) {
        LOG_ERROR("No session found for client {}", client.client_id);
        return false;
    }

    return start_session_recovery(session_id);
}

bool ClientSessionManager::complete_recovery(uint32_t session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto start_time = std::chrono::steady_clock::now();
    
    auto it = recovery_states_.find(session_id);
    if (it == recovery_states_.end()) {
        LOG_ERROR("No recovery state for session {}", session_id);
        return false;
    }

    // Verify recovery state
    if (!verify_session_state(session_id)) {
        LOG_ERROR("Session state verification failed for {}", session_id);
        return false;
    }

    // Restore session state
    if (!restore_session_state(session_id)) {
        LOG_ERROR("Failed to restore session state for {}", session_id);
        return false;
    }

    it->second.completed = true;
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    get_session_metrics().record_session_recovery(true, duration);

    LOG_INFO("Completed recovery for session {}", session_id);
    return true;
}

bool ClientSessionManager::verify_recovery(uint32_t session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = recovery_states_.find(session_id);
    if (it == recovery_states_.end()) {
        return false;
    }

    return it->second.completed;
}

// Private helper methods
void ClientSessionManager::cleanup_expired_sessions() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();

    // Remove expired sessions
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (now >= it->second.expiry) {
            LOG_INFO("Removing expired session {}", it->first);
            get_session_metrics().record_session_expiry();
            remove_session(it->first);
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }

    // Clean up expired recovery states
    for (auto it = recovery_states_.begin(); it != recovery_states_.end();) {
        if (now - it->second.start_time > RECOVERY_TIMEOUT) {
            LOG_INFO("Removing expired recovery state for session {}", it->first);
            it = recovery_states_.erase(it);
        } else {
            ++it;
        }
    }
}

void ClientSessionManager::remove_session(uint32_t session_id) {
    // Remove associated client
    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        if (it->second.client_id == std::to_string(session_id)) {
            clients_.erase(it);
            break;
        }
    }

    // Remove recovery state if exists
    recovery_states_.erase(session_id);
}

bool ClientSessionManager::verify_client(const ClientIdentifier& client) const {
    return !client.client_id.empty() && !client.verifier.empty();
}

bool ClientSessionManager::start_session_recovery(uint32_t session_id) {
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return false;
    }

    RecoveryState recovery{
        session_id,
        std::chrono::steady_clock::now(),
        {},  // recovered operations will be added during recovery
        false
    };

    recovery_states_[session_id] = std::move(recovery);
    LOG_INFO("Started recovery for session {}", session_id);
    return true;
}

bool ClientSessionManager::verify_session_state(uint32_t session_id) {
    auto session_it = sessions_.find(session_id);
    auto recovery_it = recovery_states_.find(session_id);
    
    if (session_it == sessions_.end() || recovery_it == recovery_states_.end()) {
        return false;
    }

    // Verify sequence numbers are continuous
    const auto& seq_ids = session_it->second.sequence_ids;
    if (!seq_ids.empty()) {
        for (size_t i = 1; i < seq_ids.size(); ++i) {
            if (seq_ids[i] != seq_ids[i-1] + 1) {
                LOG_ERROR("Sequence number discontinuity in session {}", session_id);
                return false;
            }
        }
    }

    return true;
}

bool ClientSessionManager::restore_session_state(uint32_t session_id) {
    auto session_it = sessions_.find(session_id);
    auto recovery_it = recovery_states_.find(session_id);
    
    if (session_it == sessions_.end() || recovery_it == recovery_states_.end()) {
        return false;
    }

    // Restore session state
    session_it->second.confirmed = true;
    session_it->second.expiry = std::chrono::steady_clock::now() + SESSION_TIMEOUT;
    session_it->second.last_activity = std::chrono::steady_clock::now();

    // Update recovered operations
    session_it->second.operations_count = recovery_it->second.recovered_operations.size();

    LOG_INFO("Restored state for session {}", session_id);
    return true;
}

} // namespace fuse_t 
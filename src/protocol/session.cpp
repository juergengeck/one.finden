#include "session.hpp"
#include <algorithm>

namespace fused {

SessionManager::SessionManager() {
    LOG_INFO("Initializing session manager");
}

SessionManager::~SessionManager() {
    running_ = false;
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
    
    // Stop parallel recovery before other cleanup
    if (parallel_recovery_) {
        parallel_recovery_->stop();
    }
}

bool SessionManager::initialize() {
    // Initialize recovery manager
    recovery_manager_ = std::make_unique<SessionRecoveryManager>();
    if (!recovery_manager_->initialize()) {
        LOG_ERROR("Failed to initialize session recovery manager");
        return false;
    }

    // Initialize parallel recovery manager
    parallel_recovery_ = std::make_unique<ParallelRecoveryManager>();
    if (!parallel_recovery_->initialize()) {
        LOG_ERROR("Failed to initialize parallel recovery manager");
        return false;
    }

    // Initialize alert manager
    alert_manager_ = std::make_unique<RecoveryAlertManager>(get_alert_manager());

    // Start cleanup thread
    cleanup_thread_ = std::thread([this]() {
        while (running_) {
            cleanup_expired_sessions();
            recovery_manager_->cleanup_expired_states();
            
            // Check recovery metrics periodically
            alert_manager_->check_metrics(recovery_manager_->get_metrics());
            
            std::this_thread::sleep_for(CLEANUP_INTERVAL);
        }
    });

    LOG_INFO("Session manager initialized successfully");
    return true;
}

bool SessionManager::create_session(const std::string& client_id, uint32_t& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if client is in recovery
    if (recovery_manager_->is_in_recovery(client_id)) {
        const auto* recovery_state = recovery_manager_->get_recovery_state(client_id);
        if (recovery_state) {
            session_id = recovery_state->session_id;
            
            // Schedule parallel recovery if not already started
            if (!parallel_recovery_->is_recovery_complete(client_id)) {
                RecoveryTask task{
                    client_id,
                    session_id,
                    recovery_state->recovered_operations,
                    std::chrono::steady_clock::now()
                };
                parallel_recovery_->schedule_recovery(std::move(task));
            }
            
            LOG_INFO("Recovering session {} for client {}", session_id, client_id);
            return true;
        }
    }
    
    // Generate new session ID
    session_id = next_session_id_++;
    
    // Create new session
    auto session = std::make_unique<Session>();
    session->client_id = client_id;
    session->session_id = session_id;
    session->expiry = std::chrono::steady_clock::now() + SESSION_TIMEOUT;
    session->confirmed = false;
    
    // Store session
    sessions_[session_id] = std::move(session);
    
    LOG_INFO("Created session {} for client {}", session_id, client_id);
    return true;
}

bool SessionManager::confirm_session(uint32_t session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        LOG_ERROR("Session {} not found", session_id);
        return false;
    }
    
    if (it->second->confirmed) {
        LOG_WARN("Session {} already confirmed", session_id);
        return true;
    }
    
    // Wait for any ongoing recovery to complete
    if (recovery_manager_->is_in_recovery(it->second->client_id)) {
        if (!parallel_recovery_->wait_for_recovery(
                it->second->client_id, 
                std::chrono::seconds(30))) {
            LOG_ERROR("Timeout waiting for recovery of session {}", session_id);
            return false;
        }
    }
    
    it->second->confirmed = true;
    it->second->expiry = std::chrono::steady_clock::now() + SESSION_TIMEOUT;
    
    LOG_INFO("Confirmed session {}", session_id);
    return true;
}

bool SessionManager::destroy_session(uint32_t session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        LOG_ERROR("Session {} not found", session_id);
        return false;
    }
    
    LOG_INFO("Destroying session {}", session_id);
    sessions_.erase(it);
    return true;
}

bool SessionManager::renew_session(uint32_t session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        LOG_ERROR("Session {} not found", session_id);
        return false;
    }
    
    if (!it->second->confirmed) {
        LOG_ERROR("Cannot renew unconfirmed session {}", session_id);
        return false;
    }
    
    it->second->expiry = std::chrono::steady_clock::now() + SESSION_TIMEOUT;
    LOG_DEBUG("Renewed session {}", session_id);
    return true;
}

bool SessionManager::check_sequence(uint32_t session_id, const std::vector<uint8_t>& seq_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        LOG_ERROR("Session {} not found", session_id);
        return false;
    }
    
    if (!it->second->confirmed) {
        LOG_ERROR("Cannot check sequence for unconfirmed session {}", session_id);
        return false;
    }
    
    // Check if sequence ID is newer than current
    if (it->second->sequence_id.empty()) {
        return true;  // First sequence ID is always valid
    }
    
    return seq_id > it->second->sequence_id;
}

void SessionManager::update_sequence(uint32_t session_id, const std::vector<uint8_t>& seq_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessions_.find(session_id);
    if (it != sessions_.end() && it->second->confirmed) {
        it->second->sequence_id = seq_id;
        LOG_DEBUG("Updated sequence for session {}", session_id);
    }
}

bool SessionManager::is_session_valid(uint32_t session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return false;
    }
    
    return std::chrono::steady_clock::now() < it->second->expiry;
}

bool SessionManager::is_session_confirmed(uint32_t session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return false;
    }
    
    return it->second->confirmed;
}

void SessionManager::cleanup_expired_sessions() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (now >= it->second->expiry) {
            LOG_INFO("Removing expired session {}", it->first);
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

void SessionManager::remove_session(uint32_t session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(session_id);
}

} // namespace fuse_t 
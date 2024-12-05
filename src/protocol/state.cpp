#include "state.hpp"
#include "util/logger.hpp"
#include <algorithm>

namespace fused {

bool StateManager::register_client(const std::string& client_id, const std::vector<uint8_t>& verifier) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if we're in recovery mode
    if (recovery_manager_.is_in_recovery()) {
        // Handle client recovery
        if (recovery_manager_.verify_client_recovery(client_id, verifier)) {
            LOG_INFO("Client {} recovering previous session", client_id);
            return recovery_manager_.start_client_recovery(client_id, verifier);
        }
    }
    
    // Normal client registration
    LOG_DEBUG("Registering client: {}", client_id);

    // Check if client already exists
    auto it = clients_.find(client_id);
    if (it != clients_.end()) {
        // If verifier matches, this is a retry of a previous registration
        if (it->second.verifier == verifier) {
            LOG_DEBUG("Client {} already registered with matching verifier", client_id);
            return true;
        }
        // If verifier doesn't match, this is a new client with same ID
        LOG_WARN("Client {} already exists with different verifier", client_id);
        return false;
    }

    // Create new client state
    ClientState client;
    client.client_id = client_id;
    client.verifier = verifier;
    client.lease_expiry = std::chrono::steady_clock::now() + LEASE_TIME;
    client.confirmed = false;

    clients_[client_id] = std::move(client);
    LOG_INFO("Registered new client: {}", client_id);
    return true;
}

bool StateManager::confirm_client(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    LOG_DEBUG("Confirming client: {}", client_id);

    auto it = clients_.find(client_id);
    if (it == clients_.end()) {
        LOG_ERROR("Cannot confirm non-existent client: {}", client_id);
        return false;
    }

    it->second.confirmed = true;
    it->second.lease_expiry = std::chrono::steady_clock::now() + LEASE_TIME;
    LOG_INFO("Confirmed client: {}", client_id);
    return true;
}

bool StateManager::renew_lease(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    LOG_DEBUG("Renewing lease for client: {}", client_id);

    auto it = clients_.find(client_id);
    if (it == clients_.end()) {
        LOG_ERROR("Cannot renew lease for non-existent client: {}", client_id);
        return false;
    }

    if (!it->second.confirmed) {
        LOG_ERROR("Cannot renew lease for unconfirmed client: {}", client_id);
        return false;
    }

    it->second.lease_expiry = std::chrono::steady_clock::now() + LEASE_TIME;
    LOG_DEBUG("Renewed lease for client: {}", client_id);
    return true;
}

void StateManager::remove_client(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    LOG_INFO("Removing client: {}", client_id);

    // Remove client's states first
    remove_client_states(client_id);

    // Remove client
    clients_.erase(client_id);
}

bool StateManager::add_state(const std::string& client_id, std::unique_ptr<State> state) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto client_it = clients_.find(client_id);
    if (client_it == clients_.end() || !client_it->second.confirmed) {
        LOG_ERROR("Cannot add state for invalid client: {}", client_id);
        return false;
    }

    // Set state expiry to match client lease
    state->expiry = client_it->second.lease_expiry;
    state->client_id = client_id;

    // Save state for potential recovery
    auto state_copy = std::make_unique<State>(*state);
    recovery_manager_.save_state_for_recovery(client_id, std::move(state_copy));

    // Add state to client's state list
    states_[client_id].push_back(std::move(state));
    return true;
}

bool StateManager::remove_state(const std::string& client_id, StateType type, uint32_t seqid) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // If in recovery, handle recovery state removal
    if (recovery_manager_.is_in_recovery()) {
        return recovery_manager_.recover_state(client_id, type, seqid);
    }
    
    // Normal state removal
    auto states_it = states_.find(client_id);
    if (states_it == states_.end()) {
        return false;
    }

    auto& client_states = states_it->second;
    auto it = std::find_if(client_states.begin(), client_states.end(),
        [type, seqid](const auto& state) {
            return state->type == type && state->seqid == seqid;
        });

    if (it == client_states.end()) {
        return false;
    }

    client_states.erase(it);
    return true;
}

State* StateManager::find_state(const std::string& client_id, StateType type, uint32_t seqid) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    LOG_DEBUG("Finding state for client {}: type={}, seqid={}", 
        client_id, static_cast<int>(type), seqid);

    auto states_it = states_.find(client_id);
    if (states_it == states_.end()) {
        return nullptr;
    }

    auto& client_states = states_it->second;
    auto it = std::find_if(client_states.begin(), client_states.end(),
        [type, seqid](const auto& state) {
            return state->type == type && state->seqid == seqid;
        });

    return it != client_states.end() ? it->get() : nullptr;
}

void StateManager::cleanup_expired_states() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    LOG_DEBUG("Starting cleanup of expired states");
    auto now = std::chrono::steady_clock::now();

    // Remove expired clients and their states
    for (auto it = clients_.begin(); it != clients_.end();) {
        if (is_lease_expired(it->second)) {
            LOG_INFO("Removing expired client: {}", it->first);
            remove_client_states(it->first);
            it = clients_.erase(it);
        } else {
            ++it;
        }
    }
}

void StateManager::remove_client_states(const std::string& client_id) {
    LOG_DEBUG("Removing all states for client: {}", client_id);
    states_.erase(client_id);
}

bool StateManager::is_lease_expired(const ClientState& client) const {
    return std::chrono::steady_clock::now() > client.lease_expiry;
}

void StateManager::initialize() {
    // Start recovery period
    recovery_manager_.start_recovery_period();
}

void StateManager::cleanup() {
    // End recovery period and cleanup unrecovered states
    recovery_manager_.end_recovery_period();
    
    // Normal cleanup
    cleanup_expired_states();
}

} // namespace fuse_t 
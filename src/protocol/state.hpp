#pragma once
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <string>
#include <vector>
#include "nfs_protocol.hpp"

namespace fused {

// Client state
struct ClientState {
    std::string client_id;          // Client identifier
    std::chrono::steady_clock::time_point lease_expiry;
    std::vector<uint8_t> verifier;  // Client-provided verifier
    bool confirmed{false};          // Whether client is confirmed
};

// State types
enum class StateType {
    OPEN,
    LOCK,
    DELEGATION
};

// Base state class
struct State {
    StateType type;
    std::string client_id;
    uint32_t seqid;
    std::chrono::steady_clock::time_point expiry;
    
    virtual ~State() = default;
};

// Open state
struct OpenState : State {
    NFSFileHandle handle;
    uint32_t share_access;    // READ, WRITE, or BOTH
    uint32_t share_deny;      // NONE, READ, WRITE, or BOTH
    uint32_t open_owner;      // Owner identifier
};

// Lock state
struct LockState : State {
    NFSFileHandle handle;
    uint64_t offset;
    uint64_t length;
    NFSLockType lock_type;
    uint32_t lock_owner;      // Owner identifier
};

// State manager
class StateManager {
public:
    // Client operations
    bool register_client(const std::string& client_id, const std::vector<uint8_t>& verifier);
    bool confirm_client(const std::string& client_id);
    bool renew_lease(const std::string& client_id);
    void remove_client(const std::string& client_id);

    // State operations
    bool add_state(const std::string& client_id, std::unique_ptr<State> state);
    bool remove_state(const std::string& client_id, StateType type, uint32_t seqid);
    State* find_state(const std::string& client_id, StateType type, uint32_t seqid);

    // Cleanup
    void cleanup_expired_states();

private:
    std::mutex mutex_;
    std::unordered_map<std::string, ClientState> clients_;
    std::unordered_map<std::string, std::vector<std::unique_ptr<State>>> states_;
    
    static constexpr auto LEASE_TIME = std::chrono::seconds(90);
    
    void remove_client_states(const std::string& client_id);
    bool is_lease_expired(const ClientState& client) const;
    class StateRecoveryManager;  // Forward declaration
    StateRecoveryManager recovery_manager_;
};

} // namespace fused 
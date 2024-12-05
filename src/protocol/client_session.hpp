#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include "nfs_protocol.hpp"
#include "util/logger.hpp"

namespace fused {

struct ClientIdentifier {
    std::string client_id;
    std::vector<uint8_t> verifier;
    std::chrono::steady_clock::time_point creation_time;
    bool operator==(const ClientIdentifier& other) const {
        return client_id == other.client_id && verifier == other.verifier;
    }
};

struct SessionState {
    uint32_t session_id;
    std::chrono::steady_clock::time_point expiry;
    std::vector<uint32_t> sequence_ids;
    bool confirmed{false};
    uint32_t slot_id{0};
    std::atomic<uint64_t> operations_count{0};
    std::chrono::steady_clock::time_point last_activity;
};

class ClientSessionManager {
public:
    ClientSessionManager();
    ~ClientSessionManager();

    // Initialize session manager
    bool initialize();

    // Session operations
    bool create_session(const ClientIdentifier& client, uint32_t& session_id);
    bool confirm_session(uint32_t session_id);
    bool destroy_session(uint32_t session_id);
    bool renew_session(uint32_t session_id);

    // Sequence operations
    bool check_sequence(uint32_t session_id, uint32_t sequence_id);
    void update_sequence(uint32_t session_id, uint32_t sequence_id);

    // Session validation
    bool is_session_valid(uint32_t session_id) const;
    bool is_session_confirmed(uint32_t session_id) const;

    // Session recovery
    bool start_recovery(const ClientIdentifier& client);
    bool complete_recovery(uint32_t session_id);
    bool verify_recovery(uint32_t session_id);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, ClientIdentifier> clients_;
    std::unordered_map<uint32_t, SessionState> sessions_;
    std::atomic<uint32_t> next_session_id_{1};
    std::thread cleanup_thread_;
    std::atomic<bool> running_{true};

    static constexpr auto SESSION_TIMEOUT = std::chrono::minutes(30);
    static constexpr auto CLEANUP_INTERVAL = std::chrono::minutes(5);
    static constexpr auto RECOVERY_TIMEOUT = std::chrono::minutes(5);

    // Session management helpers
    void cleanup_expired_sessions();
    void remove_session(uint32_t session_id);
    bool verify_client(const ClientIdentifier& client) const;
    
    // Recovery helpers
    struct RecoveryState {
        uint32_t session_id;
        std::chrono::steady_clock::time_point start_time;
        std::vector<uint32_t> recovered_operations;
        bool completed{false};
    };
    std::unordered_map<uint32_t, RecoveryState> recovery_states_;
    
    bool start_session_recovery(uint32_t session_id);
    bool verify_session_state(uint32_t session_id);
    bool restore_session_state(uint32_t session_id);
};

} // namespace fuse_t 
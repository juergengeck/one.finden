#pragma once
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include "nfs_protocol.hpp"
#include "util/logger.hpp"
#include "session_recovery.hpp"
#include "util/recovery_alerts.hpp"
#include "parallel_recovery.hpp"

namespace fused {

struct Session {
    std::string client_id;
    uint32_t session_id;
    std::chrono::steady_clock::time_point expiry;
    std::vector<uint8_t> sequence_id;
    bool confirmed{false};
};

class SessionManager {
public:
    SessionManager();
    ~SessionManager();

    // Initialize session manager
    bool initialize();

    // Session operations
    bool create_session(const std::string& client_id, uint32_t& session_id);
    bool confirm_session(uint32_t session_id);
    bool destroy_session(uint32_t session_id);
    bool renew_session(uint32_t session_id);

    // Sequence operations
    bool check_sequence(uint32_t session_id, const std::vector<uint8_t>& seq_id);
    void update_sequence(uint32_t session_id, const std::vector<uint8_t>& seq_id);

    // Session validation
    bool is_session_valid(uint32_t session_id) const;
    bool is_session_confirmed(uint32_t session_id) const;

private:
    std::mutex mutex_;
    std::unordered_map<uint32_t, std::unique_ptr<Session>> sessions_;
    uint32_t next_session_id_{1};
    std::thread cleanup_thread_;
    std::atomic<bool> running_{true};

    static constexpr auto SESSION_TIMEOUT = std::chrono::minutes(30);
    static constexpr auto CLEANUP_INTERVAL = std::chrono::minutes(5);

    std::unique_ptr<SessionRecoveryManager> recovery_manager_;
    std::unique_ptr<RecoveryAlertManager> alert_manager_;
    std::unique_ptr<ParallelRecoveryManager> parallel_recovery_;

    void cleanup_expired_sessions();
    void remove_session(uint32_t session_id);
};

} // namespace fuse_t 
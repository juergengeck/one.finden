#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include "nfs_protocol.hpp"
#include "operation_journal.hpp"
#include "client_session.hpp"
#include "util/logger.hpp"

namespace fused {

enum class RecoveryPhase {
    SCAN,           // Scan for client state
    ANALYZE,        // Analyze client operations
    RESTORE,        // Restore client session
    VERIFY,         // Verify recovered state
    COMPLETE        // Recovery complete
};

struct ClientRecoveryState {
    std::string client_id;
    uint32_t session_id;
    RecoveryPhase phase{RecoveryPhase::SCAN};
    std::chrono::steady_clock::time_point start_time;
    std::vector<uint64_t> recovered_operations;
    bool completed{false};
    bool successful{false};
    std::string error_message;
};

class ClientStateRecovery {
public:
    ClientStateRecovery(OperationJournal& journal, ClientSessionManager& session_mgr);
    ~ClientStateRecovery();

    // Initialize recovery system
    bool initialize();

    // Recovery operations
    bool start_recovery(const std::string& client_id);
    bool complete_recovery(const std::string& client_id);
    bool verify_recovery(const std::string& client_id);
    bool abort_recovery(const std::string& client_id);

    // State restoration
    bool restore_session(const std::string& client_id);
    bool restore_operations(const std::string& client_id);
    bool verify_restored_state(const std::string& client_id);

    // Status checks
    bool is_recovery_in_progress(const std::string& client_id) const;
    bool is_recovery_completed(const std::string& client_id) const;
    RecoveryPhase get_recovery_phase(const std::string& client_id) const;

private:
    OperationJournal& journal_;
    ClientSessionManager& session_mgr_;
    std::mutex mutex_;
    std::thread recovery_thread_;
    std::atomic<bool> running_{true};

    static constexpr auto RECOVERY_TIMEOUT = std::chrono::minutes(5);
    static constexpr auto RETRY_INTERVAL = std::chrono::seconds(1);
    static constexpr auto MAX_RETRY_COUNT = 3;

    // Recovery tracking
    std::unordered_map<std::string, ClientRecoveryState> recovery_states_;

    // Recovery thread
    void run_recovery_loop();
    void process_recovery_states();
    void cleanup_completed_recoveries();

    // Recovery phases
    bool scan_client_state(ClientRecoveryState& state);
    bool analyze_client_operations(ClientRecoveryState& state);
    bool restore_client_session(ClientRecoveryState& state);
    bool verify_client_state(ClientRecoveryState& state);
    bool complete_client_recovery(ClientRecoveryState& state);

    // Recovery helpers
    bool verify_operation_sequence(const std::vector<uint64_t>& operations);
    bool replay_operations(const std::string& client_id, 
                         const std::vector<uint64_t>& operations);
    bool verify_operation_results(const std::string& client_id,
                                const std::vector<uint64_t>& operations);

    // Error handling
    void handle_recovery_error(const std::string& client_id, 
                             const std::string& error);
    bool attempt_recovery_retry(const std::string& client_id);
};

} // namespace fuse_t 
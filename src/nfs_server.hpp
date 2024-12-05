#pragma once
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include "protocol/nfs_protocol.hpp"
#include "protocol/fuse_protocol.hpp"
#include "protocol/state_recovery.hpp"
#include "util/metrics_export.hpp"
#include "util/alert_handler.hpp"
#include "protocol/rpc_message.hpp"
#include "protocol/session.hpp"
#include "protocol/rpc_session.hpp"

namespace fused {

// NFS protocol structures
struct NFSHeader {
    uint32_t xid;        // Transaction ID
    uint32_t type;       // Call or reply
    uint32_t rpc_vers;   // RPC version
    uint32_t prog;       // NFS program number
    uint32_t vers;       // NFS version
    uint32_t proc;       // NFS procedure
};

// Move LockStats before NFSServer class
struct LockStats {
    std::atomic<uint64_t> lock_attempts{0};
    std::atomic<uint64_t> lock_successes{0};
    std::atomic<uint64_t> lock_failures{0};
    std::atomic<uint64_t> deadlocks{0};
    std::atomic<uint64_t> timeouts{0};
    std::atomic<uint64_t> upgrades{0};
    std::atomic<uint64_t> downgrades{0};
    std::atomic<uint64_t> total_wait_time_ms{0};
};

class NFSServer {
public:
    NFSServer();
    ~NFSServer();

    bool initialize();
    bool start();
    void stop();
    
    // Add public method to get statistics
    void get_lock_stats(LockStats& stats) const;

private:
    // Handle incoming NFS requests
    void handle_request();
    
    // Convert FUSE operations to NFS operations
    bool translate_operation(const FuseOperation& fuse_op, NFSOperation& nfs_op);

    // NFS procedure handlers
    void handle_null_proc(int client_sock, const NFSHeader& header);
    void handle_getattr(int client_sock, const NFSHeader& header, const uint8_t* data);
    void handle_lookup(int client_sock, const NFSHeader& header, const uint8_t* data);
    void handle_read(int client_sock, const NFSHeader& header, const uint8_t* data);
    void handle_write(int client_sock, const NFSHeader& header, const uint8_t* data);
    void handle_readdir(int client_sock, const NFSHeader& header, const uint8_t* data);
    void handle_create(int client_sock, const NFSHeader& header, const uint8_t* data);
    void handle_remove(int client_sock, const NFSHeader& header, const uint8_t* data);
    void handle_mkdir(int client_sock, const NFSHeader& header, const uint8_t* data);
    void handle_rmdir(int client_sock, const NFSHeader& header, const uint8_t* data);
    void handle_rename(int client_sock, const NFSHeader& header, const uint8_t* data);
    void handle_setattr(int client_sock, const NFSHeader& header, const uint8_t* data);
    void handle_lock(int client_sock, const NFSHeader& header, const uint8_t* data);
    void handle_unlock(int client_sock, const NFSHeader& header, const uint8_t* data);
    void handle_reclaim_complete(int client_sock, const NFSHeader& header, const uint8_t* data);
    void handle_setclientid(int client_sock, const NFSHeader& header, const uint8_t* data);
    void handle_setclientid_confirm(int client_sock, const NFSHeader& header, const uint8_t* data);
    void handle_create_session(int client_sock, const NFSHeader& header, const uint8_t* data);
    void handle_destroy_session(int client_sock, const NFSHeader& header, const uint8_t* data);
    void handle_sequence(int client_sock, const NFSHeader& header, const uint8_t* data);

    // Helper functions
    NFSType get_nfs_type(mode_t mode);
    std::string translate_handle_to_path(const std::vector<uint8_t>& handle);
    NFSFileHandle create_file_handle(const std::string& path);

    bool is_initialized_;
    bool is_running_;
    int sock_;

    // Handle-to-path mapping
    std::unordered_map<std::string, std::vector<uint8_t>> path_to_handle_;
    std::unordered_map<std::string, std::string> handle_to_path_;
    std::mutex handle_map_mutex_;
    std::string root_path_;

    // Helper for handle generation
    std::vector<uint8_t> generate_handle(const std::string& path);

    // Add lock tracking structures
    struct LockRegion {
        uint64_t offset;
        uint64_t length;
        NFSLockType type;
        int client_sock;
        std::chrono::steady_clock::time_point timestamp;
    };

    struct FileLocks {
        std::vector<LockRegion> regions;
        std::mutex mutex;
        std::condition_variable cond;
    };

    // Add to private members
    std::unordered_map<std::string, FileLocks> file_locks_;

    // Add to private section
    struct LockWaiter {
        int client_sock;
        std::string path;
        uint64_t offset;
        uint64_t length;
        NFSLockType type;
    };

    // Add to private members
    std::unordered_map<int, std::vector<LockWaiter>> waiting_for_;  // client_sock -> locks waiting for
    std::mutex deadlock_mutex_;

    // Add helper function declarations
    bool check_deadlock(int client_sock, const std::string& path, uint64_t offset, uint64_t length);
    void remove_waiter(int client_sock);

    // Add to private section
    bool try_upgrade_lock(const std::string& path, uint64_t offset, uint64_t length, int client_sock);
    bool try_downgrade_lock(const std::string& path, uint64_t offset, uint64_t length, int client_sock);

    // Add to private section
    void try_coalesce_locks(const std::string& path, int client_sock);
    bool can_merge_locks(const LockRegion& a, const LockRegion& b);

    // Add to private section
    void split_lock_region(const std::string& path, const LockRegion& region, uint64_t split_offset, uint64_t split_length);

    // Add to private section
    void cleanup_stale_locks();
    std::thread cleanup_thread_;
    std::atomic<bool> cleanup_running_{false};
    static constexpr auto LOCK_TIMEOUT = std::chrono::minutes(5);  // 5 minute timeout

    // Add statistics structures
    LockStats lock_stats_;  // Now using the non-nested LockStats

    // Add to private members
    StateManager state_manager_;
    std::thread state_cleanup_thread_;

    // Add state recovery methods
    bool verify_recovery_verifier(const std::string& client_id, const std::vector<uint8_t>& verifier);
    void start_client_recovery(const std::string& client_id);
    void complete_client_recovery(const std::string& client_id);

    // Add to private members
    std::unique_ptr<MetricsExporter> metrics_exporter_;

    // Add to private members
    std::vector<std::unique_ptr<AlertHandler>> alert_handlers_;

    // Add to private members
    SessionManager session_manager_;
    std::unique_ptr<SessionAwareRPCHandler> rpc_handler_;
};

} // namespace fuse_t 
#pragma once
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include <chrono>
#include "nfs_protocol.hpp"
#include "operation_journal.hpp"
#include "util/logger.hpp"

namespace fused {

struct ReplayOperation {
    uint64_t sequence_id;
    NFSProcedure procedure;
    std::vector<uint8_t> arguments;
    std::string target_path;
    std::chrono::steady_clock::time_point timestamp;
    std::vector<uint64_t> dependencies;
    bool idempotent{false};
    uint32_t retry_count{0};
};

enum class ReplayStatus {
    SUCCESS,
    RETRY_NEEDED,
    PERMANENT_FAILURE,
    DEPENDENCY_FAILED
};

class OperationReplaySystem {
public:
    OperationReplaySystem(OperationJournal& journal);
    ~OperationReplaySystem();

    // Initialize replay system
    bool initialize();

    // Replay operations
    bool queue_operation(const ReplayOperation& op);
    bool replay_operation(uint64_t sequence_id);
    bool replay_batch(const std::vector<uint64_t>& sequence_ids);

    // Dependency management
    void add_dependency(uint64_t op_id, uint64_t depends_on);
    bool check_dependencies(uint64_t op_id) const;
    std::vector<uint64_t> get_dependencies(uint64_t op_id) const;

    // Replay verification
    bool verify_replay_result(uint64_t op_id, NFSStatus result);
    bool verify_replay_order(const std::vector<uint64_t>& sequence_ids);

    // Status checks
    bool is_operation_completed(uint64_t sequence_id) const;
    ReplayStatus get_operation_status(uint64_t sequence_id) const;

private:
    OperationJournal& journal_;
    std::mutex mutex_;
    std::queue<ReplayOperation> replay_queue_;
    std::unordered_map<uint64_t, ReplayOperation> active_operations_;
    std::atomic<bool> running_{true};
    std::thread replay_thread_;

    static constexpr auto MAX_RETRY_COUNT = 3;
    static constexpr auto RETRY_DELAY = std::chrono::seconds(1);

    // Replay processing
    void process_replay_queue();
    ReplayStatus execute_replay(const ReplayOperation& op);
    bool handle_replay_result(uint64_t sequence_id, ReplayStatus status);

    // Verification helpers
    bool verify_operation_state(const ReplayOperation& op);
    bool verify_idempotency(const ReplayOperation& op);
    bool verify_dependencies(const ReplayOperation& op);

    // Error handling
    void handle_replay_error(uint64_t sequence_id, const std::string& error);
    bool attempt_error_recovery(uint64_t sequence_id);

    // Dependency tracking
    struct DependencyNode {
        uint64_t sequence_id;
        std::vector<uint64_t> dependencies;
        std::vector<uint64_t> dependents;
        bool completed{false};
    };
    std::unordered_map<uint64_t, DependencyNode> dependency_graph_;

    bool add_to_dependency_graph(const ReplayOperation& op);
    bool check_dependency_cycle(uint64_t op_id) const;
    std::vector<uint64_t> get_ready_operations() const;
};

} // namespace fuse_t 
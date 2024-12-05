#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include "nfs_protocol.hpp"
#include "operation_journal.hpp"
#include "util/logger.hpp"

namespace fused {

enum class OrderingConstraint {
    STRICT,      // Operations must be executed in exact sequence
    CAUSAL,      // Operations must respect causal dependencies
    CONCURRENT,  // Operations can be executed concurrently if no conflicts
    RELAXED      // Operations can be reordered freely
};

struct OrderingRequirement {
    OrderingConstraint constraint;
    std::vector<uint64_t> dependencies;
    std::string conflict_domain;
    bool require_verification{false};
    std::chrono::milliseconds timeout{5000};  // 5 seconds default
};

class OperationOrderingManager {
public:
    OperationOrderingManager(OperationJournal& journal);
    ~OperationOrderingManager();

    // Initialize manager
    bool initialize();

    // Operation ordering
    bool order_operation(uint64_t op_id, const OrderingRequirement& requirement);
    bool verify_order(uint64_t op_id);
    bool wait_for_dependencies(uint64_t op_id);

    // Conflict detection
    bool check_conflicts(uint64_t op_id, const std::string& path);
    bool register_conflict_domain(const std::string& domain);
    bool add_to_conflict_domain(const std::string& domain, const std::string& path);

    // Dependency management
    void add_dependency(uint64_t op_id, uint64_t depends_on);
    bool verify_dependencies(uint64_t op_id);
    std::vector<uint64_t> get_dependencies(uint64_t op_id) const;

    // Serialization points
    uint64_t create_serialization_point();
    bool wait_for_serialization(uint64_t point_id);
    bool verify_serialization_point(uint64_t point_id);

private:
    OperationJournal& journal_;
    std::mutex mutex_;
    std::atomic<uint64_t> next_point_id_{1};
    std::thread ordering_thread_;
    std::atomic<bool> running_{true};

    static constexpr auto ORDERING_INTERVAL = std::chrono::milliseconds(100);
    static constexpr auto MAX_WAIT_TIME = std::chrono::seconds(5);

    // Operation tracking
    struct OrderedOperation {
        uint64_t operation_id;
        OrderingConstraint constraint;
        std::vector<uint64_t> dependencies;
        std::string conflict_domain;
        std::chrono::steady_clock::time_point timestamp;
        bool verified{false};
        bool completed{false};
    };
    std::unordered_map<uint64_t, OrderedOperation> ordered_operations_;

    // Conflict domain tracking
    struct ConflictDomain {
        std::string name;
        std::vector<std::string> paths;
        std::vector<uint64_t> active_operations;
    };
    std::unordered_map<std::string, ConflictDomain> conflict_domains_;

    // Serialization point tracking
    struct SerializationPoint {
        uint64_t point_id;
        std::chrono::steady_clock::time_point timestamp;
        std::vector<uint64_t> operations;
        bool verified{false};
    };
    std::unordered_map<uint64_t, SerializationPoint> serialization_points_;

    // Ordering thread
    void run_ordering_loop();
    void process_ordered_operations();
    void cleanup_completed_operations();

    // Helper methods
    bool verify_operation_order(const OrderedOperation& op);
    bool check_causal_dependencies(const OrderedOperation& op);
    bool detect_path_conflicts(const OrderedOperation& op);
    bool enforce_serialization_order(const OrderedOperation& op);
    void handle_ordering_violation(uint64_t op_id, const std::string& reason);
};

} // namespace fuse_t 
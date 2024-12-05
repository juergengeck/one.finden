#pragma once
#include <atomic>
#include <chrono>
#include <unordered_map>
#include "histogram.hpp"
#include "logger.hpp"

namespace fused {

struct ReplayMetrics {
    // Overall metrics
    std::atomic<uint64_t> total_replays{0};
    std::atomic<uint64_t> successful_replays{0};
    std::atomic<uint64_t> failed_replays{0};
    std::atomic<uint64_t> total_replay_time_us{0};
    
    // Per-operation type metrics
    struct OperationMetrics {
        std::atomic<uint64_t> attempts{0};
        std::atomic<uint64_t> successes{0};
        std::atomic<uint64_t> failures{0};
        Histogram latency_histogram;
    };
    std::unordered_map<NFSProcedure, OperationMetrics> operation_metrics;
    
    // Dependency metrics
    std::atomic<uint64_t> dependency_violations{0};
    std::atomic<uint64_t> path_conflicts{0};
    std::atomic<uint64_t> ordering_violations{0};
    
    // Verification metrics
    std::atomic<uint64_t> verification_failures{0};
    std::atomic<uint64_t> idempotency_failures{0};
    std::atomic<uint64_t> consistency_violations{0};
};

class ReplayMetricsManager {
public:
    // Record replay attempt
    void record_replay(NFSProcedure proc, 
                      std::chrono::microseconds duration,
                      bool success);
    
    // Record verification issues
    void record_dependency_violation(uint32_t op_id);
    void record_path_conflict(uint32_t op1, uint32_t op2);
    void record_ordering_violation(uint32_t op_id);
    void record_verification_failure(uint32_t op_id, const std::string& reason);
    void record_idempotency_failure(uint32_t op_id);
    void record_consistency_violation(uint32_t op_id);
    
    // Get metrics
    const ReplayMetrics& get_metrics() const { return metrics_; }
    
    // Log metrics summary
    void log_metrics() const;

private:
    ReplayMetrics metrics_;
    mutable std::mutex mutex_;
};

// Global metrics instance
inline ReplayMetricsManager& get_replay_metrics() {
    static ReplayMetricsManager manager;
    return manager;
}

} // namespace fuse_t 
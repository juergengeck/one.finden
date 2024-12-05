#include "replay_metrics.hpp"

namespace fused {

void ReplayMetricsManager::record_replay(NFSProcedure proc,
                                       std::chrono::microseconds duration,
                                       bool success) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Update overall metrics
    metrics_.total_replays++;
    if (success) {
        metrics_.successful_replays++;
    } else {
        metrics_.failed_replays++;
    }
    metrics_.total_replay_time_us += duration.count();
    
    // Update per-operation metrics
    auto& op_metrics = metrics_.operation_metrics[proc];
    op_metrics.attempts++;
    if (success) {
        op_metrics.successes++;
    } else {
        op_metrics.failures++;
    }
    op_metrics.latency_histogram.record(duration.count());
}

void ReplayMetricsManager::record_dependency_violation(uint32_t op_id) {
    metrics_.dependency_violations++;
    LOG_WARN("Dependency violation detected for operation {}", op_id);
}

void ReplayMetricsManager::record_path_conflict(uint32_t op1, uint32_t op2) {
    metrics_.path_conflicts++;
    LOG_WARN("Path conflict detected between operations {} and {}", op1, op2);
}

void ReplayMetricsManager::record_ordering_violation(uint32_t op_id) {
    metrics_.ordering_violations++;
    LOG_WARN("Ordering violation detected for operation {}", op_id);
}

void ReplayMetricsManager::record_verification_failure(uint32_t op_id,
                                                     const std::string& reason) {
    metrics_.verification_failures++;
    LOG_ERROR("Verification failed for operation {}: {}", op_id, reason);
}

void ReplayMetricsManager::record_idempotency_failure(uint32_t op_id) {
    metrics_.idempotency_failures++;
    LOG_ERROR("Idempotency failure detected for operation {}", op_id);
}

void ReplayMetricsManager::record_consistency_violation(uint32_t op_id) {
    metrics_.consistency_violations++;
    LOG_ERROR("Consistency violation detected for operation {}", op_id);
}

void ReplayMetricsManager::log_metrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    LOG_INFO("Replay Metrics Summary:");
    LOG_INFO("  Total replays: {}", metrics_.total_replays.load());
    LOG_INFO("  Success rate: {:.1f}%", 
        100.0 * metrics_.successful_replays.load() / metrics_.total_replays.load());
    
    uint64_t total_time = metrics_.total_replay_time_us.load();
    if (metrics_.total_replays > 0) {
        LOG_INFO("  Average replay time: {:.2f}us", 
            static_cast<double>(total_time) / metrics_.total_replays.load());
    }
    
    LOG_INFO("Verification Issues:");
    LOG_INFO("  Dependency violations: {}", metrics_.dependency_violations.load());
    LOG_INFO("  Path conflicts: {}", metrics_.path_conflicts.load());
    LOG_INFO("  Ordering violations: {}", metrics_.ordering_violations.load());
    LOG_INFO("  Verification failures: {}", metrics_.verification_failures.load());
    LOG_INFO("  Idempotency failures: {}", metrics_.idempotency_failures.load());
    LOG_INFO("  Consistency violations: {}", metrics_.consistency_violations.load());
    
    LOG_INFO("Per-Operation Metrics:");
    for (const auto& [proc, op_metrics] : metrics_.operation_metrics) {
        uint64_t attempts = op_metrics.attempts.load();
        if (attempts > 0) {
            double success_rate = 100.0 * op_metrics.successes.load() / attempts;
            auto latency_stats = op_metrics.latency_histogram.get_stats();
            
            LOG_INFO("  {}: {} attempts, {:.1f}% success, p50={:.2f}us, p99={:.2f}us",
                static_cast<int>(proc), attempts, success_rate,
                latency_stats.p50, latency_stats.p99);
        }
    }
}

} // namespace fuse_t 
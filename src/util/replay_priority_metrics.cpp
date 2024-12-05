#include "replay_priority_metrics.hpp"
#include "logger.hpp"

namespace fused {

void ReplayPriorityMetrics::record_operation(ReplayPriority priority,
                                           std::chrono::microseconds latency,
                                           bool success) {
    auto& metrics = priority_metrics_[static_cast<size_t>(priority)];
    metrics.operations++;
    if (success) {
        metrics.successful++;
    } else {
        metrics.failed++;
    }
    metrics.total_latency_us += latency.count();
    metrics.latency_histogram.record(latency.count());
}

void ReplayPriorityMetrics::record_batch(const ReplayBatch& batch,
                                       std::chrono::microseconds processing_time,
                                       bool success) {
    if (batch.urgent) {
        urgent_batch_count_++;
    } else {
        normal_batch_count_++;
    }

    batch_size_histogram_.record(batch.operations.size());
    batch_processing_time_histogram_.record(processing_time.count());

    // Record individual operation metrics
    for (const auto& op : batch.operations) {
        auto priority = ReplayPriorityCalculator::calculate_priority(op);
        record_operation(priority, processing_time, success);
    }
}

void ReplayPriorityMetrics::record_priority_violation(ReplayPriority expected,
                                                    ReplayPriority actual) {
    priority_violations_++;
    LOG_WARN("Priority violation: expected {} but got {}",
        static_cast<int>(expected), static_cast<int>(actual));
}

void ReplayPriorityMetrics::log_metrics() const {
    LOG_INFO("Replay Priority Metrics:");
    
    // Log metrics for each priority level
    const char* priority_names[] = {
        "CRITICAL", "HIGH", "NORMAL", "LOW", "BACKGROUND"
    };

    for (size_t i = 0; i < priority_metrics_.size(); i++) {
        const auto& metrics = priority_metrics_[i];
        uint64_t total = metrics.operations.load();
        if (total > 0) {
            double success_rate = 100.0 * metrics.successful.load() / total;
            double avg_latency = static_cast<double>(metrics.total_latency_us.load()) / total;
            auto latency_stats = metrics.latency_histogram.get_stats();

            LOG_INFO("  {} Priority:", priority_names[i]);
            LOG_INFO("    Operations: {} ({:.1f}% success)", 
                total, success_rate);
            LOG_INFO("    Latency: avg={:.2f}us, p50={:.2f}us, p99={:.2f}us",
                avg_latency, latency_stats.p50, latency_stats.p99);
        }
    }

    // Log batch statistics
    auto batch_size_stats = batch_size_histogram_.get_stats();
    auto processing_time_stats = batch_processing_time_histogram_.get_stats();
    
    LOG_INFO("Batch Statistics:");
    LOG_INFO("  Urgent batches: {}", urgent_batch_count_.load());
    LOG_INFO("  Normal batches: {}", normal_batch_count_.load());
    LOG_INFO("  Batch size: avg={:.1f}, p50={:.1f}, p99={:.1f}",
        batch_size_stats.mean, batch_size_stats.p50, batch_size_stats.p99);
    LOG_INFO("  Processing time: avg={:.2f}us, p50={:.2f}us, p99={:.2f}us",
        processing_time_stats.mean, processing_time_stats.p50, processing_time_stats.p99);

    LOG_INFO("Priority violations: {}", priority_violations_.load());
}

} // namespace fuse_t 
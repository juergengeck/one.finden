#pragma once
#include <atomic>
#include <array>
#include <chrono>
#include "histogram.hpp"
#include "replay_priority.hpp"

namespace fused {

struct PriorityMetrics {
    std::atomic<uint64_t> operations{0};
    std::atomic<uint64_t> successful{0};
    std::atomic<uint64_t> failed{0};
    std::atomic<uint64_t> total_latency_us{0};
    Histogram latency_histogram;
};

class ReplayPriorityMetrics {
public:
    // Record operation metrics by priority
    void record_operation(ReplayPriority priority,
                         std::chrono::microseconds latency,
                         bool success);

    // Record batch metrics
    void record_batch(const ReplayBatch& batch,
                     std::chrono::microseconds processing_time,
                     bool success);

    // Record priority violations (when lower priority ops processed before higher)
    void record_priority_violation(ReplayPriority expected,
                                 ReplayPriority actual);

    // Get metrics
    const PriorityMetrics& get_metrics_for_priority(ReplayPriority priority) const {
        return priority_metrics_[static_cast<size_t>(priority)];
    }

    // Log metrics summary
    void log_metrics() const;

private:
    // Metrics per priority level
    std::array<PriorityMetrics, 5> priority_metrics_;  // One per ReplayPriority

    // Priority violation tracking
    std::atomic<uint64_t> priority_violations_{0};
    std::atomic<uint64_t> urgent_batch_count_{0};
    std::atomic<uint64_t> normal_batch_count_{0};

    // Batch size tracking
    Histogram batch_size_histogram_;
    Histogram batch_processing_time_histogram_;
};

// Global metrics instance
inline ReplayPriorityMetrics& get_priority_metrics() {
    static ReplayPriorityMetrics metrics;
    return metrics;
}

} // namespace fuse_t 
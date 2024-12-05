#pragma once
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "operation_log.hpp"
#include "util/logger.hpp"
#include "replay_priority.hpp"

namespace fused {

struct ReplayBatch {
    std::vector<LoggedOperation> operations;
    std::chrono::steady_clock::time_point deadline;
    ReplayPriority priority{ReplayPriority::NORMAL};
    bool urgent{false};

    // Compare batches by priority
    bool operator<(const ReplayBatch& other) const {
        if (urgent != other.urgent) {
            return other.urgent;  // Urgent batches have higher priority
        }
        return static_cast<uint32_t>(priority) > static_cast<uint32_t>(other.priority);
    }
};

class ReplayBatchManager {
public:
    explicit ReplayBatchManager(size_t batch_size = 64)
        : max_batch_size_(batch_size) {}
    
    // Initialize batch manager
    bool initialize();
    void stop();

    // Add operation to batch
    void add_operation(LoggedOperation op, bool urgent = false);

    // Get next batch to process
    bool get_next_batch(ReplayBatch& batch, std::chrono::milliseconds timeout);

    // Batch management
    void complete_batch(const ReplayBatch& batch);
    void fail_batch(const ReplayBatch& batch, const std::string& reason);
    
    // Status
    size_t get_pending_count() const { return pending_count_.load(); }
    size_t get_active_count() const { return active_count_.load(); }

private:
    const size_t max_batch_size_;
    std::atomic<bool> running_{false};
    std::atomic<size_t> pending_count_{0};
    std::atomic<size_t> active_count_{0};

    // Batch queues
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::priority_queue<ReplayBatch> normal_queue_;
    std::priority_queue<ReplayBatch> urgent_queue_;

    // Current batch being built
    ReplayBatch current_batch_;
    std::chrono::steady_clock::time_point batch_start_;

    static constexpr auto MAX_BATCH_AGE = std::chrono::seconds(1);
    static constexpr auto MIN_BATCH_SIZE = 8;

    // Helper methods
    bool should_flush_batch() const;
    void flush_current_batch();
    void add_to_queue(ReplayBatch&& batch);
};

} // namespace fuse_t 
#include "replay_batch.hpp"
#include "util/replay_metrics.hpp"
#include "replay_priority.hpp"
#include "util/replay_priority_metrics.hpp"

namespace fused {

bool ReplayBatchManager::initialize() {
    if (running_) {
        return true;
    }

    LOG_INFO("Initializing replay batch manager with batch size {}", max_batch_size_);
    running_ = true;
    batch_start_ = std::chrono::steady_clock::now();
    return true;
}

void ReplayBatchManager::stop() {
    if (!running_) {
        return;
    }

    LOG_INFO("Stopping replay batch manager");
    running_ = false;
    queue_cv_.notify_all();

    // Flush any remaining operations
    flush_current_batch();
}

void ReplayBatchManager::add_operation(LoggedOperation op, bool urgent) {
    if (!running_) {
        LOG_ERROR("Cannot add operation to stopped batch manager");
        return;
    }

    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    // If current batch is full or too old, flush it
    if (should_flush_batch()) {
        flush_current_batch();
        batch_start_ = std::chrono::steady_clock::now();
    }

    // If this is an urgent operation, create a separate batch
    if (urgent) {
        ReplayBatch urgent_batch;
        urgent_batch.operations.push_back(std::move(op));
        urgent_batch.deadline = std::chrono::steady_clock::now() + 
            std::chrono::milliseconds(100);
        urgent_batch.urgent = true;
        urgent_batch.priority = ReplayPriority::CRITICAL;
        add_to_queue(std::move(urgent_batch));
        return;
    }

    // Add to current batch
    current_batch_.operations.push_back(std::move(op));
    
    // Update batch priority based on new operation
    auto op_priority = ReplayPriorityCalculator::calculate_priority(op);
    if (static_cast<uint32_t>(op_priority) < static_cast<uint32_t>(current_batch_.priority)) {
        current_batch_.priority = op_priority;
    }
    
    pending_count_++;

    // If batch is now full, flush it
    if (current_batch_.operations.size() >= max_batch_size_) {
        flush_current_batch();
        batch_start_ = std::chrono::steady_clock::now();
    }
}

bool ReplayBatchManager::get_next_batch(ReplayBatch& batch, 
                                      std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    
    auto deadline = std::chrono::steady_clock::now() + timeout;
    bool success = queue_cv_.wait_until(lock, deadline, [this]() {
        return !running_ || !urgent_queue_.empty() || !normal_queue_.empty();
    });

    if (!success || !running_) {
        return false;
    }

    // Prioritize urgent batches
    if (!urgent_queue_.empty()) {
        batch = std::move(urgent_queue_.top());
        urgent_queue_.pop();
    } else if (!normal_queue_.empty()) {
        batch = std::move(normal_queue_.top());
        normal_queue_.pop();
    } else {
        return false;
    }

    active_count_ += batch.operations.size();
    pending_count_ -= batch.operations.size();
    return true;
}

void ReplayBatchManager::complete_batch(const ReplayBatch& batch) {
    active_count_ -= batch.operations.size();
    
    auto processing_time = std::chrono::steady_clock::now() - batch.deadline;
    get_priority_metrics().record_batch(batch, 
        std::chrono::duration_cast<std::chrono::microseconds>(processing_time),
        true);
    
    // Record batch metrics
    auto& metrics = get_replay_metrics();
    for (const auto& op : batch.operations) {
        metrics.record_replay(op.procedure, 
            std::chrono::duration_cast<std::chrono::microseconds>(processing_time),
            true);  // Success
    }
}

void ReplayBatchManager::fail_batch(const ReplayBatch& batch, 
                                  const std::string& reason) {
    active_count_ -= batch.operations.size();
    
    auto processing_time = std::chrono::steady_clock::now() - batch.deadline;
    get_priority_metrics().record_batch(batch,
        std::chrono::duration_cast<std::chrono::microseconds>(processing_time),
        false);
    
    // Record failure metrics
    auto& metrics = get_replay_metrics();
    for (const auto& op : batch.operations) {
        metrics.record_replay(op.procedure, 
            std::chrono::duration_cast<std::chrono::microseconds>(processing_time),
            false);  // Failure
        metrics.record_verification_failure(op.operation_id, reason);
    }

    LOG_ERROR("Batch replay failed: {}", reason);
}

bool ReplayBatchManager::should_flush_batch() const {
    if (current_batch_.operations.empty()) {
        return false;
    }

    // Flush if batch is full
    if (current_batch_.operations.size() >= max_batch_size_) {
        return true;
    }

    // Flush if batch is old enough and has minimum size
    auto batch_age = std::chrono::steady_clock::now() - batch_start_;
    return batch_age >= MAX_BATCH_AGE && 
           current_batch_.operations.size() >= MIN_BATCH_SIZE;
}

void ReplayBatchManager::flush_current_batch() {
    if (current_batch_.operations.empty()) {
        return;
    }

    current_batch_.deadline = std::chrono::steady_clock::now() + 
        std::chrono::seconds(5);
    
    // Final priority calculation for the batch
    if (!current_batch_.urgent) {
        current_batch_.priority = static_cast<ReplayPriority>(
            ReplayPriorityCalculator::calculate_batch_priority(current_batch_));
    }
    
    add_to_queue(std::move(current_batch_));
    current_batch_ = ReplayBatch{};
}

void ReplayBatchManager::add_to_queue(ReplayBatch&& batch) {
    if (batch.urgent) {
        urgent_queue_.push(std::move(batch));
    } else {
        normal_queue_.push(std::move(batch));
    }
    queue_cv_.notify_one();
}

} // namespace fuse_t 
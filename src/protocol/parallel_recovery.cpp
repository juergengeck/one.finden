#include "parallel_recovery.hpp"

namespace fused {

bool ParallelRecoveryManager::initialize() {
    if (running_) {
        return true;
    }

    LOG_INFO("Initializing parallel recovery manager with {} threads", thread_count_);
    running_ = true;

    // Start worker threads
    for (size_t i = 0; i < thread_count_; ++i) {
        worker_threads_.emplace_back([this]() { worker_loop(); });
    }

    return true;
}

void ParallelRecoveryManager::stop() {
    if (!running_) {
        return;
    }

    LOG_INFO("Stopping parallel recovery manager");
    running_ = false;
    queue_cv_.notify_all();

    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    worker_threads_.clear();
}

void ParallelRecoveryManager::schedule_recovery(RecoveryTask task) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push(std::move(task));
        pending_count_++;
    }
    queue_cv_.notify_one();
}

bool ParallelRecoveryManager::wait_for_recovery(const std::string& client_id,
                                              std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unique_lock<std::mutex> lock(recovery_mutex_);
    
    return queue_cv_.wait_until(lock, deadline, [this, &client_id]() {
        return completed_recoveries_.find(client_id) != completed_recoveries_.end();
    });
}

bool ParallelRecoveryManager::is_recovery_complete(const std::string& client_id) const {
    std::lock_guard<std::mutex> lock(recovery_mutex_);
    auto it = completed_recoveries_.find(client_id);
    return it != completed_recoveries_.end();
}

void ParallelRecoveryManager::worker_loop() {
    while (running_) {
        RecoveryTask task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() {
                return !running_ || !task_queue_.empty();
            });

            if (!running_) {
                break;
            }

            task = std::move(task_queue_.front());
            task_queue_.pop();
            pending_count_--;
            active_count_++;
        }

        process_task(task);
        active_count_--;
    }
}

void ParallelRecoveryManager::process_task(const RecoveryTask& task) {
    LOG_INFO("Starting parallel recovery for client {} session {}", 
        task.client_id, task.session_id);

    try {
        // Process operations in parallel using thread pool
        std::vector<std::future<bool>> results;
        std::atomic<size_t> successful_ops{0};
        
        // Split operations into chunks for parallel processing
        const size_t chunk_size = std::max(size_t(1), task.operations.size() / thread_count_);
        
        for (size_t i = 0; i < task.operations.size(); i += chunk_size) {
            size_t end = std::min(i + chunk_size, task.operations.size());
            std::vector<uint32_t> chunk(
                task.operations.begin() + i,
                task.operations.begin() + end
            );
            
            results.push_back(std::async(std::launch::async,
                [this, &task, chunk = std::move(chunk), &successful_ops]() {
                    for (uint32_t op : chunk) {
                        if (recover_operation(task.client_id, task.session_id, op)) {
                            successful_ops++;
                        }
                    }
                    return true;
                }));
        }
        
        // Wait for all operations to complete
        for (auto& result : results) {
            result.wait();
        }

        LOG_INFO("Recovered {}/{} operations for client {} session {}", 
            successful_ops.load(), task.operations.size(),
            task.client_id, task.session_id);

        // Mark recovery as complete
        {
            std::lock_guard<std::mutex> lock(recovery_mutex_);
            completed_recoveries_[task.client_id] = true;
        }
        queue_cv_.notify_all();

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to recover client {} session {}: {}", 
            task.client_id, task.session_id, e.what());
    }
}

bool ParallelRecoveryManager::recover_operation(
    const std::string& client_id, 
    uint32_t session_id,
    uint32_t operation_id) {
    
    try {
        auto& op_log = get_operation_log();
        
        // Check if operation needs replay
        if (!op_log.needs_replay(operation_id)) {
            LOG_DEBUG("Operation {} does not need replay", operation_id);
            return true;
        }
        
        // Get operation details
        auto ops = op_log.get_incomplete_operations(client_id, session_id);
        auto it = std::find_if(ops.begin(), ops.end(),
            [operation_id](const LoggedOperation& op) {
                return op.operation_id == operation_id;
            });
            
        if (it == ops.end()) {
            LOG_ERROR("Operation {} not found in log", operation_id);
            return false;
        }
        
        // Replay the operation
        NFSStatus result = op_log.replay_operation(*it);
        if (result != NFSStatus::OK) {
            LOG_ERROR("Failed to replay operation {}: {}", 
                operation_id, static_cast<int>(result));
            return false;
        }
        
        // Mark operation as completed
        op_log.complete_operation(operation_id, result);
         
        LOG_DEBUG("Recovered operation {} for client {} session {}", 
            operation_id, client_id, session_id);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to recover operation {} for client {} session {}: {}", 
            operation_id, client_id, session_id, e.what());
        return false;
    }
}

} // namespace fuse_t 
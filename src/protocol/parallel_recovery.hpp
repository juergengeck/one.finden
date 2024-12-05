#pragma once
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include "session_recovery.hpp"
#include "util/logger.hpp"

namespace fused {

struct RecoveryTask {
    std::string client_id;
    uint32_t session_id;
    std::vector<uint32_t> operations;
    std::chrono::steady_clock::time_point start_time;
    uint32_t priority{0};
};

class ParallelRecoveryManager {
public:
    explicit ParallelRecoveryManager(size_t thread_count = std::thread::hardware_concurrency())
        : thread_count_(thread_count) {}
    
    ~ParallelRecoveryManager() {
        stop();
    }

    bool initialize();
    void stop();

    // Add recovery task to queue
    void schedule_recovery(RecoveryTask task);

    // Wait for specific recovery to complete
    bool wait_for_recovery(const std::string& client_id, 
                         std::chrono::milliseconds timeout);

    // Get recovery status
    bool is_recovery_complete(const std::string& client_id) const;
    size_t get_pending_count() const { return pending_count_.load(); }
    size_t get_active_count() const { return active_count_.load(); }

private:
    const size_t thread_count_;
    std::vector<std::thread> worker_threads_;
    std::atomic<bool> running_{false};

    // Task queue
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<RecoveryTask> task_queue_;

    // Recovery tracking
    std::mutex recovery_mutex_;
    std::unordered_map<std::string, bool> completed_recoveries_;
    std::atomic<size_t> pending_count_{0};
    std::atomic<size_t> active_count_{0};

    // Worker thread function
    void worker_loop();
    void process_task(const RecoveryTask& task);
};

} // namespace fuse_t 
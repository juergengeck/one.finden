#pragma once
#include <chrono>
#include <string>
#include <unordered_map>
#include <mutex>
#include "logger.hpp"
#include <atomic>

namespace fused {

class OperationMetrics {
public:
    void start_operation(const std::string& op_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& metric = metrics_[op_name];
        metric.start_time = std::chrono::steady_clock::now();
        metric.in_progress++;
        metric.total_calls++;
    }
    
    void end_operation(const std::string& op_name, bool success = true) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& metric = metrics_[op_name];
        auto duration = std::chrono::steady_clock::now() - metric.start_time;
        metric.total_time += std::chrono::duration_cast<std::chrono::microseconds>(duration);
        metric.in_progress--;
        if (success) {
            metric.successful_calls++;
        }
        
        // Log if operation took longer than threshold (e.g., 100ms)
        if (duration > std::chrono::milliseconds(100)) {
            LOG_WARN("Operation {} took {}ms", op_name,
                std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
        }
    }
    
    void log_metrics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [op_name, metric] : metrics_) {
            LOG_INFO("Operation {}: {} calls ({} successful), avg time {}us, {} in progress",
                op_name,
                metric.total_calls,
                metric.successful_calls,
                metric.total_calls > 0 ? 
                    metric.total_time.count() / metric.total_calls : 0,
                metric.in_progress);
        }
    }

private:
    struct Metric {
        std::chrono::steady_clock::time_point start_time;
        std::chrono::microseconds total_time{0};
        uint64_t total_calls{0};
        uint64_t successful_calls{0};
        int32_t in_progress{0};
    };
    
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Metric> metrics_;
};

// Global metrics instance
inline OperationMetrics& get_metrics() {
    static OperationMetrics metrics;
    return metrics;
}

// RAII helper for operation timing
class ScopedOperation {
public:
    ScopedOperation(const std::string& op_name) 
        : op_name_(op_name) {
        get_metrics().start_operation(op_name_);
    }
    
    ~ScopedOperation() {
        get_metrics().end_operation(op_name_, success_);
    }
    
    void set_success(bool success) {
        success_ = success;
    }

private:
    std::string op_name_;
    bool success_{true};
};

#define SCOPED_OPERATION(name) \
    ScopedOperation _operation_timer(name)

#define OPERATION_SUCCESS() \
    _operation_timer.set_success(true)

#define OPERATION_FAILURE() \
    _operation_timer.set_success(false)

// Add to LockStats structure
struct LockStats {
    // Existing lock metrics
    std::atomic<uint64_t> lock_attempts{0};
    std::atomic<uint64_t> lock_successes{0};
    std::atomic<uint64_t> lock_failures{0};
    std::atomic<uint64_t> deadlocks{0};
    std::atomic<uint64_t> timeouts{0};
    std::atomic<uint64_t> upgrades{0};
    std::atomic<uint64_t> downgrades{0};
    std::atomic<uint64_t> total_wait_time_ms{0};

    // I/O metrics
    std::atomic<uint64_t> bytes_read{0};
    std::atomic<uint64_t> bytes_written{0};
    std::atomic<uint64_t> read_ops{0};
    std::atomic<uint64_t> write_ops{0};
    std::atomic<uint64_t> read_errors{0};
    std::atomic<uint64_t> write_errors{0};
    std::atomic<uint64_t> total_read_time_ms{0};
    std::atomic<uint64_t> total_write_time_ms{0};
};

} // namespace fuse_t 
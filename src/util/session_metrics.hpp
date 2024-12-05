#pragma once
#include <atomic>
#include <chrono>
#include "histogram.hpp"
#include "logger.hpp"

namespace fused {

struct SessionMetrics {
    // Session counts
    std::atomic<uint64_t> total_sessions{0};
    std::atomic<uint64_t> active_sessions{0};
    std::atomic<uint64_t> expired_sessions{0};
    std::atomic<uint64_t> recovered_sessions{0};

    // Operation metrics
    std::atomic<uint64_t> total_operations{0};
    std::atomic<uint64_t> successful_operations{0};
    std::atomic<uint64_t> failed_operations{0};
    std::atomic<uint64_t> sequence_violations{0};

    // Recovery metrics
    std::atomic<uint64_t> recovery_attempts{0};
    std::atomic<uint64_t> recovery_successes{0};
    std::atomic<uint64_t> recovery_failures{0};
    std::atomic<uint64_t> total_recovery_time_ms{0};

    // Timing histograms
    Histogram session_lifetime;
    Histogram operation_latency;
    Histogram recovery_time;

    void record_session_creation() {
        total_sessions++;
        active_sessions++;
    }

    void record_session_expiry() {
        expired_sessions++;
        active_sessions--;
    }

    void record_session_recovery(bool success, std::chrono::milliseconds duration) {
        recovery_attempts++;
        if (success) {
            recovery_successes++;
            recovered_sessions++;
        } else {
            recovery_failures++;
        }
        total_recovery_time_ms += duration.count();
        recovery_time.record(duration.count());
    }

    void record_operation(bool success, std::chrono::microseconds latency) {
        total_operations++;
        if (success) {
            successful_operations++;
        } else {
            failed_operations++;
        }
        operation_latency.record(latency.count());
    }

    void record_sequence_violation() {
        sequence_violations++;
    }

    void log_metrics() const {
        LOG_INFO("Session Metrics:");
        LOG_INFO("  Sessions: total={}, active={}, expired={}, recovered={}",
            total_sessions.load(), active_sessions.load(),
            expired_sessions.load(), recovered_sessions.load());

        LOG_INFO("  Operations: total={}, successful={}, failed={}, violations={}",
            total_operations.load(), successful_operations.load(),
            failed_operations.load(), sequence_violations.load());

        LOG_INFO("  Recovery: attempts={}, successes={}, failures={}",
            recovery_attempts.load(), recovery_successes.load(),
            recovery_failures.load());

        if (recovery_attempts > 0) {
            double avg_recovery_time = static_cast<double>(total_recovery_time_ms) / 
                recovery_attempts;
            LOG_INFO("  Average recovery time: {:.2f}ms", avg_recovery_time);
        }

        auto op_stats = operation_latency.get_stats();
        LOG_INFO("  Operation latency (us): p50={:.2f}, p90={:.2f}, p99={:.2f}",
            op_stats.p50, op_stats.p90, op_stats.p99);

        auto recovery_stats = recovery_time.get_stats();
        LOG_INFO("  Recovery time (ms): p50={:.2f}, p90={:.2f}, p99={:.2f}",
            recovery_stats.p50, recovery_stats.p90, recovery_stats.p99);
    }
};

// Global session metrics instance
inline SessionMetrics& get_session_metrics() {
    static SessionMetrics metrics;
    return metrics;
}

} // namespace fuse_t 
#pragma once
#include <atomic>
#include <chrono>
#include <string>
#include <unordered_map>
#include "logger.hpp"
#include "histogram.hpp"

namespace fused {

struct RPCOperationMetrics {
    std::atomic<uint64_t> total_calls{0};
    std::atomic<uint64_t> successful_calls{0};
    std::atomic<uint64_t> failed_calls{0};
    std::atomic<uint64_t> total_time_us{0};
    std::atomic<uint64_t> bytes_in{0};
    std::atomic<uint64_t> bytes_out{0};
    Histogram latency_histogram;
};

class RPCMetrics {
public:
    void record_operation(const std::string& operation,
                         bool success,
                         std::chrono::microseconds duration,
                         size_t input_bytes,
                         size_t output_bytes) {
        auto& metrics = operation_metrics_[operation];
        metrics.total_calls++;
        if (success) {
            metrics.successful_calls++;
        } else {
            metrics.failed_calls++;
        }
        metrics.total_time_us += duration.count();
        metrics.bytes_in += input_bytes;
        metrics.bytes_out += output_bytes;
        metrics.latency_histogram.record(duration.count());
    }

    void log_metrics() const {
        LOG_INFO("RPC Operation Metrics:");
        for (const auto& [op, metrics] : operation_metrics_) {
            uint64_t total = metrics.total_calls;
            if (total > 0) {
                double success_rate = 100.0 * metrics.successful_calls / total;
                double avg_time = static_cast<double>(metrics.total_time_us) / total;
                
                auto latency_stats = metrics.latency_histogram.get_stats();
                LOG_INFO("  {}: {} calls ({:.1f}% success)", 
                    op, total, success_rate);
                LOG_INFO("    Latency (us): min={:.2f}, avg={:.2f}, p50={:.2f}, p90={:.2f}, p99={:.2f}, max={:.2f}",
                    latency_stats.min, avg_time, latency_stats.p50,
                    latency_stats.p90, latency_stats.p99, latency_stats.max);
                LOG_INFO("    Throughput: {} bytes in, {} bytes out",
                    metrics.bytes_in.load(), metrics.bytes_out.load());
            }
        }
    }

    const std::unordered_map<std::string, RPCOperationMetrics>& get_metrics() const {
        return operation_metrics_;
    }

private:
    std::unordered_map<std::string, RPCOperationMetrics> operation_metrics_;
};

// Global RPC metrics instance
inline RPCMetrics& get_rpc_metrics() {
    static RPCMetrics metrics;
    return metrics;
}

// RAII helper for measuring RPC operations
class ScopedRPCMetrics {
public:
    ScopedRPCMetrics(const std::string& operation)
        : operation_(operation)
        , start_(std::chrono::steady_clock::now())
        , input_bytes_(0)
        , output_bytes_(0)
        , success_(false) {}

    ~ScopedRPCMetrics() {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_);
        get_rpc_metrics().record_operation(
            operation_, success_, duration, input_bytes_, output_bytes_);
    }

    void set_success(bool success) { success_ = success; }
    void set_input_bytes(size_t bytes) { input_bytes_ = bytes; }
    void set_output_bytes(size_t bytes) { output_bytes_ = bytes; }

private:
    std::string operation_;
    std::chrono::steady_clock::time_point start_;
    size_t input_bytes_;
    size_t output_bytes_;
    bool success_;
};

} // namespace fuse_t 
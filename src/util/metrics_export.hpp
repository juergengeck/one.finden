#pragma once
#include <string>
#include <fstream>
#include <chrono>
#include <thread>
#include "recovery_metrics.hpp"
#include "logger.hpp"
#include "metrics_server.hpp"

namespace fused {

class MetricsExporter {
public:
    // Export formats
    enum class Format {
        JSON,
        CSV,
        PROMETHEUS
    };

    MetricsExporter(const std::string& output_dir, Format format = Format::JSON)
        : output_dir_(output_dir), format_(format) {
        start_export_thread();
    }

    ~MetricsExporter() {
        stop_export_thread();
    }

    // Export metrics immediately
    void export_metrics() {
        const auto& metrics = get_recovery_metrics();
        std::string filename = generate_filename();

        switch (format_) {
            case Format::JSON:
                export_json(filename, metrics);
                break;
            case Format::CSV:
                export_csv(filename, metrics);
                break;
            case Format::PROMETHEUS:
                export_prometheus(filename, metrics);
                break;
        }
    }

    // Get metrics in real-time format
    std::string get_realtime_metrics() const {
        const auto& metrics = get_recovery_metrics();
        const auto& aggregator = get_metrics_aggregator();
        const auto& recovery_metrics = get_session_manager().get_recovery_manager().get_metrics();
        std::stringstream ss;

        // Current recovery status
        ss << "{\n";
        ss << "  \"timestamp\": " << 
            std::chrono::system_clock::now().time_since_epoch().count() << ",\n";
        ss << "  \"current\": {\n";
        ss << "    \"in_recovery\": " << (metrics.recovery_periods > 0) << ",\n";
        ss << "    \"active_recoveries\": " << 
            (metrics.recovery_attempts - metrics.recovery_successes - metrics.recovery_failures) << ",\n";
        ss << "    \"success_rate\": " << 
            (metrics.recovery_attempts > 0 ? 
                100.0 * metrics.recovery_successes / metrics.recovery_attempts : 0) << "\n";
        ss << "  },\n";

        // Historical data
        ss << "  \"historical\": {\n";
        ss << "    \"total_attempts\": " << metrics.recovery_attempts << ",\n";
        ss << "    \"total_successes\": " << metrics.recovery_successes << ",\n";
        ss << "    \"total_failures\": " << metrics.recovery_failures << ",\n";
        ss << "    \"avg_recovery_time\": " << 
            (metrics.recovery_attempts > 0 ? 
                metrics.total_recovery_time_ms / metrics.recovery_attempts : 0) << "\n";
        ss << "  },\n";

        // Add aggregated metrics
        ss << "  \"aggregated\": {\n";
        
        // Minute aggregation
        auto minute_metrics = aggregator.get_minute_metrics();
        ss << "    \"minute\": {\n";
        write_aggregated_metrics(ss, minute_metrics);
        ss << "    },\n";

        // Hour aggregation
        auto hour_metrics = aggregator.get_hour_metrics();
        ss << "    \"hour\": {\n";
        write_aggregated_metrics(ss, hour_metrics);
        ss << "    },\n";

        // Day aggregation
        auto day_metrics = aggregator.get_day_metrics();
        ss << "    \"day\": {\n";
        write_aggregated_metrics(ss, day_metrics);
        ss << "    }\n";

        ss << "  },\n";

        // Add recovery metrics
        ss << "  \"recovery\": {\n";
        ss << "    \"total_recoveries\": " << recovery_metrics.total_recoveries << ",\n";
        ss << "    \"successful_recoveries\": " << recovery_metrics.successful_recoveries << ",\n";
        ss << "    \"failed_recoveries\": " << recovery_metrics.failed_recoveries << ",\n";
        ss << "    \"expired_recoveries\": " << recovery_metrics.expired_recoveries << ",\n";
        ss << "    \"operations_recovered\": " << recovery_metrics.operations_recovered << ",\n";
        
        uint64_t total = recovery_metrics.total_recoveries;
        double success_rate = total > 0 ? 
            static_cast<double>(recovery_metrics.successful_recoveries) / total : 0.0;
        double avg_time = total > 0 ?
            static_cast<double>(recovery_metrics.total_recovery_time_ms) / total : 0.0;
        
        ss << "    \"success_rate\": " << success_rate << ",\n";
        ss << "    \"avg_recovery_time\": " << avg_time << "\n";
        ss << "  }\n";

        ss << "}\n";

        return ss.str();
    }

    // Start real-time metrics server
    void start_realtime_server(uint16_t port = 8080) {
        realtime_server_ = std::make_unique<MetricsServer>(port, [this]() {
            return get_realtime_metrics();
        });
    }

private:
    std::string output_dir_;
    Format format_;
    std::thread export_thread_;
    std::atomic<bool> running_{true};
    std::unique_ptr<MetricsServer> realtime_server_;

    void start_export_thread() {
        export_thread_ = std::thread([this]() {
            while (running_) {
                export_metrics();
                std::this_thread::sleep_for(std::chrono::seconds(60));
            }
        });
    }

    void stop_export_thread() {
        running_ = false;
        if (export_thread_.joinable()) {
            export_thread_.join();
        }
    }

    std::string generate_filename() const {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << output_dir_ << "/metrics_" 
           << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S")
           << get_extension();
        return ss.str();
    }

    std::string get_extension() const {
        switch (format_) {
            case Format::JSON: return ".json";
            case Format::CSV: return ".csv";
            case Format::PROMETHEUS: return ".prom";
            default: return ".txt";
        }
    }

    void export_json(const std::string& filename, const RecoveryMetrics& metrics) {
        std::ofstream out(filename);
        if (!out) {
            LOG_ERROR("Failed to open metrics file: {}", filename);
            return;
        }

        out << "{\n";
        out << "  \"recovery_attempts\": " << metrics.recovery_attempts << ",\n";
        out << "  \"recovery_successes\": " << metrics.recovery_successes << ",\n";
        out << "  \"recovery_failures\": " << metrics.recovery_failures << ",\n";
        out << "  \"clients_recovered\": " << metrics.clients_recovered << ",\n";
        out << "  \"clients_expired\": " << metrics.clients_expired << ",\n";
        out << "  \"states_recovered\": " << metrics.states_recovered << ",\n";
        out << "  \"states_lost\": " << metrics.states_lost << ",\n";
        out << "  \"total_recovery_time_ms\": " << metrics.total_recovery_time_ms << ",\n";
        out << "  \"grace_period_expirations\": " << metrics.grace_period_expirations << ",\n";
        out << "  \"recovery_periods\": " << metrics.recovery_periods << ",\n";
        out << "  \"recovery_period_timeouts\": " << metrics.recovery_period_timeouts << "\n";
        out << "}\n";

        LOG_DEBUG("Exported metrics to JSON: {}", filename);
    }

    void export_csv(const std::string& filename, const RecoveryMetrics& metrics) {
        std::ofstream out(filename);
        if (!out) {
            LOG_ERROR("Failed to open metrics file: {}", filename);
            return;
        }

        // Write header
        out << "Metric,Value\n";
        out << "recovery_attempts," << metrics.recovery_attempts << "\n";
        out << "recovery_successes," << metrics.recovery_successes << "\n";
        out << "recovery_failures," << metrics.recovery_failures << "\n";
        out << "clients_recovered," << metrics.clients_recovered << "\n";
        out << "clients_expired," << metrics.clients_expired << "\n";
        out << "states_recovered," << metrics.states_recovered << "\n";
        out << "states_lost," << metrics.states_lost << "\n";
        out << "total_recovery_time_ms," << metrics.total_recovery_time_ms << "\n";
        out << "grace_period_expirations," << metrics.grace_period_expirations << "\n";
        out << "recovery_periods," << metrics.recovery_periods << "\n";
        out << "recovery_period_timeouts," << metrics.recovery_period_timeouts << "\n";

        LOG_DEBUG("Exported metrics to CSV: {}", filename);
    }

    void export_prometheus(const std::string& filename, const RecoveryMetrics& metrics) {
        std::ofstream out(filename);
        if (!out) {
            LOG_ERROR("Failed to open metrics file: {}", filename);
            return;
        }

        // Recovery attempts metrics
        out << "# HELP nfs_recovery_attempts Total number of recovery attempts\n";
        out << "# TYPE nfs_recovery_attempts counter\n";
        out << "nfs_recovery_attempts " << metrics.recovery_attempts << "\n";

        out << "# HELP nfs_recovery_successes Number of successful recoveries\n";
        out << "# TYPE nfs_recovery_successes counter\n";
        out << "nfs_recovery_successes " << metrics.recovery_successes << "\n";

        out << "# HELP nfs_recovery_failures Number of failed recoveries\n";
        out << "# TYPE nfs_recovery_failures counter\n";
        out << "nfs_recovery_failures " << metrics.recovery_failures << "\n";

        // Client metrics
        out << "# HELP nfs_clients_recovered Number of clients successfully recovered\n";
        out << "# TYPE nfs_clients_recovered counter\n";
        out << "nfs_clients_recovered " << metrics.clients_recovered << "\n";

        out << "# HELP nfs_clients_expired Number of clients that expired during recovery\n";
        out << "# TYPE nfs_clients_expired counter\n";
        out << "nfs_clients_expired " << metrics.clients_expired << "\n";

        // State metrics
        out << "# HELP nfs_states_recovered Number of states successfully recovered\n";
        out << "# TYPE nfs_states_recovered counter\n";
        out << "nfs_states_recovered " << metrics.states_recovered << "\n";

        out << "# HELP nfs_states_lost Number of states lost during recovery\n";
        out << "# TYPE nfs_states_lost counter\n";
        out << "nfs_states_lost " << metrics.states_lost << "\n";

        // Timing metrics
        out << "# HELP nfs_recovery_time_ms Total time spent in recovery\n";
        out << "# TYPE nfs_recovery_time_ms counter\n";
        out << "nfs_recovery_time_ms " << metrics.total_recovery_time_ms << "\n";

        // Grace period metrics
        out << "# HELP nfs_grace_period_expirations Number of grace period expirations\n";
        out << "# TYPE nfs_grace_period_expirations counter\n";
        out << "nfs_grace_period_expirations " << metrics.grace_period_expirations << "\n";

        // Recovery period metrics
        out << "# HELP nfs_recovery_periods Total number of recovery periods\n";
        out << "# TYPE nfs_recovery_periods counter\n";
        out << "nfs_recovery_periods " << metrics.recovery_periods << "\n";

        out << "# HELP nfs_recovery_period_timeouts Number of recovery period timeouts\n";
        out << "# TYPE nfs_recovery_period_timeouts counter\n";
        out << "nfs_recovery_period_timeouts " << metrics.recovery_period_timeouts << "\n";

        // Success rate gauge
        out << "# HELP nfs_recovery_success_rate Recovery success rate\n";
        out << "# TYPE nfs_recovery_success_rate gauge\n";
        out << "nfs_recovery_success_rate " << 
            (metrics.recovery_attempts > 0 ? 
                100.0 * metrics.recovery_successes / metrics.recovery_attempts : 0) << "\n";

        // Average recovery time gauge
        out << "# HELP nfs_avg_recovery_time_ms Average recovery time in milliseconds\n";
        out << "# TYPE nfs_avg_recovery_time_ms gauge\n";
        out << "nfs_avg_recovery_time_ms " << 
            (metrics.recovery_attempts > 0 ? 
                metrics.total_recovery_time_ms / metrics.recovery_attempts : 0) << "\n";

        LOG_DEBUG("Exported metrics to Prometheus format: {}", filename);
    }

    // Add helper method for writing aggregated metrics
    void write_aggregated_metrics(std::stringstream& ss, 
                                const MetricsAggregator::AggregatedMetrics& metrics) const {
        ss << "      \"success_rate\": " << metrics.success_rate << ",\n";
        ss << "      \"avg_recovery_time\": " << metrics.avg_recovery_time << ",\n";
        ss << "      \"total_recoveries\": " << metrics.total_recoveries << ",\n";
        ss << "      \"active_recoveries\": " << metrics.active_recoveries << ",\n";
        ss << "      \"states_recovered\": " << metrics.states_recovered << ",\n";
        ss << "      \"states_lost\": " << metrics.states_lost << "\n";
    }
};

} // namespace fuse_t 
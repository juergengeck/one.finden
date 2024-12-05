#pragma once
#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include "metrics_aggregator.hpp"
#include "logger.hpp"

namespace fused {

enum class AlertSeverity {
    INFO,
    WARNING,
    ERROR,
    CRITICAL
};

struct Alert {
    std::string id;
    std::string message;
    AlertSeverity severity;
    std::chrono::system_clock::time_point timestamp;
    std::unordered_map<std::string, double> metrics;
};

class AlertManager {
public:
    using AlertCallback = std::function<void(const Alert&)>;

    void add_alert_handler(AlertCallback handler) {
        handlers_.push_back(handler);
    }

    void check_metrics(const MetricsAggregator::AggregatedMetrics& metrics) {
        check_success_rate(metrics);
        check_recovery_time(metrics);
        check_resource_utilization(metrics);
        check_trends(metrics);
    }

private:
    std::vector<AlertCallback> handlers_;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> alert_cooldowns_;
    static constexpr auto COOLDOWN_PERIOD = std::chrono::seconds(300);

    void emit_alert(Alert alert) {
        auto now = std::chrono::system_clock::now();
        auto& last_alert = alert_cooldowns_[alert.id];

        if (now - last_alert >= COOLDOWN_PERIOD) {
            for (const auto& handler : handlers_) {
                handler(alert);
            }
            last_alert = now;
        }
    }

    void check_success_rate(const MetricsAggregator::AggregatedMetrics& metrics) {
        if (metrics.success_rate < 95.0) {
            emit_alert({
                "low_success_rate",
                "Recovery success rate below threshold",
                AlertSeverity::WARNING,
                std::chrono::system_clock::now(),
                {{"success_rate", metrics.success_rate}}
            });
        }
    }

    void check_recovery_time(const MetricsAggregator::AggregatedMetrics& metrics) {
        if (metrics.avg_recovery_time > 1000.0) {
            emit_alert({
                "high_recovery_time",
                "Average recovery time above threshold",
                AlertSeverity::WARNING,
                std::chrono::system_clock::now(),
                {{"avg_recovery_time", metrics.avg_recovery_time}}
            });
        }
    }

    void check_resource_utilization(const MetricsAggregator::AggregatedMetrics& metrics) {
        if (metrics.resource_utilization > 0.8) {
            emit_alert({
                "high_resource_utilization",
                "Resource utilization above threshold",
                AlertSeverity::WARNING,
                std::chrono::system_clock::now(),
                {{"resource_utilization", metrics.resource_utilization}}
            });
        }
    }

    void check_trends(const MetricsAggregator::AggregatedMetrics& metrics) {
        // Check success rate trend
        if (std::abs(metrics.success_rate_trend.slope) > 0.1) {
            emit_alert({
                "success_rate_trend",
                metrics.success_rate_trend.slope < 0 ? 
                    "Success rate declining" : "Success rate improving",
                metrics.success_rate_trend.slope < 0 ? 
                    AlertSeverity::WARNING : AlertSeverity::INFO,
                std::chrono::system_clock::now(),
                {
                    {"trend_slope", metrics.success_rate_trend.slope},
                    {"prediction", metrics.success_rate_trend.prediction},
                    {"confidence", metrics.success_rate_trend.r_squared}
                }
            });
        }

        // Check recovery time trend
        if (metrics.recovery_time_trend.slope > 0.1) {
            emit_alert({
                "recovery_time_trend",
                "Recovery time increasing",
                AlertSeverity::WARNING,
                std::chrono::system_clock::now(),
                {
                    {"trend_slope", metrics.recovery_time_trend.slope},
                    {"prediction", metrics.recovery_time_trend.prediction},
                    {"volatility", metrics.recovery_time_trend.volatility}
                }
            });
        }

        // Check for high volatility
        if (metrics.recovery_time_trend.volatility > 2.0) {
            emit_alert({
                "high_volatility",
                "High recovery time volatility detected",
                AlertSeverity::WARNING,
                std::chrono::system_clock::now(),
                {{"volatility", metrics.recovery_time_trend.volatility}}
            });
        }
    }
};

// Global alert manager instance
inline AlertManager& get_alert_manager() {
    static AlertManager manager;
    return manager;
}

} // namespace fuse_t 
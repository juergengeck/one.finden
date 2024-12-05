#pragma once
#include <chrono>
#include <string>
#include <vector>
#include "alert_manager.hpp"
#include "session_recovery.hpp"

namespace fused {

class RecoveryAlertManager {
public:
    explicit RecoveryAlertManager(AlertManager& alert_manager)
        : alert_manager_(alert_manager) {}

    void check_metrics(const RecoveryMetrics& metrics) {
        check_success_rate(metrics);
        check_recovery_time(metrics);
        check_expired_recoveries(metrics);
        check_operation_recovery(metrics);
    }

private:
    AlertManager& alert_manager_;

    // Alert thresholds
    static constexpr double MIN_SUCCESS_RATE = 95.0;  // 95%
    static constexpr double MAX_AVG_RECOVERY_TIME = 5000.0;  // 5 seconds
    static constexpr uint32_t MAX_EXPIRED_RATIO = 10;  // 10%
    static constexpr uint32_t MIN_OPERATION_RECOVERY = 90;  // 90%

    void check_success_rate(const RecoveryMetrics& metrics) {
        if (metrics.total_recoveries > 0) {
            double success_rate = 100.0 * metrics.successful_recoveries / metrics.total_recoveries;
            if (success_rate < MIN_SUCCESS_RATE) {
                alert_manager_.emit_alert({
                    "low_recovery_success_rate",
                    "Recovery success rate below threshold",
                    AlertSeverity::WARNING,
                    std::chrono::system_clock::now(),
                    {
                        {"success_rate", success_rate},
                        {"threshold", MIN_SUCCESS_RATE},
                        {"total_recoveries", static_cast<double>(metrics.total_recoveries)}
                    }
                });
            }
        }
    }

    void check_recovery_time(const RecoveryMetrics& metrics) {
        if (metrics.total_recoveries > 0) {
            double avg_time = static_cast<double>(metrics.total_recovery_time_ms) / 
                metrics.total_recoveries;
            if (avg_time > MAX_AVG_RECOVERY_TIME) {
                alert_manager_.emit_alert({
                    "high_recovery_time",
                    "Average recovery time above threshold",
                    AlertSeverity::WARNING,
                    std::chrono::system_clock::now(),
                    {
                        {"avg_time", avg_time},
                        {"threshold", MAX_AVG_RECOVERY_TIME},
                        {"total_recoveries", static_cast<double>(metrics.total_recoveries)}
                    }
                });
            }
        }
    }

    void check_expired_recoveries(const RecoveryMetrics& metrics) {
        if (metrics.total_recoveries > 0) {
            double expired_ratio = 100.0 * metrics.expired_recoveries / metrics.total_recoveries;
            if (expired_ratio > MAX_EXPIRED_RATIO) {
                alert_manager_.emit_alert({
                    "high_expired_recoveries",
                    "High rate of expired recoveries",
                    AlertSeverity::ERROR,
                    std::chrono::system_clock::now(),
                    {
                        {"expired_ratio", expired_ratio},
                        {"threshold", static_cast<double>(MAX_EXPIRED_RATIO)},
                        {"expired_count", static_cast<double>(metrics.expired_recoveries)}
                    }
                });
            }
        }
    }

    void check_operation_recovery(const RecoveryMetrics& metrics) {
        if (metrics.total_recoveries > 0) {
            uint64_t total_ops = metrics.operations_recovered + metrics.failed_recoveries;
            if (total_ops > 0) {
                double recovery_rate = 100.0 * metrics.operations_recovered / total_ops;
                if (recovery_rate < MIN_OPERATION_RECOVERY) {
                    alert_manager_.emit_alert({
                        "low_operation_recovery",
                        "Operation recovery rate below threshold",
                        AlertSeverity::WARNING,
                        std::chrono::system_clock::now(),
                        {
                            {"recovery_rate", recovery_rate},
                            {"threshold", static_cast<double>(MIN_OPERATION_RECOVERY)},
                            {"recovered_ops", static_cast<double>(metrics.operations_recovered)},
                            {"total_ops", static_cast<double>(total_ops)}
                        }
                    });
                }
            }
        }
    }
};

} // namespace fuse_t 
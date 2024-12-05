#pragma once
#include <chrono>
#include <deque>
#include <mutex>
#include "recovery_metrics.hpp"
#include "logger.hpp"

namespace fused {

struct MetricsSnapshot {
    std::chrono::system_clock::time_point timestamp;
    RecoveryMetrics metrics;
};

class MetricsAggregator {
public:
    // Window sizes for different aggregations
    static constexpr auto MINUTE_WINDOW = std::chrono::minutes(1);
    static constexpr auto HOUR_WINDOW = std::chrono::hours(1);
    static constexpr auto DAY_WINDOW = std::chrono::hours(24);

    void add_snapshot(const RecoveryMetrics& metrics) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::system_clock::now();
        MetricsSnapshot snapshot;
        snapshot.timestamp = now;
        snapshot.metrics = metrics;
        snapshots_.push_back(snapshot);
        cleanup_old_snapshots();
    }

    // Get aggregated metrics for different time windows
    struct AggregatedMetrics {
        double success_rate;
        double avg_recovery_time;
        uint64_t total_recoveries;
        uint64_t active_recoveries;
        uint64_t states_recovered;
        uint64_t states_lost;

        // Trend analysis
        struct Trend {
            double slope;           // Rate of change
            double r_squared;       // Correlation coefficient
            double prediction;      // Predicted next value
            double volatility;      // Standard deviation of changes
        };

        Trend success_rate_trend;
        Trend recovery_time_trend;
        Trend recovery_rate_trend;  // Recoveries per minute
        
        // Performance indicators
        double recovery_efficiency;    // states_recovered / (states_recovered + states_lost)
        double resource_utilization;   // active_recoveries / total_recoveries
        double mean_time_to_recovery;  // avg time between recovery start and completion
    };

    AggregatedMetrics get_minute_metrics() const {
        return get_aggregated_metrics(MINUTE_WINDOW);
    }

    AggregatedMetrics get_hour_metrics() const {
        return get_aggregated_metrics(HOUR_WINDOW);
    }

    AggregatedMetrics get_day_metrics() const {
        return get_aggregated_metrics(DAY_WINDOW);
    }

    // Get raw snapshots for custom analysis
    std::vector<MetricsSnapshot> get_snapshots(
        std::chrono::system_clock::duration window) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::system_clock::now();
        auto cutoff = now - window;

        std::vector<MetricsSnapshot> result;
        for (const auto& snapshot : snapshots_) {
            if (std::chrono::duration_cast<std::chrono::seconds>(
                snapshot.timestamp.time_since_epoch()).count() >= 
                std::chrono::duration_cast<std::chrono::seconds>(
                cutoff.time_since_epoch()).count()) {
                result.push_back(snapshot);
            }
        }
        return result;
    }

private:
    mutable std::mutex mutex_;
    std::deque<MetricsSnapshot> snapshots_;

    void cleanup_old_snapshots() {
        auto now = std::chrono::system_clock::now();
        auto cutoff = now - DAY_WINDOW;  // Keep up to 24 hours of data

        while (!snapshots_.empty() && snapshots_.front().timestamp < cutoff) {
            snapshots_.pop_front();
        }
    }

    Trend calculate_trend(const std::vector<double>& values, 
                         const std::vector<double>& timestamps) const {
        if (values.size() < 2) {
            return {0.0, 0.0, values.empty() ? 0.0 : values.back(), 0.0};
        }

        // Calculate means
        double mean_x = 0.0, mean_y = 0.0;
        for (size_t i = 0; i < values.size(); ++i) {
            mean_x += timestamps[i];
            mean_y += values[i];
        }
        mean_x /= values.size();
        mean_y /= values.size();

        // Calculate slope and correlation
        double numerator = 0.0, denominator = 0.0;
        double sum_squared_residuals = 0.0;
        double sum_squared_total = 0.0;

        for (size_t i = 0; i < values.size(); ++i) {
            double x_diff = timestamps[i] - mean_x;
            double y_diff = values[i] - mean_y;
            numerator += x_diff * y_diff;
            denominator += x_diff * x_diff;
            sum_squared_residuals += y_diff * y_diff;
        }

        double slope = numerator / denominator;
        double r_squared = (numerator * numerator) / (denominator * sum_squared_residuals);

        // Calculate prediction
        double next_time = timestamps.back() + 
            (timestamps.back() - timestamps.front()) / values.size();
        double prediction = slope * (next_time - mean_x) + mean_y;

        // Calculate volatility
        double volatility = 0.0;
        for (size_t i = 1; i < values.size(); ++i) {
            double change = values[i] - values[i-1];
            volatility += change * change;
        }
        volatility = std::sqrt(volatility / (values.size() - 1));

        return {slope, r_squared, prediction, volatility};
    }

    AggregatedMetrics get_aggregated_metrics(
        std::chrono::system_clock::duration window) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        auto cutoff = now - window;

        AggregatedMetrics result{};
        uint64_t total_attempts = 0;
        uint64_t total_successes = 0;
        uint64_t total_time = 0;

        // Collect time series data for trend analysis
        std::vector<double> timestamps;
        std::vector<double> success_rates;
        std::vector<double> recovery_times;
        std::vector<double> recovery_rates;

        for (const auto& snapshot : snapshots_) {
            if (std::chrono::duration_cast<std::chrono::seconds>(
                snapshot.timestamp.time_since_epoch()).count() >= 
                std::chrono::duration_cast<std::chrono::seconds>(
                cutoff.time_since_epoch()).count()) {
                double time_point = std::chrono::duration<double>(
                    snapshot.timestamp.time_since_epoch()).count();
                timestamps.push_back(time_point);

                const auto& m = snapshot.metrics;
                total_attempts += m.recovery_attempts;
                total_successes += m.recovery_successes;
                total_time += m.total_recovery_time_ms;
                result.states_recovered += m.states_recovered;
                result.states_lost += m.states_lost;
                result.active_recoveries = m.recovery_attempts - 
                    m.recovery_successes - m.recovery_failures;

                success_rates.push_back(
                    m.recovery_attempts > 0 ? 
                        100.0 * m.recovery_successes / m.recovery_attempts : 0.0);
                recovery_times.push_back(
                    m.recovery_attempts > 0 ? 
                        m.total_recovery_time_ms / m.recovery_attempts : 0.0);
                recovery_rates.push_back(
                    static_cast<double>(m.recovery_attempts) / 60.0);  // per minute
            }
        }

        if (total_attempts > 0) {
            result.success_rate = 100.0 * total_successes / total_attempts;
            result.avg_recovery_time = static_cast<double>(total_time) / total_attempts;
        }
        result.total_recoveries = total_attempts;

        // Calculate trends
        result.success_rate_trend = calculate_trend(success_rates, timestamps);
        result.recovery_time_trend = calculate_trend(recovery_times, timestamps);
        result.recovery_rate_trend = calculate_trend(recovery_rates, timestamps);

        // Calculate performance indicators
        result.recovery_efficiency = 
            result.states_recovered > 0 ? 
                static_cast<double>(result.states_recovered) / 
                    (result.states_recovered + result.states_lost) : 0.0;
        
        result.resource_utilization = 
            result.total_recoveries > 0 ?
                static_cast<double>(result.active_recoveries) / 
                    result.total_recoveries : 0.0;

        result.mean_time_to_recovery = 
            result.total_recoveries > 0 ?
                result.avg_recovery_time : 0.0;

        return result;
    }
};

// Global metrics aggregator instance
inline MetricsAggregator& get_metrics_aggregator() {
    static MetricsAggregator aggregator;
    return aggregator;
}

} // namespace fused 
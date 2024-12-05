#pragma once
#include <array>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cmath>

namespace fused {

class Histogram {
public:
    static constexpr size_t NUM_BUCKETS = 32;
    static constexpr double MIN_VALUE = 1.0;    // 1 microsecond
    static constexpr double MAX_VALUE = 1e7;    // 10 seconds

    void record(double value) {
        if (value < MIN_VALUE) value = MIN_VALUE;
        if (value > MAX_VALUE) value = MAX_VALUE;

        size_t bucket = get_bucket(value);
        buckets_[bucket]++;
        count_++;

        // Update min/max
        double current_min = min_.load();
        while (value < current_min) {
            min_.compare_exchange_weak(current_min, value);
        }

        double current_max = max_.load();
        while (value > current_max) {
            max_.compare_exchange_weak(current_max, value);
        }

        // Update sum for mean calculation
        sum_ += value;
    }

    struct Stats {
        double min;
        double max;
        double mean;
        double p50;  // median
        double p90;
        double p95;
        double p99;
        uint64_t count;
    };

    Stats get_stats() const {
        Stats stats;
        stats.min = min_.load();
        stats.max = max_.load();
        stats.count = count_.load();
        stats.mean = sum_.load() / stats.count;

        // Calculate percentiles
        std::vector<uint64_t> cumulative(NUM_BUCKETS);
        uint64_t sum = 0;
        for (size_t i = 0; i < NUM_BUCKETS; i++) {
            sum += buckets_[i].load();
            cumulative[i] = sum;
        }

        stats.p50 = get_percentile_value(cumulative, 0.50);
        stats.p90 = get_percentile_value(cumulative, 0.90);
        stats.p95 = get_percentile_value(cumulative, 0.95);
        stats.p99 = get_percentile_value(cumulative, 0.99);

        return stats;
    }

private:
    std::array<std::atomic<uint64_t>, NUM_BUCKETS> buckets_{};
    std::atomic<uint64_t> count_{0};
    std::atomic<double> min_{MAX_VALUE};
    std::atomic<double> max_{MIN_VALUE};
    std::atomic<double> sum_{0.0};

    size_t get_bucket(double value) const {
        double log_range = std::log(MAX_VALUE / MIN_VALUE);
        double log_value = std::log(value / MIN_VALUE);
        size_t bucket = static_cast<size_t>(NUM_BUCKETS * log_value / log_range);
        return std::min(bucket, NUM_BUCKETS - 1);
    }

    double get_bucket_lower_bound(size_t bucket) const {
        double log_range = std::log(MAX_VALUE / MIN_VALUE);
        double bucket_log = log_range * bucket / NUM_BUCKETS;
        return MIN_VALUE * std::exp(bucket_log);
    }

    double get_percentile_value(const std::vector<uint64_t>& cumulative,
                              double percentile) const {
        uint64_t target = static_cast<uint64_t>(percentile * count_.load());
        auto it = std::lower_bound(cumulative.begin(), cumulative.end(), target);
        size_t bucket = std::distance(cumulative.begin(), it);
        return get_bucket_lower_bound(bucket);
    }
};

} // namespace fuse_t 
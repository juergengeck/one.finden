#pragma once
#include <atomic>
#include <chrono>

namespace fused {

struct RecoveryMetrics {
    // Recovery counts
    std::atomic<uint64_t> recovery_attempts{0};
    std::atomic<uint64_t> recovery_successes{0};
    std::atomic<uint64_t> recovery_failures{0};
    
    // Operation counts
    std::atomic<uint64_t> operations_recovered{0};
    std::atomic<uint64_t> operations_failed{0};
    
    // Timing
    std::atomic<uint64_t> total_recovery_time_ms{0};
    
    // State tracking
    std::atomic<uint64_t> states_recovered{0};
    std::atomic<uint64_t> states_lost{0};
};

} // namespace fused 
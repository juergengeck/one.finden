#pragma once
#include <string>
#include <chrono>
#include "state.hpp"

namespace fused {

class StateRecoveryManager {
public:
    StateRecoveryManager();
    ~StateRecoveryManager();

    bool initialize();
    bool start_recovery(const std::string& client_id, uint32_t session_id);
    bool complete_recovery(const std::string& client_id);
    bool abort_recovery(const std::string& client_id);

    // Recovery state tracking
    bool is_recovering(const std::string& client_id) const;
    bool was_operation_recovered(uint32_t session_id, uint32_t operation_id) const;

private:
    std::mutex mutex_;
    std::unordered_map<std::string, RecoveryState> recovery_states_;
    static constexpr auto RECOVERY_TIMEOUT = std::chrono::minutes(5);
};

} // namespace fused 
#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include "nfs_protocol.hpp"
#include "operation_journal.hpp"
#include "util/logger.hpp"

namespace fused {

enum class StateValidationType {
    INVARIANT,    // Check system invariants
    TRANSITION,   // Validate state transitions
    CONSISTENCY,  // Check data consistency
    INTEGRITY     // Verify data integrity
};

struct StateValidationRule {
    StateValidationType type;
    std::string path;
    std::function<bool(const std::string&)> check;
    std::string error_message;
    bool critical{false};  // If true, validation failure triggers recovery
};

class StateValidator {
public:
    StateValidator(OperationJournal& journal);
    ~StateValidator();

    // Initialize validator
    bool initialize();

    // Validation rules
    void add_validation_rule(const StateValidationRule& rule);
    bool remove_validation_rule(const std::string& path);
    bool update_validation_rule(const std::string& path, const StateValidationRule& rule);

    // State validation
    bool validate_state(const std::string& path);
    bool validate_transition(const std::string& path, uint64_t operation_id);
    bool validate_consistency(const std::string& path);

    // Error detection
    bool detect_errors(const std::string& path);
    bool handle_error(const std::string& path, const std::string& error);
    bool trigger_recovery(const std::string& path);

private:
    OperationJournal& journal_;
    std::mutex mutex_;
    std::thread validation_thread_;
    std::atomic<bool> running_{true};

    static constexpr auto VALIDATION_INTERVAL = std::chrono::seconds(1);
    static constexpr auto ERROR_THRESHOLD = 3;

    // Validation tracking
    struct ValidationState {
        std::string path;
        std::chrono::steady_clock::time_point last_check;
        uint32_t error_count{0};
        bool needs_recovery{false};
        std::vector<StateValidationRule> rules;
    };
    std::unordered_map<std::string, ValidationState> validation_states_;

    // State transition tracking
    struct TransitionState {
        uint64_t operation_id;
        std::string path;
        std::chrono::steady_clock::time_point timestamp;
        bool validated{false};
        bool successful{false};
    };
    std::unordered_map<uint64_t, TransitionState> transitions_;

    // Validation thread
    void run_validation_loop();
    void process_validation_states();
    void cleanup_old_states();

    // Validation helpers
    bool check_invariants(const std::string& path);
    bool verify_state_transition(const TransitionState& transition);
    bool check_data_consistency(const std::string& path);
    bool verify_data_integrity(const std::string& path);

    // Error handling
    void log_validation_error(const std::string& path, const std::string& error);
    bool attempt_error_recovery(const std::string& path);
    bool verify_recovery_success(const std::string& path);
};

} // namespace fuse_t 
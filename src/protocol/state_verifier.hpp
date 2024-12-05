#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include "nfs_protocol.hpp"
#include "util/logger.hpp"

namespace fused {

struct StateInvariant {
    std::string name;
    std::string path;
    std::function<bool(const std::string&)> check;
    std::string error_message;
};

struct StateValidationResult {
    bool valid{true};
    std::vector<std::string> violations;
    std::chrono::steady_clock::time_point timestamp;
    uint64_t objects_checked{0};
    uint64_t invariants_checked{0};
};

class StateVerifier {
public:
    StateVerifier();
    ~StateVerifier();

    // Initialize verifier
    bool initialize();

    // Add invariants
    void add_file_invariant(const StateInvariant& invariant);
    void add_directory_invariant(const StateInvariant& invariant);
    void add_handle_invariant(const StateInvariant& invariant);

    // Verification methods
    StateValidationResult verify_state() const;
    StateValidationResult verify_path(const std::string& path) const;
    StateValidationResult verify_handle(const NFSFileHandle& handle) const;

    // Corruption detection
    bool detect_corruption(const std::string& path);
    bool repair_corruption(const std::string& path);

    // Recovery triggers
    void set_recovery_trigger(const std::string& path, 
                            std::function<void()> trigger);
    void trigger_recovery(const std::string& path);

private:
    std::mutex mutex_;
    std::vector<StateInvariant> file_invariants_;
    std::vector<StateInvariant> directory_invariants_;
    std::vector<StateInvariant> handle_invariants_;
    std::unordered_map<std::string, std::function<void()>> recovery_triggers_;

    // Verification helpers
    bool check_file_invariants(const std::string& path, 
                             StateValidationResult& result) const;
    bool check_directory_invariants(const std::string& path,
                                  StateValidationResult& result) const;
    bool check_handle_invariants(const NFSFileHandle& handle,
                               StateValidationResult& result) const;

    // Corruption detection helpers
    bool verify_file_integrity(const std::string& path);
    bool verify_directory_integrity(const std::string& path);
    bool verify_handle_integrity(const NFSFileHandle& handle);

    // Recovery helpers
    bool attempt_repair(const std::string& path);
    void log_corruption(const std::string& path, const std::string& details);
};

} // namespace fuse_t 
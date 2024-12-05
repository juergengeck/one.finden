#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include "nfs_protocol.hpp"
#include "transaction_log.hpp"
#include "util/logger.hpp"

namespace fused {

struct JournalEntry {
    uint64_t sequence_id;
    uint64_t transaction_id;  // Link to transaction log
    NFSProcedure procedure;
    std::vector<uint8_t> arguments;
    std::vector<uint32_t> dependencies;  // Sequence IDs of dependent operations
    std::string target_path;
    std::chrono::steady_clock::time_point timestamp;
    bool completed{false};
    NFSStatus result{NFSStatus::OK};
};

class OperationJournal {
public:
    explicit OperationJournal(TransactionLog& txn_log);
    ~OperationJournal();

    // Initialize journal
    bool initialize(const std::string& journal_path);

    // Journal operations
    uint64_t append_operation(NFSProcedure proc, 
                            const std::vector<uint8_t>& args,
                            const std::string& path);
    
    bool complete_operation(uint64_t sequence_id, NFSStatus result);
    
    // Dependency tracking
    void add_dependency(uint64_t sequence_id, uint64_t depends_on);
    bool check_dependencies(uint64_t sequence_id) const;
    std::vector<uint64_t> get_dependencies(uint64_t sequence_id) const;

    // State transitions
    bool begin_state_transition(uint64_t sequence_id);
    bool commit_state_transition(uint64_t sequence_id);
    bool rollback_state_transition(uint64_t sequence_id);

    // Recovery
    bool recover_journal();
    std::vector<JournalEntry> get_incomplete_operations() const;

private:
    TransactionLog& txn_log_;
    std::string journal_path_;
    int journal_fd_{-1};
    std::mutex mutex_;
    std::atomic<uint64_t> next_sequence_id_{1};

    // In-memory operation tracking
    std::unordered_map<uint64_t, JournalEntry> active_operations_;
    
    // Helper methods
    bool write_entry(const JournalEntry& entry);
    bool read_entry(JournalEntry& entry);
    bool truncate_journal();
    
    // State transition helpers
    bool save_pre_state(uint64_t sequence_id);
    bool verify_state_transition(uint64_t sequence_id);
    
    // Recovery helpers
    bool replay_operation(const JournalEntry& entry);
    bool verify_operation(const JournalEntry& entry);
    bool check_operation_order(const JournalEntry& op1, const JournalEntry& op2) const;
};

} // namespace fuse_t 
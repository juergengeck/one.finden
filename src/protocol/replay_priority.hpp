#pragma once
#include <cstdint>
#include "nfs_protocol.hpp"

namespace fused {

// Priority levels for replay operations
enum class ReplayPriority : uint32_t {
    CRITICAL = 0,   // Must be replayed immediately (e.g., metadata operations)
    HIGH = 1,       // Should be replayed soon (e.g., write operations)
    NORMAL = 2,     // Standard priority (e.g., read operations)
    LOW = 3,        // Can be delayed (e.g., attribute updates)
    BACKGROUND = 4  // Process when system is idle
};

class ReplayPriorityCalculator {
public:
    // Calculate priority for an operation
    static ReplayPriority calculate_priority(const LoggedOperation& op) {
        switch (op.procedure) {
            // Metadata operations
            case NFSProcedure::CREATE:
            case NFSProcedure::REMOVE:
            case NFSProcedure::RENAME:
                return ReplayPriority::CRITICAL;

            // Write operations
            case NFSProcedure::WRITE:
                return ReplayPriority::HIGH;

            // Read operations
            case NFSProcedure::READ:
            case NFSProcedure::READDIR:
                return ReplayPriority::NORMAL;

            // Attribute operations
            case NFSProcedure::SETATTR:
            case NFSProcedure::GETATTR:
                return ReplayPriority::LOW;

            // Everything else
            default:
                return ReplayPriority::BACKGROUND;
        }
    }

    // Calculate batch priority based on contained operations
    static uint32_t calculate_batch_priority(const ReplayBatch& batch) {
        if (batch.urgent) {
            return 0;  // Highest priority for urgent batches
        }

        // Find highest priority operation in batch
        ReplayPriority highest = ReplayPriority::BACKGROUND;
        for (const auto& op : batch.operations) {
            auto priority = calculate_priority(op);
            if (static_cast<uint32_t>(priority) < static_cast<uint32_t>(highest)) {
                highest = priority;
            }
        }

        return static_cast<uint32_t>(highest);
    }
};

} // namespace fuse_t 
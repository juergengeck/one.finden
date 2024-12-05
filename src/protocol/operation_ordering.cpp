#include "operation_ordering.hpp"
#include <algorithm>

namespace fused {

OperationOrderingManager::OperationOrderingManager(OperationJournal& journal)
    : journal_(journal) {
    // Start ordering thread
    ordering_thread_ = std::thread([this]() {
        run_ordering_loop();
    });
}

OperationOrderingManager::~OperationOrderingManager() {
    running_ = false;
    if (ordering_thread_.joinable()) {
        ordering_thread_.join();
    }
}

bool OperationOrderingManager::initialize() {
    LOG_INFO("Initializing operation ordering manager");
    return true;
}

bool OperationOrderingManager::order_operation(uint64_t op_id,
                                             const OrderingRequirement& requirement) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Create ordered operation
    OrderedOperation op{
        op_id,
        requirement.constraint,
        requirement.dependencies,
        requirement.conflict_domain,
        std::chrono::steady_clock::now(),
        false,  // not verified
        false   // not completed
    };

    // Check for conflicts
    if (!check_conflicts(op_id, requirement.conflict_domain)) {
        LOG_ERROR("Operation {} has conflicts in domain {}", 
            op_id, requirement.conflict_domain);
        return false;
    }

    // Add to tracking
    ordered_operations_[op_id] = std::move(op);

    // Add to conflict domain if specified
    if (!requirement.conflict_domain.empty()) {
        auto& domain = conflict_domains_[requirement.conflict_domain];
        domain.active_operations.push_back(op_id);
    }

    LOG_DEBUG("Ordered operation {} with constraint {}", 
        op_id, static_cast<int>(requirement.constraint));
    return true;
}

bool OperationOrderingManager::verify_order(uint64_t op_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = ordered_operations_.find(op_id);
    if (it == ordered_operations_.end()) {
        return false;
    }

    // Verify based on constraint type
    switch (it->second.constraint) {
        case OrderingConstraint::STRICT:
            if (!verify_operation_order(it->second)) {
                return false;
            }
            break;

        case OrderingConstraint::CAUSAL:
            if (!check_causal_dependencies(it->second)) {
                return false;
            }
            break;

        case OrderingConstraint::CONCURRENT:
            if (!detect_path_conflicts(it->second)) {
                return false;
            }
            break;

        case OrderingConstraint::RELAXED:
            // No verification needed
            break;
    }

    it->second.verified = true;
    return true;
}

bool OperationOrderingManager::wait_for_dependencies(uint64_t op_id) {
    auto deadline = std::chrono::steady_clock::now() + MAX_WAIT_TIME;
    
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (verify_dependencies(op_id)) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return false;
}

bool OperationOrderingManager::check_conflicts(uint64_t op_id, 
                                            const std::string& path) {
    // Check conflicts in all relevant domains
    for (const auto& [domain_name, domain] : conflict_domains_) {
        // Skip if path not in domain
        if (std::find(domain.paths.begin(), domain.paths.end(), path) == 
            domain.paths.end()) {
            continue;
        }

        // Check for conflicts with active operations
        for (uint64_t active_op : domain.active_operations) {
            if (active_op != op_id) {
                auto it = ordered_operations_.find(active_op);
                if (it != ordered_operations_.end() && !it->second.completed) {
                    if (detect_path_conflicts(it->second)) {
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

bool OperationOrderingManager::register_conflict_domain(const std::string& domain) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (conflict_domains_.find(domain) != conflict_domains_.end()) {
        return false;
    }

    ConflictDomain new_domain{
        domain,
        {},  // no paths yet
        {}   // no active operations
    };

    conflict_domains_[domain] = std::move(new_domain);
    return true;
}

bool OperationOrderingManager::add_to_conflict_domain(const std::string& domain,
                                                    const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = conflict_domains_.find(domain);
    if (it == conflict_domains_.end()) {
        return false;
    }

    it->second.paths.push_back(path);
    return true;
}

void OperationOrderingManager::add_dependency(uint64_t op_id, uint64_t depends_on) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = ordered_operations_.find(op_id);
    if (it != ordered_operations_.end()) {
        it->second.dependencies.push_back(depends_on);
    }
}

bool OperationOrderingManager::verify_dependencies(uint64_t op_id) {
    auto it = ordered_operations_.find(op_id);
    if (it == ordered_operations_.end()) {
        return false;
    }

    // Check all dependencies are completed
    for (uint64_t dep_id : it->second.dependencies) {
        auto dep_it = ordered_operations_.find(dep_id);
        if (dep_it == ordered_operations_.end() || !dep_it->second.completed) {
            return false;
        }
    }

    return true;
}

std::vector<uint64_t> OperationOrderingManager::get_dependencies(uint64_t op_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = ordered_operations_.find(op_id);
    if (it != ordered_operations_.end()) {
        return it->second.dependencies;
    }
    return {};
}

uint64_t OperationOrderingManager::create_serialization_point() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    uint64_t point_id = next_point_id_++;
    
    SerializationPoint point{
        point_id,
        std::chrono::steady_clock::now(),
        {},  // no operations yet
        false
    };

    // Add all active operations
    for (const auto& [op_id, op] : ordered_operations_) {
        if (!op.completed) {
            point.operations.push_back(op_id);
        }
    }

    serialization_points_[point_id] = std::move(point);
    return point_id;
}

bool OperationOrderingManager::wait_for_serialization(uint64_t point_id) {
    auto deadline = std::chrono::steady_clock::now() + MAX_WAIT_TIME;
    
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = serialization_points_.find(point_id);
            if (it != serialization_points_.end() && it->second.verified) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return false;
}

bool OperationOrderingManager::verify_serialization_point(uint64_t point_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = serialization_points_.find(point_id);
    if (it == serialization_points_.end()) {
        return false;
    }

    // Verify all operations at this point are completed and ordered correctly
    for (uint64_t op_id : it->second.operations) {
        auto op_it = ordered_operations_.find(op_id);
        if (op_it == ordered_operations_.end() || !op_it->second.completed) {
            return false;
        }

        if (!enforce_serialization_order(op_it->second)) {
            return false;
        }
    }

    it->second.verified = true;
    return true;
}

void OperationOrderingManager::run_ordering_loop() {
    while (running_) {
        process_ordered_operations();
        cleanup_completed_operations();
        std::this_thread::sleep_for(ORDERING_INTERVAL);
    }
}

void OperationOrderingManager::process_ordered_operations() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& [op_id, op] : ordered_operations_) {
        if (!op.completed && !op.verified) {
            verify_order(op_id);
        }
    }
}

void OperationOrderingManager::cleanup_completed_operations() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    
    // Cleanup old completed operations
    for (auto it = ordered_operations_.begin(); it != ordered_operations_.end();) {
        if (it->second.completed && 
            now - it->second.timestamp > std::chrono::minutes(5)) {
            // Remove from conflict domains
            if (!it->second.conflict_domain.empty()) {
                auto domain_it = conflict_domains_.find(it->second.conflict_domain);
                if (domain_it != conflict_domains_.end()) {
                    auto& ops = domain_it->second.active_operations;
                    ops.erase(std::remove(ops.begin(), ops.end(), it->first), 
                            ops.end());
                }
            }
            it = ordered_operations_.erase(it);
        } else {
            ++it;
        }
    }

    // Cleanup old serialization points
    for (auto it = serialization_points_.begin(); it != serialization_points_.end();) {
        if (now - it->second.timestamp > std::chrono::minutes(5)) {
            it = serialization_points_.erase(it);
        } else {
            ++it;
        }
    }
}

bool OperationOrderingManager::verify_operation_order(const OrderedOperation& op) {
    // For strict ordering, verify all dependencies are completed in order
    for (uint64_t dep_id : op.dependencies) {
        auto dep_it = ordered_operations_.find(dep_id);
        if (dep_it == ordered_operations_.end() || !dep_it->second.completed) {
            return false;
        }
        if (dep_it->second.timestamp >= op.timestamp) {
            return false;
        }
    }
    return true;
}

bool OperationOrderingManager::check_causal_dependencies(const OrderedOperation& op) {
    // For causal ordering, verify dependencies are completed but allow concurrent execution
    for (uint64_t dep_id : op.dependencies) {
        auto dep_it = ordered_operations_.find(dep_id);
        if (dep_it == ordered_operations_.end() || !dep_it->second.completed) {
            return false;
        }
    }
    return true;
}

bool OperationOrderingManager::detect_path_conflicts(const OrderedOperation& op) {
    // Check for conflicts in the same conflict domain
    if (!op.conflict_domain.empty()) {
        auto domain_it = conflict_domains_.find(op.conflict_domain);
        if (domain_it != conflict_domains_.end()) {
            for (uint64_t other_id : domain_it->second.active_operations) {
                if (other_id != op.operation_id) {
                    auto other_it = ordered_operations_.find(other_id);
                    if (other_it != ordered_operations_.end() && 
                        !other_it->second.completed) {
                        // Check for path overlap or other conflicts
                        // TODO: Implement more sophisticated conflict detection
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

bool OperationOrderingManager::enforce_serialization_order(const OrderedOperation& op) {
    // Verify operation respects serialization order
    if (op.constraint == OrderingConstraint::STRICT) {
        return verify_operation_order(op);
    }
    return true;
}

void OperationOrderingManager::handle_ordering_violation(uint64_t op_id,
                                                       const std::string& reason) {
    LOG_ERROR("Ordering violation for operation {}: {}", op_id, reason);
    // TODO: Implement violation handling
}

} // namespace fuse_t 
#pragma once
#include <string>
#include <memory>
#include "util/logger.hpp"

namespace fused {

class ServiceRegistration {
public:
    ServiceRegistration();
    ~ServiceRegistration();

    // Register NFS service with macOS
    bool register_service(uint16_t port = 2049);

    // Unregister service
    bool unregister_service();

    // Check if service is registered
    bool is_registered() const;

    // Get service status
    std::string get_status() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;

    // Register with various macOS subsystems
    bool register_with_launchd();
    bool register_with_nfs_registry();
    bool register_with_bonjour();
    bool setup_security_policies();
};

} // namespace fused 
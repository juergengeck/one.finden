#pragma once
#include <string>
#include <memory>
#include "nfs_server.hpp"
#include "service_monitor.hpp"

namespace fused {

class NFSService {
public:
    NFSService();
    ~NFSService();

    // Initialize NFS service
    bool initialize(const std::string& export_path);

    // Start NFS service
    bool start();

    // Stop NFS service
    void stop();

    // Export management
    bool add_export(const std::string& path, const std::string& options);
    bool remove_export(const std::string& path);

    // Client management
    bool allow_client(const std::string& client_addr);
    bool deny_client(const std::string& client_addr);

private:
    std::unique_ptr<NFSServer> server_;
    std::unique_ptr<ServiceMonitor> monitor_;
    bool running_{false};
    int server_socket_{-1};
    DNSServiceRef service_ref_{nullptr};

    // NFS protocol handlers
    bool handle_mount_request(const std::string& path);
    bool handle_unmount_request(const std::string& path);
    bool handle_nfs_request();

    // Service management
    bool register_service();
    bool unregister_service();
    bool setup_networking();
};

} // namespace fused 
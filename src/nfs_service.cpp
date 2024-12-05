#include "nfs_service.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include "util/logger.hpp"
#include <launch.h>
#include <sys/un.h>

namespace fused {

NFSService::NFSService() {
    server_ = std::make_unique<NFSServer>();
}

NFSService::~NFSService() {
    stop();
}

bool NFSService::initialize(const std::string& export_path) {
    if (!server_->initialize()) {
        LOG_ERROR("Failed to initialize NFS server");
        return false;
    }

    // Register with macOS service framework
    if (!register_service()) {
        LOG_ERROR("Failed to register NFS service");
        return false;
    }

    // Setup networking
    if (!setup_networking()) {
        LOG_ERROR("Failed to setup networking");
        return false;
    }

    // Add initial export
    return add_export(export_path, "rw,async,no_root_squash");
}

bool NFSService::start() {
    if (running_) {
        return true;
    }

    // Start NFS server
    if (!server_->start()) {
        LOG_ERROR("Failed to start NFS server");
        return false;
    }

    // Start service monitor
    monitor_ = std::make_unique<ServiceMonitor>();
    if (!monitor_->start()) {
        LOG_ERROR("Failed to start service monitor");
        server_->stop();
        return false;
    }

    running_ = true;
    LOG_INFO("NFS service started");
    return true;
}

void NFSService::stop() {
    if (!running_) {
        return;
    }

    server_->stop();
    unregister_service();
    running_ = false;
    LOG_INFO("NFS service stopped");
}

bool NFSService::register_service() {
    // Get the launchd socket
    int *fds = nullptr;
    size_t count = 0;
    int result = launch_activate_socket("Listeners", &fds, &count);
    
    if (result != 0) {
        LOG_ERROR("Failed to activate launchd socket: {}", strerror(result));
        return false;
    }

    if (count != 1) {
        LOG_ERROR("Unexpected socket count from launchd: {}", count);
        free(fds);
        return false;
    }

    // Store the socket for later use
    server_socket_ = fds[0];
    free(fds);

    // Register with mDNSResponder for service discovery
    DNSServiceRef service;
    result = DNSServiceRegister(&service,
                              0,                    // Default flags
                              0,                    // All interfaces
                              "Fused NFS",          // Service name
                              "_nfs._tcp",          // Service type
                              "",                   // Domain (default)
                              nullptr,              // Host (default)
                              htons(2049),          // Port
                              0,                    // TXT record length
                              nullptr,              // TXT record data
                              nullptr,              // Callback
                              nullptr);             // Context

    if (result != kDNSServiceErr_NoError) {
        LOG_ERROR("Failed to register mDNS service: {}", result);
        return false;
    }

    service_ref_ = service;
    LOG_INFO("Service registered successfully");
    return true;
}

bool NFSService::unregister_service() {
    // Unregister from mDNSResponder
    if (service_ref_) {
        DNSServiceRefDeallocate(service_ref_);
        service_ref_ = nullptr;
    }

    // Close server socket
    if (server_socket_ >= 0) {
        close(server_socket_);
        server_socket_ = -1;
    }

    LOG_INFO("Service unregistered");
    return true;
}

bool NFSService::setup_networking() {
    // Setup NFS protocol endpoints
    return true;
}

} // namespace fused 
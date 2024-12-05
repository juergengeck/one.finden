#pragma once
#include <memory>
#include <string>
#include "nfs_server.hpp"
#include "mount/mount_manager.hpp"

namespace fused {

class FuseServer {
public:
    FuseServer();
    ~FuseServer();

    // Initialize the server with configuration
    bool initialize(const std::string& mount_point);
    
    // Start serving requests
    bool start();
    
    // Stop the server
    void stop();

private:
    std::unique_ptr<NFSServer> nfs_server_;
    std::unique_ptr<MountManager> mount_manager_;
    bool is_running_;
};

} // namespace fused 
#include "fuse_server.hpp"
#include <iostream>

namespace fused {

FuseServer::FuseServer() : is_running_(false) {
    nfs_server_ = std::make_unique<NFSServer>();
    mount_manager_ = std::make_unique<MountManager>();
}

FuseServer::~FuseServer() {
    stop();
}

bool FuseServer::initialize(const std::string& mount_point) {
    if (!mount_manager_->validate_mount_point(mount_point)) {
        std::cerr << "Invalid mount point: " << mount_point << std::endl;
        return false;
    }

    if (!nfs_server_->initialize()) {
        std::cerr << "Failed to initialize NFS server" << std::endl;
        return false;
    }

    return true;
}

bool FuseServer::start() {
    if (is_running_) {
        return true;
    }

    if (!nfs_server_->start()) {
        return false;
    }

    is_running_ = true;
    return true;
}

void FuseServer::stop() {
    if (!is_running_) {
        return;
    }

    nfs_server_->stop();
    is_running_ = false;
}

} // namespace fuse_t 
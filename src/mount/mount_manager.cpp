#include "mount_manager.hpp"
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <iostream>
#include <cstring>

namespace fused {

MountManager::MountManager() : is_mounted_(false) {}

MountManager::~MountManager() {
    if (is_mounted_) {
        unmount(current_mount_point_);
    }
}

bool MountManager::validate_mount_point(const std::string& path) {
    if (path.empty()) {
        std::cerr << "Mount point path is empty" << std::endl;
        return false;
    }

    if (!check_directory(path)) {
        std::cerr << "Mount point is not a valid directory: " << path << std::endl;
        return false;
    }

    return true;
}

bool MountManager::mount(const std::string& path) {
    if (is_mounted_) {
        std::cerr << "Already mounted at: " << current_mount_point_ << std::endl;
        return false;
    }

    if (!validate_mount_point(path)) {
        return false;
    }

    // Prepare mount options
    std::string options = create_mount_options();

    // Mount using NFS
    if (::mount("127.0.0.1:/", path.c_str(), "nfs", 0, options.c_str()) != 0) {
        std::cerr << "Failed to mount: " << strerror(errno) << std::endl;
        return false;
    }

    current_mount_point_ = path;
    is_mounted_ = true;
    return true;
}

bool MountManager::unmount(const std::string& path) {
    if (!is_mounted_) {
        return true;
    }

    if (path != current_mount_point_) {
        std::cerr << "Mount point mismatch" << std::endl;
        return false;
    }

    // Unmount the filesystem
    if (::unmount(path.c_str(), MNT_FORCE) != 0) {
        std::cerr << "Failed to unmount: " << strerror(errno) << std::endl;
        return false;
    }

    is_mounted_ = false;
    current_mount_point_.clear();
    return true;
}

bool MountManager::check_directory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

std::string MountManager::create_mount_options() {
    // Configure NFS mount options
    return "vers=4,port=2049,tcp,local,async";
}

} // namespace fuse_t 
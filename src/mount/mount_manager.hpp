#pragma once
#include <string>
#include <sys/mount.h>
#include <sys/param.h>

namespace fused {

class MountManager {
public:
    MountManager();
    ~MountManager();

    // Validate if the mount point is valid
    bool validate_mount_point(const std::string& path);

    // Mount the filesystem
    bool mount(const std::string& path);

    // Unmount the filesystem
    bool unmount(const std::string& path);

private:
    // Check if path exists and is a directory
    bool check_directory(const std::string& path);

    // Create mount options string
    std::string create_mount_options();

    std::string current_mount_point_;
    bool is_mounted_;
};

} // namespace fuse_t 
#pragma once
#include <string>
#include <memory>
#include <functional>
#include "nfs_server.hpp"

namespace fused {

// FUSE operation callbacks
struct FuseOperations {
    std::function<int(const char*, struct stat*)> getattr;
    std::function<int(const char*, char*, size_t, off_t)> read;
    std::function<int(const char*, const char*, size_t, off_t)> write;
    std::function<int(const char*, mode_t)> mkdir;
    std::function<int(const char*)> rmdir;
    std::function<int(const char*, mode_t)> create;
    std::function<int(const char*)> unlink;
    std::function<int(const char*, struct fuse_file_info*)> open;
    std::function<int(const char*, struct fuse_file_info*)> release;
    std::function<int(const char*, void*, fuse_fill_dir_t, off_t, struct fuse_file_info*)> readdir;
};

class FuseImpl {
public:
    explicit FuseImpl(NFSServer& server);
    ~FuseImpl();

    // Initialize FUSE implementation
    bool initialize(const std::string& mount_point);

    // Start FUSE operations
    bool start();

    // Stop FUSE operations
    void stop();

    // Get FUSE operations
    const FuseOperations& get_operations() const { return operations_; }

private:
    NFSServer& server_;
    std::string mount_point_;
    FuseOperations operations_;
    struct fuse* fuse_{nullptr};
    struct fuse_chan* channel_{nullptr};
    bool running_{false};

    // Initialize FUSE operations
    void init_operations();

    // FUSE operation implementations
    static int impl_getattr(const char* path, struct stat* stbuf);
    static int impl_read(const char* path, char* buf, size_t size, off_t offset);
    static int impl_write(const char* path, const char* buf, size_t size, off_t offset);
    static int impl_mkdir(const char* path, mode_t mode);
    static int impl_rmdir(const char* path);
    static int impl_create(const char* path, mode_t mode);
    static int impl_unlink(const char* path);
    static int impl_open(const char* path, struct fuse_file_info* fi);
    static int impl_release(const char* path, struct fuse_file_info* fi);
    static int impl_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info* fi);
};

} // namespace fused 
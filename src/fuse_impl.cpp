#include "fuse_impl.hpp"
#include <errno.h>
#include <string.h>
#include <fuse.h>
#include "util/logger.hpp"

namespace fused {

FuseImpl::FuseImpl(NFSServer& server) : server_(server) {
    init_operations();
}

FuseImpl::~FuseImpl() {
    stop();
}

bool FuseImpl::initialize(const std::string& mount_point) {
    mount_point_ = mount_point;

    // Create FUSE channel
    struct fuse_args args = FUSE_ARGS_INIT(0, nullptr);
    fuse_opt_add_arg(&args, "");  // Program name
    fuse_opt_add_arg(&args, mount_point_.c_str());
    fuse_opt_add_arg(&args, "-f");  // Foreground operation

    channel_ = fuse_mount(mount_point_.c_str(), &args);
    if (!channel_) {
        LOG_ERROR("Failed to create FUSE channel");
        return false;
    }

    // Create FUSE handle
    struct fuse_operations ops = {};
    ops.getattr = operations_.getattr;
    ops.read = operations_.read;
    ops.write = operations_.write;
    ops.mkdir = operations_.mkdir;
    ops.rmdir = operations_.rmdir;
    ops.create = operations_.create;
    ops.unlink = operations_.unlink;
    ops.open = operations_.open;
    ops.release = operations_.release;
    ops.readdir = operations_.readdir;

    fuse_ = fuse_new(channel_, &args, &ops, sizeof(ops), this);
    fuse_opt_free_args(&args);

    if (!fuse_) {
        LOG_ERROR("Failed to create FUSE handle");
        fuse_unmount(mount_point_.c_str(), channel_);
        return false;
    }

    return true;
}

bool FuseImpl::start() {
    if (running_) {
        return true;
    }

    if (!fuse_ || !channel_) {
        LOG_ERROR("FUSE not initialized");
        return false;
    }

    running_ = true;
    fuse_loop(fuse_);  // Blocks until unmounted
    return true;
}

void FuseImpl::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    if (fuse_) {
        fuse_exit(fuse_);
        fuse_destroy(fuse_);
        fuse_ = nullptr;
    }

    if (channel_) {
        fuse_unmount(mount_point_.c_str(), channel_);
        channel_ = nullptr;
    }
}

void FuseImpl::init_operations() {
    operations_.getattr = impl_getattr;
    operations_.read = impl_read;
    operations_.write = impl_write;
    operations_.mkdir = impl_mkdir;
    operations_.rmdir = impl_rmdir;
    operations_.create = impl_create;
    operations_.unlink = impl_unlink;
    operations_.open = impl_open;
    operations_.release = impl_release;
    operations_.readdir = impl_readdir;
}

// Operation implementations...
int FuseImpl::impl_getattr(const char* path, struct stat* stbuf) {
    auto impl = fuse_get_context()->private_data;
    auto self = static_cast<FuseImpl*>(impl);

    NFSFileHandle handle;
    NFSFattr4 attrs;
    if (!self->server_.lookup_path(path, handle) ||
        !self->server_.get_attributes(handle, attrs)) {
        return -ENOENT;
    }

    memset(stbuf, 0, sizeof(struct stat));
    stbuf->st_mode = attrs.mode;
    stbuf->st_nlink = attrs.nlink;
    stbuf->st_uid = attrs.uid;
    stbuf->st_gid = attrs.gid;
    stbuf->st_size = attrs.size;
    stbuf->st_atime = attrs.atime;
    stbuf->st_mtime = attrs.mtime;
    stbuf->st_ctime = attrs.ctime;

    return 0;
}

// Add other operation implementations...

} // namespace fused 
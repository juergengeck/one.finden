#include "compound_ops.hpp"
#include "nfs_server.hpp"
#include "util/logger.hpp"
#include "util/error.hpp"

namespace fused {

CompoundProcessor::CompoundProcessor(NFSServer& server)
    : server_(server) {
}

NFSv4CompoundResponse CompoundProcessor::process(const CompoundSequence& sequence) {
    SCOPED_OPERATION("compound_proc");
    NFSv4CompoundResponse response;
    response.tag = sequence.tag;
    response.status = NFS4_OK;

    LOG_DEBUG("Processing compound sequence with {} operations", sequence.operations.size());

    // Initialize context
    CompoundContext ctx = sequence.context;

    // Process each operation in sequence
    for (auto& op : sequence.operations) {
        if (!process_op(op, ctx)) {
            response.status = ctx.status;
            break;
        }
        response.operations.push_back(op);
    }

    LOG_DEBUG("Completed compound sequence, status: {}", response.status);
    return response;
}

bool CompoundProcessor::process_op(NFSv4CompoundOp& op, CompoundContext& ctx) {
    LOG_DEBUG("Processing operation: {}", static_cast<int>(op.op));

    try {
        switch (op.op) {
            case NFSv4Op::PUTFH:
                return handle_putfh(static_cast<const PutFHArgs&>(*op.args), ctx);
            
            case NFSv4Op::GETFH:
                return handle_getfh(static_cast<const GetFHArgs&>(*op.args), ctx);
            
            case NFSv4Op::PUTROOTFH:
                return handle_putrootfh(ctx);
            
            case NFSv4Op::SAVEFH:
                return handle_savefh(ctx);
            
            case NFSv4Op::RESTOREFH:
                return handle_restorefh(ctx);
            
            case NFSv4Op::LOOKUP:
                return handle_lookup(static_cast<const LookupArgs&>(*op.args), ctx);
            
            case NFSv4Op::GETATTR:
                return handle_getattr(static_cast<const GetAttrArgs&>(*op.args), ctx);
            
            case NFSv4Op::CREATE:
                return handle_create(static_cast<const CreateArgs&>(*op.args), ctx);
            
            case NFSv4Op::REMOVE:
                return handle_remove(static_cast<const RemoveArgs&>(*op.args), ctx);
            
            case NFSv4Op::READ:
                return handle_read(static_cast<const ReadArgs&>(*op.args), ctx);
            
            case NFSv4Op::WRITE:
                return handle_write(static_cast<const WriteArgs&>(*op.args), ctx);
            
            case NFSv4Op::READDIR:
                return handle_readdir(static_cast<const ReadDirArgs&>(*op.args), ctx);
            
            case NFSv4Op::RENAME:
                return handle_rename(static_cast<const RenameArgs&>(*op.args), ctx);
            
            case NFSv4Op::SETATTR:
                return handle_setattr(static_cast<const SetAttrArgs&>(*op.args), ctx);
            
            default:
                LOG_ERROR("Unsupported compound operation: {}", static_cast<int>(op.op));
                ctx.status = NFS4ERR_NOTSUPP;
                return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in compound operation {}: {}", static_cast<int>(op.op), e.what());
        ctx.status = NFS4ERR_SERVERFAULT;
        return false;
    }
}

bool CompoundProcessor::handle_putfh(const PutFHArgs& args, CompoundContext& ctx) {
    LOG_DEBUG("PUTFH operation");
    
    // Validate filehandle
    if (args.handle.handle.empty()) {
        ctx.status = NFS4ERR_BADHANDLE;
        return false;
    }

    // Set current filehandle
    ctx.current_fh = args.handle;
    return true;
}

bool CompoundProcessor::handle_getfh(const GetFHArgs& args, CompoundContext& ctx) {
    LOG_DEBUG("GETFH operation");
    
    // Check if we have a current filehandle
    if (ctx.current_fh.handle.empty()) {
        ctx.status = NFS4ERR_NOFILEHANDLE;
        return false;
    }

    // Return current filehandle in result
    auto result = std::make_unique<GetFHResult>();
    result->status.status = NFS4_OK;
    result->handle = ctx.current_fh;
    
    return true;
}

bool CompoundProcessor::handle_putrootfh(CompoundContext& ctx) {
    LOG_DEBUG("PUTROOTFH operation");
    
    // Get root filehandle from server
    ctx.current_fh = server_.get_root_filehandle();
    if (ctx.current_fh.handle.empty()) {
        ctx.status = NFS4ERR_SERVERFAULT;
        return false;
    }
    
    return true;
}

bool CompoundProcessor::handle_savefh(CompoundContext& ctx) {
    LOG_DEBUG("SAVEFH operation");
    
    // Check if we have a current filehandle
    if (ctx.current_fh.handle.empty()) {
        ctx.status = NFS4ERR_NOFILEHANDLE;
        return false;
    }

    // Save current filehandle
    ctx.saved_fh = ctx.current_fh;
    return true;
}

bool CompoundProcessor::handle_restorefh(CompoundContext& ctx) {
    LOG_DEBUG("RESTOREFH operation");
    
    // Check if we have a saved filehandle
    if (ctx.saved_fh.handle.empty()) {
        ctx.status = NFS4ERR_RESTOREFH;
        return false;
    }

    // Restore saved filehandle to current
    ctx.current_fh = ctx.saved_fh;
    return true;
}

bool CompoundProcessor::handle_lookup(const LookupArgs& args, CompoundContext& ctx) {
    LOG_DEBUG("LOOKUP operation: {}", args.name);
    
    // Check if we have a current filehandle
    if (ctx.current_fh.handle.empty()) {
        ctx.status = NFS4ERR_NOFILEHANDLE;
        return false;
    }

    // Get current directory path
    std::string dir_path = server_.translate_handle_to_path(ctx.current_fh);
    if (dir_path.empty()) {
        ctx.status = NFS4ERR_STALE;
        return false;
    }

    // Create full path for lookup
    std::string lookup_path = dir_path + "/" + args.name;
    
    // Check if file exists
    struct stat st;
    if (stat(lookup_path.c_str(), &st) != 0) {
        ctx.status = NFS4ERR_NOENT;
        return false;
    }

    // Create new filehandle for the looked-up file
    ctx.current_fh = server_.create_file_handle(lookup_path);
    return true;
}

bool CompoundProcessor::handle_getattr(const GetAttrArgs& args, CompoundContext& ctx) {
    LOG_DEBUG("GETATTR operation");
    
    // Check if we have a current filehandle
    if (ctx.current_fh.handle.empty()) {
        ctx.status = NFS4ERR_NOFILEHANDLE;
        return false;
    }

    // Get file path
    std::string path = server_.translate_handle_to_path(ctx.current_fh);
    if (path.empty()) {
        ctx.status = NFS4ERR_STALE;
        return false;
    }

    // Get file attributes
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        ctx.status = NFS4ERR_NOENT;
        return false;
    }

    // Create result
    auto result = std::make_unique<GetAttrResult>();
    result->status.status = NFS4_OK;
    
    // Fill requested attributes
    if (args.attr_request & FATTR4_TYPE)
        result->attrs.type = server_.get_nfs_type(st.st_mode);
    if (args.attr_request & FATTR4_MODE)
        result->attrs.mode = st.st_mode & 0777;
    // ... fill other requested attributes ...

    return true;
}

bool CompoundProcessor::handle_create(const CreateArgs& args, CompoundContext& ctx) {
    LOG_DEBUG("CREATE operation");
    
    // Check if we have a current filehandle
    if (ctx.current_fh.handle.empty()) {
        ctx.status = NFS4ERR_NOFILEHANDLE;
        return false;
    }

    // Create file
    std::string path = server_.translate_handle_to_path(ctx.current_fh);
    if (path.empty()) {
        ctx.status = NFS4ERR_STALE;
        return false;
    }

    // Create file with the specified mode
    if (mkdir(path.c_str(), args.mode) != 0) {
        ctx.status = NFS4ERR_IO;
        return false;
    }

    return true;
}

bool CompoundProcessor::handle_remove(const RemoveArgs& args, CompoundContext& ctx) {
    LOG_DEBUG("REMOVE operation");
    
    // Check if we have a current filehandle
    if (ctx.current_fh.handle.empty()) {
        ctx.status = NFS4ERR_NOFILEHANDLE;
        return false;
    }

    // Remove file
    std::string path = server_.translate_handle_to_path(ctx.current_fh);
    if (path.empty()) {
        ctx.status = NFS4ERR_STALE;
        return false;
    }

    // Remove file
    if (unlink(path.c_str()) != 0) {
        ctx.status = NFS4ERR_IO;
        return false;
    }

    return true;
}

bool CompoundProcessor::handle_read(const ReadArgs& args, CompoundContext& ctx) {
    LOG_DEBUG("READ operation: offset={}, count={}", args.offset, args.count);
    
    // Check if we have a current filehandle
    if (ctx.current_fh.handle.empty()) {
        ctx.status = NFS4ERR_NOFILEHANDLE;
        return false;
    }

    // Get file path
    std::string path = server_.translate_handle_to_path(ctx.current_fh);
    if (path.empty()) {
        ctx.status = NFS4ERR_STALE;
        return false;
    }

    // Open file
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        ctx.status = NFS4ERR_IO;
        return false;
    }

    // Create result
    auto result = std::make_unique<ReadResult>();
    result->data.resize(args.count);

    // Seek to position
    if (lseek(fd, args.offset, SEEK_SET) < 0) {
        close(fd);
        ctx.status = NFS4ERR_IO;
        return false;
    }

    // Read data
    ssize_t bytes_read = read(fd, result->data.data(), args.count);
    close(fd);

    if (bytes_read < 0) {
        ctx.status = NFS4ERR_IO;
        return false;
    }

    result->count = bytes_read;
    result->eof = (bytes_read < static_cast<ssize_t>(args.count));
    result->status.status = NFS4_OK;

    return true;
}

bool CompoundProcessor::handle_write(const WriteArgs& args, CompoundContext& ctx) {
    LOG_DEBUG("WRITE operation: offset={}, size={}", args.offset, args.data.size());
    
    // Check if we have a current filehandle
    if (ctx.current_fh.handle.empty()) {
        ctx.status = NFS4ERR_NOFILEHANDLE;
        return false;
    }

    // Get file path
    std::string path = server_.translate_handle_to_path(ctx.current_fh);
    if (path.empty()) {
        ctx.status = NFS4ERR_STALE;
        return false;
    }

    // Open file
    int fd = open(path.c_str(), O_WRONLY);
    if (fd < 0) {
        ctx.status = NFS4ERR_IO;
        return false;
    }

    // Seek to position
    if (lseek(fd, args.offset, SEEK_SET) < 0) {
        close(fd);
        ctx.status = NFS4ERR_IO;
        return false;
    }

    // Write data
    ssize_t bytes_written = write(fd, args.data.data(), args.data.size());
    
    // Sync to disk
    fsync(fd);
    close(fd);

    if (bytes_written < 0) {
        ctx.status = NFS4ERR_IO;
        return false;
    }

    auto result = std::make_unique<WriteResult>();
    result->count = bytes_written;
    result->committed = true;
    result->status.status = NFS4_OK;

    return true;
}

bool CompoundProcessor::handle_readdir(const ReadDirArgs& args, CompoundContext& ctx) {
    LOG_DEBUG("READDIR operation");
    
    // Check if we have a current filehandle
    if (ctx.current_fh.handle.empty()) {
        ctx.status = NFS4ERR_NOFILEHANDLE;
        return false;
    }

    // Get directory path
    std::string dir_path = server_.translate_handle_to_path(ctx.current_fh);
    if (dir_path.empty()) {
        ctx.status = NFS4ERR_STALE;
        return false;
    }

    // Open directory
    DIR* dir = opendir(dir_path.c_str());
    if (!dir) {
        ctx.status = NFS4ERR_NOTDIR;
        return false;
    }

    // Create result
    auto result = std::make_unique<ReadDirResult>();
    result->status.status = NFS4_OK;
    result->eof = false;

    // Skip to cookie position
    if (args.cookie > 0) {
        seekdir(dir, args.cookie);
    }

    // Read directory entries
    size_t total_size = 0;
    while (struct dirent* entry = readdir(dir)) {
        // Skip "." and ".." entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Create full path for stat
        std::string full_path = dir_path + "/" + entry->d_name;
        struct stat st;
        
        if (stat(full_path.c_str(), &st) != 0) {
            continue;  // Skip entries we can't stat
        }

        // Check if we would exceed the response size limit
        size_t entry_size = sizeof(NFSDirEntry) + strlen(entry->d_name);
        if (total_size + entry_size > args.maxcount) {
            result->eof = false;
            break;
        }

        // Add entry to result
        NFSDirEntry dir_entry;
        dir_entry.fileid = st.st_ino;
        dir_entry.name = entry->d_name;
        dir_entry.cookie = telldir(dir);

        // Fill in attributes
        dir_entry.attrs.type = server_.get_nfs_type(st.st_mode);
        dir_entry.attrs.mode = st.st_mode & 0777;
        dir_entry.attrs.nlink = st.st_nlink;
        dir_entry.attrs.uid = st.st_uid;
        dir_entry.attrs.gid = st.st_gid;
        dir_entry.attrs.size = st.st_size;
        dir_entry.attrs.used = st.st_blocks * 512;
        dir_entry.attrs.fsid = st.st_dev;
        dir_entry.attrs.fileid = st.st_ino;
        dir_entry.attrs.atime = st.st_atime;
        dir_entry.attrs.mtime = st.st_mtime;
        dir_entry.attrs.ctime = st.st_ctime;

        result->entries.push_back(std::move(dir_entry));
        total_size += entry_size;
    }

    // Check if we reached the end
    result->eof = (readdir(dir) == nullptr);
    closedir(dir);

    return true;
}

bool CompoundProcessor::handle_rename(const RenameArgs& args, CompoundContext& ctx) {
    LOG_DEBUG("RENAME operation: {} -> {}", args.old_name, args.new_name);
    
    // Check if we have both current and saved filehandles
    if (ctx.current_fh.handle.empty() || ctx.saved_fh.handle.empty()) {
        ctx.status = NFS4ERR_NOFILEHANDLE;
        return false;
    }

    // Get source and destination paths
    std::string src_dir = server_.translate_handle_to_path(ctx.saved_fh);
    std::string dst_dir = server_.translate_handle_to_path(ctx.current_fh);
    
    if (src_dir.empty() || dst_dir.empty()) {
        ctx.status = NFS4ERR_STALE;
        return false;
    }

    std::string src_path = src_dir + "/" + args.old_name;
    std::string dst_path = dst_dir + "/" + args.new_name;

    // Check if source exists
    struct stat src_st;
    if (stat(src_path.c_str(), &src_st) != 0) {
        ctx.status = NFS4ERR_NOENT;
        return false;
    }

    // Check if source and destination are on same filesystem
    struct stat dst_st;
    if (stat(dst_dir.c_str(), &dst_st) != 0) {
        ctx.status = NFS4ERR_IO;
        return false;
    }

    if (src_st.st_dev != dst_st.st_dev) {
        ctx.status = NFS4ERR_XDEV;
        return false;
    }

    // Perform rename
    if (rename(src_path.c_str(), dst_path.c_str()) != 0) {
        switch (errno) {
            case EEXIST:
                ctx.status = NFS4ERR_EXIST;
                break;
            case ENOTEMPTY:
                ctx.status = NFS4ERR_NOTEMPTY;
                break;
            case EACCES:
                ctx.status = NFS4ERR_ACCESS;
                break;
            default:
                ctx.status = NFS4ERR_IO;
                break;
        }
        return false;
    }

    // Update file handle mappings
    server_.update_handle_mapping(src_path, dst_path);
    return true;
}

bool CompoundProcessor::handle_setattr(const SetAttrArgs& args, CompoundContext& ctx) {
    LOG_DEBUG("SETATTR operation");
    
    // Check if we have a current filehandle
    if (ctx.current_fh.handle.empty()) {
        ctx.status = NFS4ERR_NOFILEHANDLE;
        return false;
    }

    // Get file path
    std::string path = server_.translate_handle_to_path(ctx.current_fh);
    if (path.empty()) {
        ctx.status = NFS4ERR_STALE;
        return false;
    }

    // Get current attributes
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        ctx.status = NFS4ERR_NOENT;
        return false;
    }

    // Apply requested changes
    if (args.attr_mask & FATTR4_MODE) {
        if (chmod(path.c_str(), args.attrs.mode) != 0) {
            ctx.status = NFS4ERR_ACCESS;
            return false;
        }
    }

    if (args.attr_mask & (FATTR4_UID | FATTR4_GID)) {
        uid_t uid = (args.attr_mask & FATTR4_UID) ? args.attrs.uid : st.st_uid;
        gid_t gid = (args.attr_mask & FATTR4_GID) ? args.attrs.gid : st.st_gid;
        
        if (chown(path.c_str(), uid, gid) != 0) {
            ctx.status = NFS4ERR_ACCESS;
            return false;
        }
    }

    if (args.attr_mask & FATTR4_SIZE) {
        if (truncate(path.c_str(), args.attrs.size) != 0) {
            ctx.status = NFS4ERR_IO;
            return false;
        }
    }

    if (args.attr_mask & (FATTR4_ATIME | FATTR4_MTIME)) {
        struct timeval times[2];
        times[0].tv_sec = (args.attr_mask & FATTR4_ATIME) ? args.attrs.atime : st.st_atime;
        times[0].tv_usec = 0;
        times[1].tv_sec = (args.attr_mask & FATTR4_MTIME) ? args.attrs.mtime : st.st_mtime;
        times[1].tv_usec = 0;

        if (utimes(path.c_str(), times) != 0) {
            ctx.status = NFS4ERR_ACCESS;
            return false;
        }
    }

    return true;
}

} // namespace fuse_t 
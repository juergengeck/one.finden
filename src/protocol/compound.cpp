#include "compound.hpp"
#include "operations.hpp"
#include <stdexcept>

namespace fused {

// CompoundProc implementation
CompoundProc::CompoundProc(AuthSysVerifier& auth_verifier)
    : auth_verifier_(auth_verifier) {
}

NFSv4CompoundResponse CompoundProc::process(const NFSv4CompoundRequest& request) {
    NFSv4CompoundResponse response;
    response.tag = request.tag;
    response.status = 0;  // NFS4_OK
    
    // Verify credentials
    if (!auth_verifier_.verify_creds(request.auth)) {
        response.status = NFS4ERR_PERM;
        return response;
    }
    
    // Store auth params for the duration of compound processing
    state_.auth = request.auth;
    
    // Process each operation in sequence
    for (const auto& op : request.operations) {
        auto result = process_op(op);
        if (!result) {
            response.status = NFS4ERR_SERVERFAULT;
            break;
        }
        
        // Check operation status
        uint32_t op_status = result->get_status();
        if (op_status != NFS4_OK) {
            response.status = op_status;
            break;
        }
        
        // Add result to response
        NFSv4CompoundOp resp_op;
        resp_op.op = op.op;
        resp_op.result = std::move(result);
        response.operations.push_back(std::move(resp_op));
    }
    
    return response;
}

std::unique_ptr<NFSv4OpResult> CompoundProc::process_op(const NFSv4CompoundOp& op) {
    switch (op.op) {
        case NFSv4Op::PUTFH:
            return process_putfh(static_cast<const PutFHArgs&>(*op.args));
            
        case NFSv4Op::GETFH:
            return process_getfh(static_cast<const GetFHArgs&>(*op.args));
            
        case NFSv4Op::GETATTR:
            return process_getattr(static_cast<const GetAttrArgs&>(*op.args));
            
        case NFSv4Op::LOOKUP:
            return process_lookup(static_cast<const LookupArgs&>(*op.args));
            
        case NFSv4Op::CREATE:
            return process_create(static_cast<const CreateArgs&>(*op.args));
            
        case NFSv4Op::REMOVE:
            return process_remove(static_cast<const RemoveArgs&>(*op.args));
            
        case NFSv4Op::RENAME:
            return process_rename(static_cast<const RenameArgs&>(*op.args));
            
        case NFSv4Op::SETATTR:
            return process_setattr(static_cast<const SetAttrArgs&>(*op.args));
            
        case NFSv4Op::READLINK:
            return process_readlink(static_cast<const ReadLinkArgs&>(*op.args));
            
        case NFSv4Op::SYMLINK:
            return process_symlink(static_cast<const SymLinkArgs&>(*op.args));
            
        default:
            throw std::runtime_error("Unsupported operation");
    }
}

// Individual operation processors
std::unique_ptr<NFSv4OpResult> CompoundProc::process_putfh(const PutFHArgs& args) {
    auto result = std::make_unique<PutFHResult>();
    
    // Store the file handle
    state_.current_fh = args.handle;
    result->status.status = NFS4_OK;
    
    return result;
}

std::unique_ptr<NFSv4OpResult> CompoundProc::process_getfh(const GetFHArgs& args) {
    auto result = std::make_unique<GetFHResult>();
    
    // Return current filehandle
    if (state_.current_fh.handle.empty()) {
        result->status.status = NFS4ERR_NOFILEHANDLE;
    } else {
        result->status.status = NFS4_OK;
        result->handle = state_.current_fh;
    }
    
    return result;
}

std::unique_ptr<NFSv4OpResult> CompoundProc::process_getattr(const GetAttrArgs& args) {
    auto result = std::make_unique<GetAttrResult>();
    
    // Check if we have a current filehandle
    if (state_.current_fh.handle.empty()) {
        result->status.status = NFS4ERR_NOFILEHANDLE;
        return result;
    }
    
    // Get the path from the current filehandle
    std::string path = translate_handle_to_path(state_.current_fh);
    if (path.empty()) {
        result->status.status = NFS4ERR_STALE;
        return result;
    }
    
    // Get file attributes
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        result->status.status = NFS4ERR_NOENT;
        return result;
    }
    
    // Fill in requested attributes
    result->status.status = NFS4_OK;
    if (args.attr_request & FATTR4_TYPE)
        result->attrs.type = get_nfs_type(st.st_mode);
    if (args.attr_request & FATTR4_MODE)
        result->attrs.mode = st.st_mode & 0777;
    if (args.attr_request & FATTR4_NUMLINKS)
        result->attrs.nlink = st.st_nlink;
    if (args.attr_request & FATTR4_OWNER)
        result->attrs.uid = st.st_uid;
    if (args.attr_request & FATTR4_OWNER_GROUP)
        result->attrs.gid = st.st_gid;
    if (args.attr_request & FATTR4_SIZE)
        result->attrs.size = st.st_size;
    if (args.attr_request & FATTR4_SPACE_USED)
        result->attrs.used = st.st_blocks * 512;
    if (args.attr_request & FATTR4_FSID)
        result->attrs.fsid = st.st_dev;
    if (args.attr_request & FATTR4_FILEID)
        result->attrs.fileid = st.st_ino;
    if (args.attr_request & FATTR4_TIME_ACCESS)
        result->attrs.atime = st.st_atime;
    if (args.attr_request & FATTR4_TIME_MODIFY)
        result->attrs.mtime = st.st_mtime;
    if (args.attr_request & FATTR4_TIME_METADATA)
        result->attrs.ctime = st.st_ctime;
    
    return result;
}

std::unique_ptr<NFSv4OpResult> CompoundProc::process_lookup(const LookupArgs& args) {
    auto result = std::make_unique<LookupResult>();
    
    // Check if we have a current filehandle
    if (state_.current_fh.handle.empty()) {
        result->status.status = NFS4ERR_NOFILEHANDLE;
        return result;
    }
    
    // Get the current directory path
    std::string dir_path = translate_handle_to_path(state_.current_fh);
    if (dir_path.empty()) {
        result->status.status = NFS4ERR_STALE;
        return result;
    }
    
    // Create full path for lookup
    std::string lookup_path = dir_path + "/" + args.name;
    
    // Check if file exists
    struct stat st;
    if (stat(lookup_path.c_str(), &st) != 0) {
        result->status.status = NFS4ERR_NOENT;
        return result;
    }
    
    // Create new filehandle for the looked-up file
    state_.current_fh = create_file_handle(lookup_path);
    result->status.status = NFS4_OK;
    
    return result;
}

std::unique_ptr<NFSv4OpResult> CompoundProc::process_create(const CreateArgs& args) {
    auto result = std::make_unique<CreateResult>();
    
    // Check if we have a current filehandle
    if (state_.current_fh.handle.empty()) {
        result->status.status = NFS4ERR_NOFILEHANDLE;
        return result;
    }
    
    // Get the current directory path
    std::string dir_path = translate_handle_to_path(state_.current_fh);
    if (dir_path.empty()) {
        result->status.status = NFS4ERR_STALE;
        return result;
    }
    
    // Create full path for new file
    std::string new_path = dir_path + "/" + args.name;
    
    // Check if file already exists
    struct stat st;
    if (stat(new_path.c_str(), &st) == 0) {
        result->status.status = NFS4ERR_EXIST;
        return result;
    }
    
    // Create the file based on type
    switch (args.type) {
        case NFSType::REG: {
            // Create regular file
            int fd = open(new_path.c_str(), O_CREAT | O_WRONLY | O_EXCL, args.attrs.mode);
            if (fd < 0) {
                result->status.status = NFS4ERR_IO;
                return result;
            }
            close(fd);
            break;
        }
        case NFSType::DIR:
            if (mkdir(new_path.c_str(), args.attrs.mode) != 0) {
                result->status.status = NFS4ERR_IO;
                return result;
            }
            break;
        default:
            result->status.status = NFS4ERR_NOTSUPP;
            return result;
    }
    
    // Set ownership if requested
    if (args.attrs.uid != static_cast<uint32_t>(-1) || 
        args.attrs.gid != static_cast<uint32_t>(-1)) {
        if (chown(new_path.c_str(), args.attrs.uid, args.attrs.gid) != 0) {
            // Clean up on error
            unlink(new_path.c_str());
            result->status.status = NFS4ERR_PERM;
            return result;
        }
    }
    
    // Create file handle for new file
    result->handle = create_file_handle(new_path);
    result->status.status = NFS4_OK;
    
    // Get final attributes
    if (stat(new_path.c_str(), &st) == 0) {
        result->attrs.type = get_nfs_type(st.st_mode);
        result->attrs.mode = st.st_mode & 0777;
        result->attrs.nlink = st.st_nlink;
        result->attrs.uid = st.st_uid;
        result->attrs.gid = st.st_gid;
        result->attrs.size = st.st_size;
        result->attrs.used = st.st_blocks * 512;
        result->attrs.fsid = st.st_dev;
        result->attrs.fileid = st.st_ino;
        result->attrs.atime = st.st_atime;
        result->attrs.mtime = st.st_mtime;
        result->attrs.ctime = st.st_ctime;
    }
    
    return result;
}

std::unique_ptr<NFSv4OpResult> CompoundProc::process_remove(const RemoveArgs& args) {
    auto result = std::make_unique<RemoveResult>();
    
    // Check if we have a current filehandle
    if (state_.current_fh.handle.empty()) {
        result->status.status = NFS4ERR_NOFILEHANDLE;
        return result;
    }
    
    // Get the current directory path
    std::string dir_path = translate_handle_to_path(state_.current_fh);
    if (dir_path.empty()) {
        result->status.status = NFS4ERR_STALE;
        return result;
    }
    
    // Create full path for file to remove
    std::string remove_path = dir_path + "/" + args.name;
    
    // Check file type and remove accordingly
    struct stat st;
    if (stat(remove_path.c_str(), &st) != 0) {
        result->status.status = NFS4ERR_NOENT;
        return result;
    }
    
    if (S_ISDIR(st.st_mode)) {
        if (rmdir(remove_path.c_str()) != 0) {
            result->status.status = errno == ENOTEMPTY ? NFS4ERR_NOTEMPTY : NFS4ERR_IO;
            return result;
        }
    } else {
        if (unlink(remove_path.c_str()) != 0) {
            result->status.status = NFS4ERR_IO;
            return result;
        }
    }
    
    // Remove file handle mapping
    remove_file_handle(remove_path);
    result->status.status = NFS4_OK;
    
    return result;
}

std::unique_ptr<NFSv4OpResult> CompoundProc::process_rename(const RenameArgs& args) {
    auto result = std::make_unique<RenameResult>();
    
    // Check if we have a current filehandle (source directory)
    if (state_.current_fh.handle.empty()) {
        result->status.status = NFS4ERR_NOFILEHANDLE;
        return result;
    }
    
    // Get source directory path
    std::string src_dir_path = translate_handle_to_path(state_.current_fh);
    if (src_dir_path.empty()) {
        result->status.status = NFS4ERR_STALE;
        return result;
    }
    
    // Get destination directory path
    std::string dst_dir_path = translate_handle_to_path(args.dst_dir_handle);
    if (dst_dir_path.empty()) {
        result->status.status = NFS4ERR_STALE;
        return result;
    }
    
    // Create full paths
    std::string old_path = src_dir_path + "/" + args.old_name;
    std::string new_path = dst_dir_path + "/" + args.new_name;
    
    // Check if source exists
    struct stat src_st;
    if (stat(old_path.c_str(), &src_st) != 0) {
        result->status.status = NFS4ERR_NOENT;
        return result;
    }
    
    // Check if source and destination are on same filesystem
    struct stat dst_st;
    if (stat(dst_dir_path.c_str(), &dst_st) != 0) {
        result->status.status = NFS4ERR_IO;
        return result;
    }
    
    if (src_st.st_dev != dst_st.st_dev) {
        result->status.status = NFS4ERR_XDEV;
        return result;
    }
    
    // Perform the rename
    if (rename(old_path.c_str(), new_path.c_str()) != 0) {
        switch (errno) {
            case EEXIST:
                result->status.status = NFS4ERR_EXIST;
                break;
            case ENOTEMPTY:
                result->status.status = NFS4ERR_NOTEMPTY;
                break;
            case EACCES:
                result->status.status = NFS4ERR_ACCESS;
                break;
            default:
                result->status.status = NFS4ERR_IO;
                break;
        }
        return result;
    }
    
    // Update file handle mappings
    {
        std::lock_guard<std::mutex> lock(handle_map_mutex_);
        auto it = path_to_handle_.find(old_path);
        if (it != path_to_handle_.end()) {
            std::string handle_str(it->second.begin(), it->second.end());
            handle_to_path_[handle_str] = new_path;
            path_to_handle_[new_path] = std::move(it->second);
            path_to_handle_.erase(it);
        }
    }
    
    result->status.status = NFS4_OK;
    return result;
}

std::unique_ptr<NFSv4OpResult> CompoundProc::process_setattr(const SetAttrArgs& args) {
    auto result = std::make_unique<SetAttrResult>();
    
    // Check if we have a current filehandle
    if (state_.current_fh.handle.empty()) {
        result->status.status = NFS4ERR_NOFILEHANDLE;
        return result;
    }
    
    // Get the path from the current filehandle
    std::string path = translate_handle_to_path(state_.current_fh);
    if (path.empty()) {
        result->status.status = NFS4ERR_STALE;
        return result;
    }
    
    // Get current attributes
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        result->status.status = NFS4ERR_NOENT;
        return result;
    }
    
    // Apply requested changes
    if (args.attr_mask & FATTR4_MODE) {
        if (chmod(path.c_str(), args.attrs.mode) != 0) {
            result->status.status = NFS4ERR_ACCESS;
            return result;
        }
    }
    
    if (args.attr_mask & (FATTR4_OWNER | FATTR4_OWNER_GROUP)) {
        uid_t uid = (args.attr_mask & FATTR4_OWNER) ? args.attrs.uid : st.st_uid;
        gid_t gid = (args.attr_mask & FATTR4_OWNER_GROUP) ? args.attrs.gid : st.st_gid;
        
        if (chown(path.c_str(), uid, gid) != 0) {
            result->status.status = NFS4ERR_ACCESS;
            return result;
        }
    }
    
    if (args.attr_mask & FATTR4_SIZE) {
        if (truncate(path.c_str(), args.attrs.size) != 0) {
            result->status.status = NFS4ERR_IO;
            return result;
        }
    }
    
    // Get updated attributes
    if (stat(path.c_str(), &st) != 0) {
        result->status.status = NFS4ERR_IO;
        return result;
    }
    
    // Fill in current attributes
    result->status.status = NFS4_OK;
    result->attrs.type = get_nfs_type(st.st_mode);
    result->attrs.mode = st.st_mode & 0777;
    result->attrs.nlink = st.st_nlink;
    result->attrs.uid = st.st_uid;
    result->attrs.gid = st.st_gid;
    result->attrs.size = st.st_size;
    result->attrs.used = st.st_blocks * 512;
    result->attrs.fsid = st.st_dev;
    result->attrs.fileid = st.st_ino;
    result->attrs.atime = st.st_atime;
    result->attrs.mtime = st.st_mtime;
    result->attrs.ctime = st.st_ctime;
    
    return result;
}

std::unique_ptr<NFSv4OpResult> CompoundProc::process_readlink(const ReadLinkArgs& args) {
    auto result = std::make_unique<ReadLinkResult>();
    
    // Check if we have a current filehandle
    if (state_.current_fh.handle.empty()) {
        result->status.status = NFS4ERR_NOFILEHANDLE;
        return result;
    }
    
    // Get the path from the current filehandle
    std::string path = translate_handle_to_path(state_.current_fh);
    if (path.empty()) {
        result->status.status = NFS4ERR_STALE;
        return result;
    }
    
    // Check if it's a symlink
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) {
        result->status.status = NFS4ERR_NOENT;
        return result;
    }
    
    if (!S_ISLNK(st.st_mode)) {
        result->status.status = NFS4ERR_INVAL;
        return result;
    }
    
    // Read the link content
    char buffer[PATH_MAX];
    ssize_t len = readlink(path.c_str(), buffer, sizeof(buffer) - 1);
    if (len < 0) {
        result->status.status = NFS4ERR_IO;
        return result;
    }
    
    buffer[len] = '\0';
    result->link_content = buffer;
    result->status.status = NFS4_OK;
    
    return result;
}

std::unique_ptr<NFSv4OpResult> CompoundProc::process_symlink(const SymLinkArgs& args) {
    auto result = std::make_unique<SymLinkResult>();
    
    // Check if we have a current filehandle
    if (state_.current_fh.handle.empty()) {
        result->status.status = NFS4ERR_NOFILEHANDLE;
        return result;
    }
    
    // Get the current directory path
    std::string dir_path = translate_handle_to_path(state_.current_fh);
    if (dir_path.empty()) {
        result->status.status = NFS4ERR_STALE;
        return result;
    }
    
    // Create full path for new symlink
    std::string link_path = dir_path + "/" + args.name;
    
    // Check if file already exists
    struct stat st;
    if (lstat(link_path.c_str(), &st) == 0) {
        result->status.status = NFS4ERR_EXIST;
        return result;
    }
    
    // Create the symlink
    if (symlink(args.link_data.c_str(), link_path.c_str()) != 0) {
        result->status.status = NFS4ERR_IO;
        return result;
    }
    
    // Set ownership if requested
    if (args.attrs.uid != static_cast<uint32_t>(-1) || 
        args.attrs.gid != static_cast<uint32_t>(-1)) {
        if (lchown(link_path.c_str(), args.attrs.uid, args.attrs.gid) != 0) {
            // Clean up on error
            unlink(link_path.c_str());
            result->status.status = NFS4ERR_PERM;
            return result;
        }
    }
    
    // Create file handle for new symlink
    result->handle = create_file_handle(link_path);
    result->status.status = NFS4_OK;
    
    // Get final attributes
    if (lstat(link_path.c_str(), &st) == 0) {
        result->attrs.type = NFSType::LNK;
        result->attrs.mode = st.st_mode & 0777;
        result->attrs.nlink = st.st_nlink;
        result->attrs.uid = st.st_uid;
        result->attrs.gid = st.st_gid;
        result->attrs.size = st.st_size;
        result->attrs.used = st.st_blocks * 512;
        result->attrs.fsid = st.st_dev;
        result->attrs.fileid = st.st_ino;
        result->attrs.atime = st.st_atime;
        result->attrs.mtime = st.st_mtime;
        result->attrs.ctime = st.st_ctime;
    }
    
    return result;
}

} // namespace fuse_t 
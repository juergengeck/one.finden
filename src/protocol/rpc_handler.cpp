#include "rpc_handler.hpp"
#include "util/logger.hpp"
#include "util/rpc_metrics.hpp"

namespace fused {

// Base class implementation
void RPCProcedureHandler::send_error_reply(const RPCCallHeader& call_header,
                                         RPCAcceptStatus status,
                                         std::vector<uint8_t>& reply_data) {
    RPCReplyHeader reply_header{
        call_header.xid,
        RPCMessageType::REPLY,
        RPCReplyStatus::MSG_ACCEPTED,
        status,
        {}  // Empty verifier
    };

    XDREncoder encoder;
    encoder.encode(reply_header.xid);
    encoder.encode(static_cast<uint32_t>(reply_header.type));
    encoder.encode(static_cast<uint32_t>(reply_header.reply_status));
    encoder.encode(static_cast<uint32_t>(reply_header.accept_status));
    encoder.encode_opaque(reply_header.verifier);

    reply_data = encoder.get_buffer();
}

// NFS procedure handler implementation
bool NFSProcedureHandler::handle_call(const RPCCallHeader& call_header,
                                    const std::vector<uint8_t>& call_data,
                                    size_t& offset,
                                    std::vector<uint8_t>& reply_data) {
    std::string operation = "UNKNOWN";
    switch (static_cast<NFSProcedure>(call_header.procedure)) {
        case NFSProcedure::NULL_PROC: operation = "NULL"; break;
        case NFSProcedure::GETATTR: operation = "GETATTR"; break;
        case NFSProcedure::LOOKUP: operation = "LOOKUP"; break;
        case NFSProcedure::READ: operation = "READ"; break;
        case NFSProcedure::WRITE: operation = "WRITE"; break;
        case NFSProcedure::CREATE: operation = "CREATE"; break;
        case NFSProcedure::REMOVE: operation = "REMOVE"; break;
        // Add more cases...
    }

    ScopedRPCMetrics metrics(operation);
    metrics.set_input_bytes(call_data.size());

    bool result = false;
    try {
        switch (static_cast<NFSProcedure>(call_header.procedure)) {
            case NFSProcedure::NULL_PROC:
                result = handle_null(call_header, call_data, offset, reply_data);
                break;
            case NFSProcedure::GETATTR:
                result = handle_getattr(call_header, call_data, offset, reply_data);
                break;
            case NFSProcedure::LOOKUP:
                result = handle_lookup(call_header, call_data, offset, reply_data);
                break;
            case NFSProcedure::READ:
                result = handle_read(call_header, call_data, offset, reply_data);
                break;
            case NFSProcedure::WRITE:
                result = handle_write(call_header, call_data, offset, reply_data);
                break;
            case NFSProcedure::CREATE:
                result = handle_create(call_header, call_data, offset, reply_data);
                break;
            case NFSProcedure::REMOVE:
                result = handle_remove(call_header, call_data, offset, reply_data);
                break;
            // Add more cases...
            default:
                LOG_ERROR("Unsupported NFS procedure: {}", call_header.procedure);
                send_error_reply(call_header, RPCAcceptStatus::PROC_UNAVAIL, reply_data);
                result = true;
                break;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in RPC handler: {}", e.what());
        send_error_reply(call_header, RPCAcceptStatus::SYSTEM_ERR, reply_data);
        result = true;
    }

    metrics.set_success(result);
    metrics.set_output_bytes(reply_data.size());
    return result;
}

bool NFSProcedureHandler::handle_null(const RPCCallHeader& call_header,
                                    const std::vector<uint8_t>& call_data,
                                    size_t& offset,
                                    std::vector<uint8_t>& reply_data) {
    // NULL procedure just sends success response
    RPCReplyHeader reply_header{
        call_header.xid,
        RPCMessageType::REPLY,
        RPCReplyStatus::MSG_ACCEPTED,
        RPCAcceptStatus::SUCCESS,
        {}  // Empty verifier
    };

    XDREncoder encoder;
    encoder.encode(reply_header.xid);
    encoder.encode(static_cast<uint32_t>(reply_header.type));
    encoder.encode(static_cast<uint32_t>(reply_header.reply_status));
    encoder.encode(static_cast<uint32_t>(reply_header.accept_status));
    encoder.encode_opaque(reply_header.verifier);

    reply_data = encoder.get_buffer();
    return true;
}

bool NFSProcedureHandler::handle_getattr(const RPCCallHeader& call_header,
                                       const std::vector<uint8_t>& call_data,
                                       size_t& offset,
                                       std::vector<uint8_t>& reply_data) {
    // Decode GETATTR arguments
    XDRDecoder decoder(call_data);
    decoder.set_offset(offset);

    NFSFileHandle file_handle;
    if (!decoder.decode_opaque(file_handle.handle)) {
        LOG_ERROR("Failed to decode GETATTR arguments");
        send_error_reply(call_header, RPCAcceptStatus::GARBAGE_ARGS, reply_data);
        return true;
    }

    // Get file attributes
    std::string path = server_.translate_handle_to_path(file_handle.handle);
    struct stat st;
    
    if (stat(path.c_str(), &st) != 0) {
        // Send NOENT error
        XDREncoder encoder;
        encoder.encode(call_header.xid);
        encoder.encode(static_cast<uint32_t>(RPCMessageType::REPLY));
        encoder.encode(static_cast<uint32_t>(RPCReplyStatus::MSG_ACCEPTED));
        encoder.encode(static_cast<uint32_t>(RPCAcceptStatus::SUCCESS));
        encoder.encode_opaque(std::vector<uint8_t>());  // Empty verifier
        encoder.encode(static_cast<uint32_t>(NFSStatus::NOENT));
        reply_data = encoder.get_buffer();
        return true;
    }

    // Encode success response with attributes
    XDREncoder encoder;
    encoder.encode(call_header.xid);
    encoder.encode(static_cast<uint32_t>(RPCMessageType::REPLY));
    encoder.encode(static_cast<uint32_t>(RPCReplyStatus::MSG_ACCEPTED));
    encoder.encode(static_cast<uint32_t>(RPCAcceptStatus::SUCCESS));
    encoder.encode_opaque(std::vector<uint8_t>());  // Empty verifier
    encoder.encode(static_cast<uint32_t>(NFSStatus::OK));

    // Encode file attributes
    NFSFattr4 attrs;
    attrs.type = server_.get_nfs_type(st.st_mode);
    attrs.mode = st.st_mode & 0777;
    attrs.nlink = st.st_nlink;
    attrs.uid = st.st_uid;
    attrs.gid = st.st_gid;
    attrs.size = st.st_size;
    attrs.used = st.st_blocks * 512;
    attrs.rdev = st.st_rdev;
    attrs.fsid = st.st_dev;
    attrs.fileid = st.st_ino;
    attrs.atime = st.st_atime;
    attrs.mtime = st.st_mtime;
    attrs.ctime = st.st_ctime;

    encoder.encode(attrs);
    reply_data = encoder.get_buffer();
    return true;
}

bool NFSProcedureHandler::handle_lookup(const RPCCallHeader& call_header,
                                      const std::vector<uint8_t>& call_data,
                                      size_t& offset,
                                      std::vector<uint8_t>& reply_data) {
    // Decode LOOKUP arguments
    XDRDecoder decoder(call_data);
    decoder.set_offset(offset);

    NFSFileHandle dir_handle;
    std::string name;
    if (!decoder.decode_opaque(dir_handle.handle) ||
        !decoder.decode_string(name)) {
        LOG_ERROR("Failed to decode LOOKUP arguments");
        send_error_reply(call_header, RPCAcceptStatus::GARBAGE_ARGS, reply_data);
        return true;
    }

    // Get directory path and construct file path
    std::string dir_path = server_.translate_handle_to_path(dir_handle.handle);
    std::string file_path = dir_path + "/" + name;

    // Get file attributes
    struct stat st;
    if (stat(file_path.c_str(), &st) != 0) {
        // Send NOENT error
        XDREncoder encoder;
        encoder.encode(call_header.xid);
        encoder.encode(static_cast<uint32_t>(RPCMessageType::REPLY));
        encoder.encode(static_cast<uint32_t>(RPCReplyStatus::MSG_ACCEPTED));
        encoder.encode(static_cast<uint32_t>(RPCAcceptStatus::SUCCESS));
        encoder.encode_opaque(std::vector<uint8_t>());  // Empty verifier
        encoder.encode(static_cast<uint32_t>(NFSStatus::NOENT));
        reply_data = encoder.get_buffer();
        return true;
    }

    // Create file handle and encode response
    NFSFileHandle file_handle = server_.create_file_handle(file_path);
    NFSFattr4 attrs;
    attrs.type = server_.get_nfs_type(st.st_mode);
    attrs.mode = st.st_mode & 0777;
    attrs.nlink = st.st_nlink;
    attrs.uid = st.st_uid;
    attrs.gid = st.st_gid;
    attrs.size = st.st_size;
    attrs.used = st.st_blocks * 512;
    attrs.rdev = st.st_rdev;
    attrs.fsid = st.st_dev;
    attrs.fileid = st.st_ino;
    attrs.atime = st.st_atime;
    attrs.mtime = st.st_mtime;
    attrs.ctime = st.st_ctime;

    XDREncoder encoder;
    encoder.encode(call_header.xid);
    encoder.encode(static_cast<uint32_t>(RPCMessageType::REPLY));
    encoder.encode(static_cast<uint32_t>(RPCReplyStatus::MSG_ACCEPTED));
    encoder.encode(static_cast<uint32_t>(RPCAcceptStatus::SUCCESS));
    encoder.encode_opaque(std::vector<uint8_t>());  // Empty verifier
    encoder.encode(static_cast<uint32_t>(NFSStatus::OK));
    encoder.encode_opaque(file_handle.handle);
    encoder.encode(attrs);

    reply_data = encoder.get_buffer();
    return true;
}

bool NFSProcedureHandler::handle_read(const RPCCallHeader& call_header,
                                    const std::vector<uint8_t>& call_data,
                                    size_t& offset,
                                    std::vector<uint8_t>& reply_data) {
    // Decode READ arguments
    XDRDecoder decoder(call_data);
    decoder.set_offset(offset);

    NFSFileHandle file_handle;
    uint64_t read_offset;
    uint32_t count;
    if (!decoder.decode_opaque(file_handle.handle) ||
        !decoder.decode(read_offset) ||
        !decoder.decode(count)) {
        LOG_ERROR("Failed to decode READ arguments");
        send_error_reply(call_header, RPCAcceptStatus::GARBAGE_ARGS, reply_data);
        return true;
    }

    // Get file path and open file
    std::string path = server_.translate_handle_to_path(file_handle.handle);
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        // Send IO error
        XDREncoder encoder;
        encoder.encode(call_header.xid);
        encoder.encode(static_cast<uint32_t>(RPCMessageType::REPLY));
        encoder.encode(static_cast<uint32_t>(RPCReplyStatus::MSG_ACCEPTED));
        encoder.encode(static_cast<uint32_t>(RPCAcceptStatus::SUCCESS));
        encoder.encode_opaque(std::vector<uint8_t>());  // Empty verifier
        encoder.encode(static_cast<uint32_t>(NFSStatus::IO));
        reply_data = encoder.get_buffer();
        return true;
    }

    // Seek to offset
    if (lseek(fd, read_offset, SEEK_SET) < 0) {
        close(fd);
        XDREncoder encoder;
        encoder.encode(call_header.xid);
        encoder.encode(static_cast<uint32_t>(RPCMessageType::REPLY));
        encoder.encode(static_cast<uint32_t>(RPCReplyStatus::MSG_ACCEPTED));
        encoder.encode(static_cast<uint32_t>(RPCAcceptStatus::SUCCESS));
        encoder.encode_opaque(std::vector<uint8_t>());  // Empty verifier
        encoder.encode(static_cast<uint32_t>(NFSStatus::IO));
        reply_data = encoder.get_buffer();
        return true;
    }

    // Read data
    std::vector<uint8_t> data(count);
    ssize_t bytes_read = read(fd, data.data(), count);
    close(fd);

    if (bytes_read < 0) {
        XDREncoder encoder;
        encoder.encode(call_header.xid);
        encoder.encode(static_cast<uint32_t>(RPCMessageType::REPLY));
        encoder.encode(static_cast<uint32_t>(RPCReplyStatus::MSG_ACCEPTED));
        encoder.encode(static_cast<uint32_t>(RPCAcceptStatus::SUCCESS));
        encoder.encode_opaque(std::vector<uint8_t>());  // Empty verifier
        encoder.encode(static_cast<uint32_t>(NFSStatus::IO));
        reply_data = encoder.get_buffer();
        return true;
    }

    // Encode success response
    data.resize(bytes_read);
    XDREncoder encoder;
    encoder.encode(call_header.xid);
    encoder.encode(static_cast<uint32_t>(RPCMessageType::REPLY));
    encoder.encode(static_cast<uint32_t>(RPCReplyStatus::MSG_ACCEPTED));
    encoder.encode(static_cast<uint32_t>(RPCAcceptStatus::SUCCESS));
    encoder.encode_opaque(std::vector<uint8_t>());  // Empty verifier
    encoder.encode(static_cast<uint32_t>(NFSStatus::OK));
    encoder.encode(static_cast<uint32_t>(bytes_read));
    encoder.encode_opaque(data);
    encoder.encode(bytes_read == 0);  // EOF flag

    reply_data = encoder.get_buffer();
    return true;
}

// Add write handler implementation
bool NFSProcedureHandler::handle_write(const RPCCallHeader& call_header,
                                     const std::vector<uint8_t>& call_data,
                                     size_t& offset,
                                     std::vector<uint8_t>& reply_data) {
    // Decode WRITE arguments
    XDRDecoder decoder(call_data);
    decoder.set_offset(offset);

    NFSFileHandle file_handle;
    uint64_t write_offset;
    std::vector<uint8_t> data;
    if (!decoder.decode_opaque(file_handle.handle) ||
        !decoder.decode(write_offset) ||
        !decoder.decode_opaque(data)) {
        LOG_ERROR("Failed to decode WRITE arguments");
        send_error_reply(call_header, RPCAcceptStatus::GARBAGE_ARGS, reply_data);
        return true;
    }

    // Get file path and open file
    std::string path = server_.translate_handle_to_path(file_handle.handle);
    int fd = open(path.c_str(), O_WRONLY);
    if (fd < 0) {
        // Send IO error
        XDREncoder encoder;
        encoder.encode(call_header.xid);
        encoder.encode(static_cast<uint32_t>(RPCMessageType::REPLY));
        encoder.encode(static_cast<uint32_t>(RPCReplyStatus::MSG_ACCEPTED));
        encoder.encode(static_cast<uint32_t>(RPCAcceptStatus::SUCCESS));
        encoder.encode_opaque(std::vector<uint8_t>());  // Empty verifier
        encoder.encode(static_cast<uint32_t>(NFSStatus::IO));
        reply_data = encoder.get_buffer();
        return true;
    }

    // Seek to offset
    if (lseek(fd, write_offset, SEEK_SET) < 0) {
        close(fd);
        XDREncoder encoder;
        encoder.encode(call_header.xid);
        encoder.encode(static_cast<uint32_t>(RPCMessageType::REPLY));
        encoder.encode(static_cast<uint32_t>(RPCReplyStatus::MSG_ACCEPTED));
        encoder.encode(static_cast<uint32_t>(RPCAcceptStatus::SUCCESS));
        encoder.encode_opaque(std::vector<uint8_t>());  // Empty verifier
        encoder.encode(static_cast<uint32_t>(NFSStatus::IO));
        reply_data = encoder.get_buffer();
        return true;
    }

    // Write data
    ssize_t bytes_written = write(fd, data.data(), data.size());
    
    // Sync to disk
    fsync(fd);
    close(fd);

    if (bytes_written < 0) {
        XDREncoder encoder;
        encoder.encode(call_header.xid);
        encoder.encode(static_cast<uint32_t>(RPCMessageType::REPLY));
        encoder.encode(static_cast<uint32_t>(RPCReplyStatus::MSG_ACCEPTED));
        encoder.encode(static_cast<uint32_t>(RPCAcceptStatus::SUCCESS));
        encoder.encode_opaque(std::vector<uint8_t>());  // Empty verifier
        encoder.encode(static_cast<uint32_t>(NFSStatus::IO));
        reply_data = encoder.get_buffer();
        return true;
    }

    // Encode success response
    XDREncoder encoder;
    encoder.encode(call_header.xid);
    encoder.encode(static_cast<uint32_t>(RPCMessageType::REPLY));
    encoder.encode(static_cast<uint32_t>(RPCReplyStatus::MSG_ACCEPTED));
    encoder.encode(static_cast<uint32_t>(RPCAcceptStatus::SUCCESS));
    encoder.encode_opaque(std::vector<uint8_t>());  // Empty verifier
    encoder.encode(static_cast<uint32_t>(NFSStatus::OK));
    encoder.encode(static_cast<uint32_t>(bytes_written));
    encoder.encode(true);  // Committed to stable storage

    reply_data = encoder.get_buffer();
    return true;
}

// Add create handler implementation
bool NFSProcedureHandler::handle_create(const RPCCallHeader& call_header,
                                      const std::vector<uint8_t>& call_data,
                                      size_t& offset,
                                      std::vector<uint8_t>& reply_data) {
    // Decode CREATE arguments
    XDRDecoder decoder(call_data);
    decoder.set_offset(offset);

    NFSFileHandle dir_handle;
    std::string name;
    uint32_t mode;
    if (!decoder.decode_opaque(dir_handle.handle) ||
        !decoder.decode_string(name) ||
        !decoder.decode(mode)) {
        LOG_ERROR("Failed to decode CREATE arguments");
        send_error_reply(call_header, RPCAcceptStatus::GARBAGE_ARGS, reply_data);
        return true;
    }

    // Get directory path and construct file path
    std::string dir_path = server_.translate_handle_to_path(dir_handle.handle);
    std::string file_path = dir_path + "/" + name;

    // Create the file
    int fd = open(file_path.c_str(), O_CREAT | O_WRONLY | O_EXCL, mode);
    if (fd < 0) {
        // Handle specific errors
        XDREncoder encoder;
        encoder.encode(call_header.xid);
        encoder.encode(static_cast<uint32_t>(RPCMessageType::REPLY));
        encoder.encode(static_cast<uint32_t>(RPCReplyStatus::MSG_ACCEPTED));
        encoder.encode(static_cast<uint32_t>(RPCAcceptStatus::SUCCESS));
        encoder.encode_opaque(std::vector<uint8_t>());  // Empty verifier
        
        if (errno == EEXIST) {
            encoder.encode(static_cast<uint32_t>(NFSStatus::EXIST));
        } else {
            encoder.encode(static_cast<uint32_t>(NFSStatus::IO));
        }
        
        reply_data = encoder.get_buffer();
        return true;
    }
    close(fd);

    // Get file attributes
    struct stat st;
    if (stat(file_path.c_str(), &st) != 0) {
        unlink(file_path.c_str());  // Clean up on error
        XDREncoder encoder;
        encoder.encode(call_header.xid);
        encoder.encode(static_cast<uint32_t>(RPCMessageType::REPLY));
        encoder.encode(static_cast<uint32_t>(RPCReplyStatus::MSG_ACCEPTED));
        encoder.encode(static_cast<uint32_t>(RPCAcceptStatus::SUCCESS));
        encoder.encode_opaque(std::vector<uint8_t>());  // Empty verifier
        encoder.encode(static_cast<uint32_t>(NFSStatus::IO));
        reply_data = encoder.get_buffer();
        return true;
    }

    // Create file handle and encode response
    NFSFileHandle file_handle = server_.create_file_handle(file_path);
    NFSFattr4 attrs;
    attrs.type = NFSType::REG;
    attrs.mode = st.st_mode & 0777;
    attrs.nlink = st.st_nlink;
    attrs.uid = st.st_uid;
    attrs.gid = st.st_gid;
    attrs.size = st.st_size;
    attrs.used = st.st_blocks * 512;
    attrs.rdev = st.st_rdev;
    attrs.fsid = st.st_dev;
    attrs.fileid = st.st_ino;
    attrs.atime = st.st_atime;
    attrs.mtime = st.st_mtime;
    attrs.ctime = st.st_ctime;

    XDREncoder encoder;
    encoder.encode(call_header.xid);
    encoder.encode(static_cast<uint32_t>(RPCMessageType::REPLY));
    encoder.encode(static_cast<uint32_t>(RPCReplyStatus::MSG_ACCEPTED));
    encoder.encode(static_cast<uint32_t>(RPCAcceptStatus::SUCCESS));
    encoder.encode_opaque(std::vector<uint8_t>());  // Empty verifier
    encoder.encode(static_cast<uint32_t>(NFSStatus::OK));
    encoder.encode_opaque(file_handle.handle);
    encoder.encode(attrs);

    reply_data = encoder.get_buffer();
    return true;
}

// Add remove handler implementation
bool NFSProcedureHandler::handle_remove(const RPCCallHeader& call_header,
                                      const std::vector<uint8_t>& call_data,
                                      size_t& offset,
                                      std::vector<uint8_t>& reply_data) {
    // Decode REMOVE arguments
    XDRDecoder decoder(call_data);
    decoder.set_offset(offset);

    NFSFileHandle dir_handle;
    std::string name;
    if (!decoder.decode_opaque(dir_handle.handle) ||
        !decoder.decode_string(name)) {
        LOG_ERROR("Failed to decode REMOVE arguments");
        send_error_reply(call_header, RPCAcceptStatus::GARBAGE_ARGS, reply_data);
        return true;
    }

    // Get directory path and construct file path
    std::string dir_path = server_.translate_handle_to_path(dir_handle.handle);
    std::string file_path = dir_path + "/" + name;

    // Check if path exists and get its type
    struct stat st;
    if (stat(file_path.c_str(), &st) != 0) {
        XDREncoder encoder;
        encoder.encode(call_header.xid);
        encoder.encode(static_cast<uint32_t>(RPCMessageType::REPLY));
        encoder.encode(static_cast<uint32_t>(RPCReplyStatus::MSG_ACCEPTED));
        encoder.encode(static_cast<uint32_t>(RPCAcceptStatus::SUCCESS));
        encoder.encode_opaque(std::vector<uint8_t>());  // Empty verifier
        encoder.encode(static_cast<uint32_t>(NFSStatus::NOENT));
        reply_data = encoder.get_buffer();
        return true;
    }

    // Remove the file or directory
    int result;
    if (S_ISDIR(st.st_mode)) {
        result = rmdir(file_path.c_str());
    } else {
        result = unlink(file_path.c_str());
    }

    if (result != 0) {
        XDREncoder encoder;
        encoder.encode(call_header.xid);
        encoder.encode(static_cast<uint32_t>(RPCMessageType::REPLY));
        encoder.encode(static_cast<uint32_t>(RPCReplyStatus::MSG_ACCEPTED));
        encoder.encode(static_cast<uint32_t>(RPCAcceptStatus::SUCCESS));
        encoder.encode_opaque(std::vector<uint8_t>());  // Empty verifier
        
        if (errno == ENOTEMPTY) {
            encoder.encode(static_cast<uint32_t>(NFSStatus::NOTEMPTY));
        } else {
            encoder.encode(static_cast<uint32_t>(NFSStatus::IO));
        }
        
        reply_data = encoder.get_buffer();
        return true;
    }

    // Remove from handle mappings
    server_.remove_handle(file_path);

    // Encode success response
    XDREncoder encoder;
    encoder.encode(call_header.xid);
    encoder.encode(static_cast<uint32_t>(RPCMessageType::REPLY));
    encoder.encode(static_cast<uint32_t>(RPCReplyStatus::MSG_ACCEPTED));
    encoder.encode(static_cast<uint32_t>(RPCAcceptStatus::SUCCESS));
    encoder.encode_opaque(std::vector<uint8_t>());  // Empty verifier
    encoder.encode(static_cast<uint32_t>(NFSStatus::OK));

    reply_data = encoder.get_buffer();
    return true;
}

// RPC handler factory implementation
std::unique_ptr<RPCProcedureHandler> RPCHandlerFactory::create_handler(
    uint32_t program, NFSServer& server) {
    switch (program) {
        case 100003:  // NFS program
            return std::make_unique<NFSProcedureHandler>(server);
        case 100005:  // Mount program
            return std::make_unique<MountProcedureHandler>(server);
        default:
            LOG_ERROR("Unknown RPC program: {}", program);
            return nullptr;
    }
}

} // namespace fuse_t 
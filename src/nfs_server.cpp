#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <functional>
#include <algorithm>
#include <chrono>
#include <unordered_set>
#include "nfs_server.hpp"
#include "util/error.hpp"
#include "util/metrics.hpp"
#include "protocol/state_recovery.hpp"
#include "util/metrics_export.hpp"
#include "util/alert_handler.hpp"
#include "protocol/rpc_message.hpp"
#include "protocol/session.hpp"
#include "protocol/rpc_session.hpp"

namespace fused {

namespace {
    constexpr size_t BUFFER_SIZE = 8192;
    constexpr uint32_t NFS_VERSION = 4;
}

NFSServer::NFSServer() 
    : is_initialized_(false)
    , is_running_(false)
    , sock_(-1)
    , root_path_("/") {
    // Create root handle
    create_file_handle(root_path_);
}

NFSServer::~NFSServer() {
    stop();
    
    // Stop cleanup thread
    cleanup_running_ = false;
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
}

bool NFSServer::initialize() {
    if (is_initialized_) {
        LOG_INFO("NFS server already initialized");
        return true;
    }

    LOG_INFO("Initializing NFS server...");

    // Initialize session manager
    if (!session_manager_.initialize()) {
        LOG_ERROR("Failed to initialize session manager");
        return false;
    }

    // Initialize session-aware RPC handler
    rpc_handler_ = std::make_unique<SessionAwareRPCHandler>(session_manager_);

    // Initialize NFS server socket
    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    CHECK_ERRNO(sock_, "Failed to create socket");

    // Allow socket reuse
    int opt = 1;
    CHECK_ERRNO(
        setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)),
        "Failed to set socket options"
    );

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(2049); // Standard NFS port

    CHECK_ERRNO(
        bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)),
        "Failed to bind socket"
    );

    // Start lock cleanup thread
    cleanup_running_ = true;
    cleanup_thread_ = std::thread(&NFSServer::cleanup_stale_locks, this);

    // Start state cleanup thread
    state_cleanup_thread_ = std::thread([this]() {
        while (is_running_) {
            state_manager_.cleanup_expired_states();
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
    });

    // Initialize metrics exporter with aggregation
    metrics_exporter_ = std::make_unique<MetricsExporter>(
        "/var/log/fused-nfs/metrics",
        MetricsExporter::Format::JSON
    );
    metrics_exporter_->start_realtime_server();

    // Start periodic metrics aggregation
    metrics_aggregation_thread_ = std::thread([this]() {
        while (is_running_) {
            get_metrics_aggregator().add_snapshot(get_recovery_metrics());
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
    });

    // Initialize alert handlers
    alert_handlers_.push_back(AlertHandlerFactory::create_handler("log", ""));
    alert_handlers_.push_back(AlertHandlerFactory::create_handler(
        "email", "admin@example.com"));
    alert_handlers_.push_back(AlertHandlerFactory::create_handler(
        "webhook", "http://monitoring.example.com/webhook"));

    // Register alert handlers
    for (const auto& handler : alert_handlers_) {
        get_alert_manager().add_alert_handler(
            [handler = handler.get()](const Alert& alert) {
                handler->handle(alert);
            });
    }

    is_initialized_ = true;
    LOG_INFO("NFS server initialized successfully");
    return true;
}

bool NFSServer::start() {
    if (!is_initialized_) {
        LOG_ERROR("Cannot start uninitialized NFS server");
        return false;
    }

    if (is_running_) {
        LOG_INFO("NFS server already running");
        return true;
    }

    LOG_INFO("Starting NFS server...");

    // Start listening for connections
    CHECK_ERRNO(listen(sock_, 5), "Failed to listen on socket");

    is_running_ = true;
    
    // Start request handling thread
    std::thread([this]() {
        LOG_INFO("Request handler thread started");
        while (is_running_) {
            handle_request();
        }
        LOG_INFO("Request handler thread stopped");
    }).detach();

    LOG_INFO("NFS server started successfully");
    return true;
}

void NFSServer::stop() {
    LOG_INFO("Stopping NFS server...");
    
    is_running_ = false;
    cleanup_running_ = false;
    
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
    
    if (state_cleanup_thread_.joinable()) {
        state_cleanup_thread_.join();
    }
    
    if (sock_ >= 0) {
        close(sock_);
        sock_ = -1;
    }
    
    // Export final metrics
    if (metrics_exporter_) {
        metrics_exporter_->export_metrics();
    }
    
    if (metrics_aggregation_thread_.joinable()) {
        metrics_aggregation_thread_.join();
    }
    
    LOG_INFO("NFS server stopped");
}

void NFSServer::handle_request() {
    static uint64_t request_count = 0;
    
    // Log metrics every 1000 requests
    if (++request_count % 1000 == 0) {
        get_metrics().log_metrics();
        get_metrics_aggregator().add_snapshot(get_recovery_metrics());
        get_alert_manager().check_metrics(get_metrics_aggregator().get_minute_metrics());
    }
    
    SCOPED_OPERATION("handle_request");
    
    std::vector<uint8_t> buffer(BUFFER_SIZE);
    
    // Accept incoming connection
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int client_sock = accept(sock_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
    
    if (client_sock < 0) {
        LOG_ERROR("Failed to accept connection: {}", 
            std::system_error(errno, std::system_category()).what());
        return;
    }

    LOG_DEBUG("Accepted connection from {}:{}", 
        inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    // Read RPC request
    ssize_t bytes_read = read(client_sock, buffer.data(), buffer.size());
    if (bytes_read <= 0) {
        if (bytes_read < 0) {
            LOG_ERROR("Failed to read request: {}", 
                std::system_error(errno, std::system_category()).what());
        } else {
            LOG_DEBUG("Client closed connection");
        }
        close(client_sock);
        return;
    }

    LOG_DEBUG("Read {} bytes from client", bytes_read);

    // Process RPC call
    std::vector<uint8_t> call_data(buffer.data(), buffer.data() + bytes_read);
    std::vector<uint8_t> reply_data;

    if (!rpc_handler_->process_call(call_data, reply_data)) {
        LOG_ERROR("Failed to process RPC call");
        close(client_sock);
        return;
    }

    // Send reply
    if (!reply_data.empty()) {
        ssize_t bytes_written = write(client_sock, reply_data.data(), reply_data.size());
        if (bytes_written < 0) {
            LOG_ERROR("Failed to send reply: {}", 
                std::system_error(errno, std::system_category()).what());
        } else {
            LOG_DEBUG("Sent {} bytes reply", bytes_written);
        }
    }

    close(client_sock);
}

void NFSServer::handle_null_proc(int client_sock, const NFSHeader& header) {
    SCOPED_OPERATION("null_proc");
    // Send empty response for NULL procedure
    NFSHeader response = header;
    response.type = 1; // Reply
    write(client_sock, &response, sizeof(response));
    OPERATION_SUCCESS();
}

void NFSServer::handle_getattr(int client_sock, const NFSHeader& header, const uint8_t* data) {
    SCOPED_OPERATION("getattr");
    // Parse file handle from request
    const NFSFileHandle* fh = reinterpret_cast<const NFSFileHandle*>(data);
    
    // Prepare response
    struct {
        NFSHeader header;
        NFSStatus status;
        NFSFattr4 attrs;
    } response;

    response.header = header;
    response.header.type = 1; // Reply

    // Get file attributes
    struct stat st;
    std::string path = translate_handle_to_path(fh->handle);
    
    if (stat(path.c_str(), &st) != 0) {
        response.status = NFSStatus::NOENT;
    } else {
        response.status = NFSStatus::OK;
        response.attrs.type = get_nfs_type(st.st_mode);
        response.attrs.mode = st.st_mode & 0777;
        response.attrs.nlink = st.st_nlink;
        response.attrs.uid = st.st_uid;
        response.attrs.gid = st.st_gid;
        response.attrs.size = st.st_size;
        response.attrs.used = st.st_blocks * 512;
        response.attrs.rdev = st.st_rdev;
        response.attrs.fsid = st.st_dev;
        response.attrs.fileid = st.st_ino;
        response.attrs.atime = st.st_atime;
        response.attrs.mtime = st.st_mtime;
        response.attrs.ctime = st.st_ctime;
    }

    // Send response
    write(client_sock, &response, sizeof(response));
    if (response.status == NFSStatus::OK) {
        OPERATION_SUCCESS();
    } else {
        OPERATION_FAILURE();
    }
}

void NFSServer::handle_lookup(int client_sock, const NFSHeader& header, const uint8_t* data) {
    SCOPED_OPERATION("lookup");
    // Parse directory handle and filename
    struct {
        NFSFileHandle dir_handle;
        uint32_t name_len;
        char name[256];
    } __attribute__((packed)) *request = 
        reinterpret_cast<decltype(request)>(const_cast<uint8_t*>(data));

    // Prepare response
    struct {
        NFSHeader header;
        NFSLookupResult result;
    } response;

    response.header = header;
    response.header.type = 1; // Reply

    // Look up the file
    std::string dir_path = translate_handle_to_path(request->dir_handle.handle);
    std::string file_path = dir_path + "/" + 
        std::string(request->name, request->name_len);

    struct stat st;
    if (stat(file_path.c_str(), &st) != 0) {
        response.result.status = NFSStatus::NOENT;
    } else {
        response.result.status = NFSStatus::OK;
        response.result.file_handle = create_file_handle(file_path);
        
        // Fill in attributes
        response.result.attrs.type = get_nfs_type(st.st_mode);
        response.result.attrs.mode = st.st_mode & 0777;
        response.result.attrs.nlink = st.st_nlink;
        response.result.attrs.uid = st.st_uid;
        response.result.attrs.gid = st.st_gid;
        response.result.attrs.size = st.st_size;
        response.result.attrs.used = st.st_blocks * 512;
        response.result.attrs.rdev = st.st_rdev;
        response.result.attrs.fsid = st.st_dev;
        response.result.attrs.fileid = st.st_ino;
        response.result.attrs.atime = st.st_atime;
        response.result.attrs.mtime = st.st_mtime;
        response.result.attrs.ctime = st.st_ctime;
    }

    // Send response
    write(client_sock, &response, sizeof(response));
    if (response.result.status == NFSStatus::OK) {
        OPERATION_SUCCESS();
    } else {
        OPERATION_FAILURE();
    }
}

void NFSServer::handle_read(int client_sock, const NFSHeader& header, const uint8_t* data) {
    SCOPED_OPERATION("read");
    auto start_time = std::chrono::steady_clock::now();
    lock_stats_.read_ops++;

    // Parse read arguments
    const NFSReadArgs* args = reinterpret_cast<const NFSReadArgs*>(data);
    
    // Prepare response
    NFSReadResult result;
    result.status = NFSStatus::OK;
    result.eof = false;
    
    // Get file path
    std::string path = translate_handle_to_path(args->file_handle.handle);
    if (path.empty()) {
        result.status = NFSStatus::BADHANDLE;
        lock_stats_.read_errors++;
        goto send_response;
    }
    
    // Open file
    {
        int fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) {
            result.status = NFSStatus::IO;
            lock_stats_.read_errors++;
            goto send_response;
        }
        
        // Seek to offset
        if (lseek(fd, args->offset, SEEK_SET) < 0) {
            close(fd);
            result.status = NFSStatus::IO;
            lock_stats_.read_errors++;
            goto send_response;
        }
        
        // Read data
        result.data.resize(args->count);
        ssize_t bytes_read = read(fd, result.data.data(), args->count);
        close(fd);
        
        if (bytes_read < 0) {
            result.status = NFSStatus::IO;
            lock_stats_.read_errors++;
            goto send_response;
        }
        
        result.count = bytes_read;
        result.data.resize(bytes_read);
        lock_stats_.bytes_read += bytes_read;

        // Update read timing
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        lock_stats_.total_read_time_ms += duration.count();
    }
    
send_response:
    // Send response header
    NFSHeader response_header = header;
    response_header.type = 1; // Reply
    write(client_sock, &response_header, sizeof(response_header));
    
    // Send result
    write(client_sock, &result.status, sizeof(result.status));
    write(client_sock, &result.count, sizeof(result.count));
    write(client_sock, &result.eof, sizeof(result.eof));
    if (result.count > 0) {
        write(client_sock, result.data.data(), result.count);
    }
    if (result.status == NFSStatus::OK) {
        OPERATION_SUCCESS();
    } else {
        OPERATION_FAILURE();
    }
}

void NFSServer::handle_write(int client_sock, const NFSHeader& header, const uint8_t* data) {
    SCOPED_OPERATION("write");
    auto start_time = std::chrono::steady_clock::now();
    lock_stats_.write_ops++;

    // Parse write arguments
    const NFSWriteArgs* args = reinterpret_cast<const NFSWriteArgs*>(data);
    
    // Prepare response
    NFSWriteResult result;
    result.status = NFSStatus::OK;
    result.count = 0;
    result.committed = false;
    
    // Get file path
    std::string path = translate_handle_to_path(args->file_handle.handle);
    if (path.empty()) {
        result.status = NFSStatus::BADHANDLE;
        lock_stats_.write_errors++;
        goto send_response;
    }
    
    // Open file
    {
        int fd = open(path.c_str(), O_WRONLY);
        if (fd < 0) {
            result.status = NFSStatus::IO;
            lock_stats_.write_errors++;
            goto send_response;
        }
        
        // Seek to offset
        if (lseek(fd, args->offset, SEEK_SET) < 0) {
            close(fd);
            result.status = NFSStatus::IO;
            lock_stats_.write_errors++;
            goto send_response;
        }
        
        // Write data
        ssize_t bytes_written = write(fd, args->data.data(), args->count);
        
        // Sync to disk
        fsync(fd);
        close(fd);
        
        if (bytes_written < 0) {
            result.status = NFSStatus::IO;
            lock_stats_.write_errors++;
            goto send_response;
        }
        
        result.count = bytes_written;
        lock_stats_.bytes_written += bytes_written;

        // Update write timing
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        lock_stats_.total_write_time_ms += duration.count();
    }
    
send_response:
    // Send response
    struct {
        NFSHeader header;
        NFSWriteResult result;
    } response;
    
    response.header = header;
    response.header.type = 1; // Reply
    response.result = result;
    
    write(client_sock, &response, sizeof(response));
    if (result.status == NFSStatus::OK) {
        OPERATION_SUCCESS();
    } else {
        OPERATION_FAILURE();
    }
}

void NFSServer::handle_readdir(int client_sock, const NFSHeader& header, const uint8_t* data) {
    SCOPED_OPERATION("readdir");
    // Initialize all variables at the start
    const NFSReadDirArgs* args = reinterpret_cast<const NFSReadDirArgs*>(data);
    NFSReadDirResult result;
    DIR* dir = nullptr;
    std::string dir_path;
    struct stat st;
    size_t total_size = 0;
    
    // Initialize response
    result.status = NFSStatus::OK;
    result.eof = false;
    
    // Get directory path
    dir_path = translate_handle_to_path(args->dir_handle.handle);
    if (dir_path.empty()) {
        result.status = NFSStatus::BADHANDLE;
        goto send_response;
    }
    
    // Open directory
    dir = opendir(dir_path.c_str());
    if (!dir) {
        result.status = NFSStatus::IO;
        goto send_response;
    }
    
    // Skip to cookie position
    if (args->cookie > 0) {
        seekdir(dir, args->cookie);
    }
    
    // Read directory entries
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
        if (total_size + entry_size > args->count) {
            result.eof = false;
            break;
        }
        
        // Add entry to result
        NFSDirEntry dir_entry;
        dir_entry.fileid = st.st_ino;
        dir_entry.name = entry->d_name;
        
        // Fill in attributes
        dir_entry.attrs.type = get_nfs_type(st.st_mode);
        dir_entry.attrs.mode = st.st_mode & 0777;
        dir_entry.attrs.nlink = st.st_nlink;
        dir_entry.attrs.uid = st.st_uid;
        dir_entry.attrs.gid = st.st_gid;
        dir_entry.attrs.size = st.st_size;
        dir_entry.attrs.used = st.st_blocks * 512;
        dir_entry.attrs.rdev = st.st_rdev;
        dir_entry.attrs.fsid = st.st_dev;
        dir_entry.attrs.fileid = st.st_ino;
        dir_entry.attrs.atime = st.st_atime;
        dir_entry.attrs.mtime = st.st_mtime;
        dir_entry.attrs.ctime = st.st_ctime;
        
        result.entries.push_back(std::move(dir_entry));
        total_size += entry_size;
    }
    
    // Check if we reached the end
    result.eof = (readdir(dir) == nullptr);
    closedir(dir);
    
send_response:
    if (dir) {
        closedir(dir);
    }

    // Send response header
    NFSHeader response_header = header;
    response_header.type = 1; // Reply
    write(client_sock, &response_header, sizeof(response_header));
    
    // Send result status
    write(client_sock, &result.status, sizeof(result.status));
    
    if (result.status == NFSStatus::OK) {
        // Send number of entries
        uint32_t count = result.entries.size();
        write(client_sock, &count, sizeof(count));
        
        // Send EOF flag
        write(client_sock, &result.eof, sizeof(result.eof));
        
        // Send entries
        for (const auto& entry : result.entries) {
            // Send entry data
            write(client_sock, &entry.fileid, sizeof(entry.fileid));
            
            // Send name
            uint32_t name_len = entry.name.length();
            write(client_sock, &name_len, sizeof(name_len));
            write(client_sock, entry.name.c_str(), name_len);
            
            // Send attributes
            write(client_sock, &entry.attrs, sizeof(entry.attrs));
        }
        OPERATION_SUCCESS();
    } else {
        OPERATION_FAILURE();
    }
}

void NFSServer::handle_create(int client_sock, const NFSHeader& header, const uint8_t* data) {
    SCOPED_OPERATION("create");
    // Initialize all variables at the start
    const NFSCreateArgs* args = reinterpret_cast<const NFSCreateArgs*>(data);
    NFSCreateResult result;
    std::string dir_path;
    std::string file_path;
    struct stat st;
    int fd = -1;
    
    // Initialize response
    result.status = NFSStatus::OK;
    
    // Get parent directory path
    dir_path = translate_handle_to_path(args->dir_handle.handle);
    if (dir_path.empty()) {
        result.status = NFSStatus::BADHANDLE;
        goto send_response;
    }
    
    // Create full path for new file
    file_path = dir_path + "/" + args->name;
    
    // Create the file
    {
        fd = open(file_path.c_str(), O_CREAT | O_WRONLY | O_EXCL, args->mode);
        if (fd < 0) {
            if (errno == EEXIST) {
                result.status = NFSStatus::EXIST;
            } else {
                result.status = NFSStatus::IO;
            }
            goto send_response;
        }
        close(fd);
        
        // Get file attributes
        if (stat(file_path.c_str(), &st) != 0) {
            result.status = NFSStatus::IO;
            unlink(file_path.c_str()); // Clean up on error
            goto send_response;
        }
        
        // Create file handle
        result.file_handle = create_file_handle(file_path);
        
        // Fill in attributes
        result.attrs.type = NFSType::REG;
        result.attrs.mode = st.st_mode & 0777;
        result.attrs.nlink = st.st_nlink;
        result.attrs.uid = st.st_uid;
        result.attrs.gid = st.st_gid;
        result.attrs.size = st.st_size;
        result.attrs.used = st.st_blocks * 512;
        result.attrs.rdev = st.st_rdev;
        result.attrs.fsid = st.st_dev;
        result.attrs.fileid = st.st_ino;
        result.attrs.atime = st.st_atime;
        result.attrs.mtime = st.st_mtime;
        result.attrs.ctime = st.st_ctime;
    }
    
send_response:
    // Send response
    NFSHeader response_header = header;
    response_header.type = 1; // Reply
    write(client_sock, &response_header, sizeof(response_header));
    write(client_sock, &result, sizeof(result));
    if (result.status == NFSStatus::OK) {
        OPERATION_SUCCESS();
    } else {
        OPERATION_FAILURE();
    }
}

void NFSServer::handle_remove(int client_sock, const NFSHeader& header, const uint8_t* data) {
    SCOPED_OPERATION("remove");
    // Initialize all variables at the start
    const NFSRemoveArgs* args = reinterpret_cast<const NFSRemoveArgs*>(data);
    NFSRemoveResult result;
    std::string dir_path;
    std::string file_path;
    struct stat st;
    
    // Initialize response
    result.status = NFSStatus::OK;
    
    // Get parent directory path
    dir_path = translate_handle_to_path(args->dir_handle.handle);
    if (dir_path.empty()) {
        result.status = NFSStatus::BADHANDLE;
        goto send_response;
    }
    
    // Create full path for file to remove
    file_path = dir_path + "/" + args->name;
    
    // Check if file exists and get its attributes
    if (stat(file_path.c_str(), &st) != 0) {
        result.status = NFSStatus::NOENT;
        goto send_response;
    }
    
    // Remove the file or directory
    if (S_ISDIR(st.st_mode)) {
        if (rmdir(file_path.c_str()) != 0) {
            if (errno == ENOTEMPTY) {
                result.status = NFSStatus::NOTEMPTY;
            } else {
                result.status = NFSStatus::IO;
            }
        }
    } else {
        if (unlink(file_path.c_str()) != 0) {
            result.status = NFSStatus::IO;
        }
    }
    
    // Remove from handle mappings if successful
    if (result.status == NFSStatus::OK) {
        std::lock_guard<std::mutex> lock(handle_map_mutex_);
        auto it = path_to_handle_.find(file_path);
        if (it != path_to_handle_.end()) {
            std::string handle_str(it->second.begin(), it->second.end());
            handle_to_path_.erase(handle_str);
            path_to_handle_.erase(it);
        }
    }
    
send_response:
    // Send response
    NFSHeader response_header = header;
    response_header.type = 1; // Reply
    write(client_sock, &response_header, sizeof(response_header));
    write(client_sock, &result, sizeof(result));
    if (result.status == NFSStatus::OK) {
        OPERATION_SUCCESS();
    } else {
        OPERATION_FAILURE();
    }
}

void NFSServer::handle_mkdir(int client_sock, const NFSHeader& header, const uint8_t* data) {
    SCOPED_OPERATION("mkdir");
    // Initialize all variables at the start
    const NFSMkdirArgs* args = reinterpret_cast<const NFSMkdirArgs*>(data);
    NFSMkdirResult result;
    std::string parent_path;
    std::string dir_path;
    struct stat st;
    
    // Initialize response
    result.status = NFSStatus::OK;
    
    // Get parent directory path
    parent_path = translate_handle_to_path(args->parent_handle.handle);
    if (parent_path.empty()) {
        result.status = NFSStatus::BADHANDLE;
        goto send_response;
    }
    
    // Create full path for new directory
    dir_path = parent_path + "/" + args->name;
    
    // Create the directory
    if (mkdir(dir_path.c_str(), args->mode) != 0) {
        if (errno == EEXIST) {
            result.status = NFSStatus::EXIST;
        } else {
            result.status = NFSStatus::IO;
        }
        goto send_response;
    }
    
    // Get directory attributes
    if (stat(dir_path.c_str(), &st) != 0) {
        result.status = NFSStatus::IO;
        rmdir(dir_path.c_str()); // Clean up on error
        goto send_response;
    }
    
    // Create directory handle
    result.dir_handle = create_file_handle(dir_path);
    
    // Fill in attributes
    result.attrs.type = NFSType::DIR;
    result.attrs.mode = st.st_mode & 0777;
    result.attrs.nlink = st.st_nlink;
    result.attrs.uid = st.st_uid;
    result.attrs.gid = st.st_gid;
    result.attrs.size = st.st_size;
    result.attrs.used = st.st_blocks * 512;
    result.attrs.rdev = st.st_rdev;
    result.attrs.fsid = st.st_dev;
    result.attrs.fileid = st.st_ino;
    result.attrs.atime = st.st_atime;
    result.attrs.mtime = st.st_mtime;
    result.attrs.ctime = st.st_ctime;
    
send_response:
    // Send response
    NFSHeader response_header = header;
    response_header.type = 1; // Reply
    write(client_sock, &response_header, sizeof(response_header));
    write(client_sock, &result, sizeof(result));
    if (result.status == NFSStatus::OK) {
        OPERATION_SUCCESS();
    } else {
        OPERATION_FAILURE();
    }
}

void NFSServer::handle_rmdir(int client_sock, const NFSHeader& header, const uint8_t* data) {
    SCOPED_OPERATION("rmdir");
    // Initialize all variables at the start
    const NFSRmdirArgs* args = reinterpret_cast<const NFSRmdirArgs*>(data);
    NFSRmdirResult result;
    std::string parent_path;
    std::string dir_path;
    struct stat st;
    
    // Initialize response
    result.status = NFSStatus::OK;
    
    // Get parent directory path
    parent_path = translate_handle_to_path(args->parent_handle.handle);
    if (parent_path.empty()) {
        result.status = NFSStatus::BADHANDLE;
        goto send_response;
    }
    
    // Create full path for directory to remove
    dir_path = parent_path + "/" + args->name;
    
    // Check if directory exists and is actually a directory
    if (stat(dir_path.c_str(), &st) != 0) {
        result.status = NFSStatus::NOENT;
        goto send_response;
    }
    
    if (!S_ISDIR(st.st_mode)) {
        result.status = NFSStatus::NOTDIR;
        goto send_response;
    }
    
    // Remove the directory
    if (rmdir(dir_path.c_str()) != 0) {
        if (errno == ENOTEMPTY) {
            result.status = NFSStatus::NOTEMPTY;
        } else {
            result.status = NFSStatus::IO;
        }
        goto send_response;
    }
    
    // Remove from handle mappings if successful
    {
        std::lock_guard<std::mutex> lock(handle_map_mutex_);
        auto it = path_to_handle_.find(dir_path);
        if (it != path_to_handle_.end()) {
            std::string handle_str(it->second.begin(), it->second.end());
            handle_to_path_.erase(handle_str);
            path_to_handle_.erase(it);
        }
    }
    
send_response:
    // Send response
    NFSHeader response_header = header;
    response_header.type = 1; // Reply
    write(client_sock, &response_header, sizeof(response_header));
    write(client_sock, &result, sizeof(result));
    if (result.status == NFSStatus::OK) {
        OPERATION_SUCCESS();
    } else {
        OPERATION_FAILURE();
    }
}

void NFSServer::handle_rename(int client_sock, const NFSHeader& header, const uint8_t* data) {
    SCOPED_OPERATION("rename");
    // Initialize all variables at the start
    const NFSRenameArgs* args = reinterpret_cast<const NFSRenameArgs*>(data);
    NFSRenameResult result;
    std::string src_dir_path;
    std::string dst_dir_path;
    std::string src_path;
    std::string dst_path;
    struct stat st;
    
    // Initialize response
    result.status = NFSStatus::OK;
    
    // Get source directory path
    src_dir_path = translate_handle_to_path(args->src_dir_handle.handle);
    if (src_dir_path.empty()) {
        result.status = NFSStatus::BADHANDLE;
        goto send_response;
    }
    
    // Get destination directory path
    dst_dir_path = translate_handle_to_path(args->dst_dir_handle.handle);
    if (dst_dir_path.empty()) {
        result.status = NFSStatus::BADHANDLE;
        goto send_response;
    }
    
    // Create full paths
    src_path = src_dir_path + "/" + args->src_name;
    dst_path = dst_dir_path + "/" + args->dst_name;
    
    // Check if source exists
    if (stat(src_path.c_str(), &st) != 0) {
        result.status = NFSStatus::NOENT;
        goto send_response;
    }
    
    // Check if source and destination are on the same filesystem
    {
        struct stat dst_dir_st;
        if (stat(dst_dir_path.c_str(), &dst_dir_st) != 0) {
            result.status = NFSStatus::IO;
            goto send_response;
        }
        
        if (st.st_dev != dst_dir_st.st_dev) {
            result.status = NFSStatus::XDEV;
            goto send_response;
        }
    }
    
    // Perform the rename
    if (rename(src_path.c_str(), dst_path.c_str()) != 0) {
        switch (errno) {
            case EEXIST:
                result.status = NFSStatus::EXIST;
                break;
            case ENOTEMPTY:
                result.status = NFSStatus::NOTEMPTY;
                break;
            case EACCES:
                result.status = NFSStatus::ACCESS;
                break;
            default:
                result.status = NFSStatus::IO;
                break;
        }
        goto send_response;
    }
    
    // Update handle mappings
    {
        std::lock_guard<std::mutex> lock(handle_map_mutex_);
        
        // Find the handle for the source path
        auto src_it = path_to_handle_.find(src_path);
        if (src_it != path_to_handle_.end()) {
            // Create handle string for lookup
            std::string handle_str(src_it->second.begin(), src_it->second.end());
            
            // Update the path mapping
            handle_to_path_[handle_str] = dst_path;
            
            // Move the handle mapping to the new path
            path_to_handle_[dst_path] = std::move(src_it->second);
            path_to_handle_.erase(src_it);
        }
    }
    
send_response:
    // Send response
    NFSHeader response_header = header;
    response_header.type = 1; // Reply
    write(client_sock, &response_header, sizeof(response_header));
    write(client_sock, &result, sizeof(result));
    if (result.status == NFSStatus::OK) {
        OPERATION_SUCCESS();
    } else {
        OPERATION_FAILURE();
    }
}

void NFSServer::handle_setattr(int client_sock, const NFSHeader& header, const uint8_t* data) {
    SCOPED_OPERATION("setattr");
    // Initialize all variables at the start
    const NFSSetAttrArgs* args = reinterpret_cast<const NFSSetAttrArgs*>(data);
    NFSSetAttrResult result;
    std::string path;
    struct stat st;
    struct timeval times[2];
    
    // Initialize response
    result.status = NFSStatus::OK;
    
    // Get file path
    path = translate_handle_to_path(args->file_handle.handle);
    if (path.empty()) {
        result.status = NFSStatus::BADHANDLE;
        goto send_response;
    }
    
    // Get current attributes
    if (stat(path.c_str(), &st) != 0) {
        result.status = NFSStatus::NOENT;
        goto send_response;
    }
    
    // Apply requested changes
    if (args->attr_mask & FATTR4_MODE) {
        if (chmod(path.c_str(), args->attrs.mode) != 0) {
            result.status = NFSStatus::ACCESS;
            goto send_response;
        }
    }
    
    if (args->attr_mask & (FATTR4_UID | FATTR4_GID)) {
        uid_t uid = (args->attr_mask & FATTR4_UID) ? args->attrs.uid : st.st_uid;
        gid_t gid = (args->attr_mask & FATTR4_GID) ? args->attrs.gid : st.st_gid;
        
        if (chown(path.c_str(), uid, gid) != 0) {
            result.status = NFSStatus::ACCESS;
            goto send_response;
        }
    }
    
    if (args->attr_mask & FATTR4_SIZE) {
        if (truncate(path.c_str(), args->attrs.size) != 0) {
            result.status = NFSStatus::IO;
            goto send_response;
        }
    }
    
    if (args->attr_mask & (FATTR4_ATIME | FATTR4_MTIME)) {
        // Get current times first
        times[0].tv_sec = st.st_atime;
        times[0].tv_usec = 0;
        times[1].tv_sec = st.st_mtime;
        times[1].tv_usec = 0;
        
        // Update requested times
        if (args->attr_mask & FATTR4_ATIME) {
            times[0].tv_sec = args->attrs.atime;
        }
        if (args->attr_mask & FATTR4_MTIME) {
            times[1].tv_sec = args->attrs.mtime;
        }
        
        if (utimes(path.c_str(), times) != 0) {
            result.status = NFSStatus::ACCESS;
            goto send_response;
        }
    }
    
    // Get updated attributes
    if (stat(path.c_str(), &st) != 0) {
        result.status = NFSStatus::IO;
        goto send_response;
    }
    
    // Fill in current attributes
    result.attrs.type = get_nfs_type(st.st_mode);
    result.attrs.mode = st.st_mode & 0777;
    result.attrs.nlink = st.st_nlink;
    result.attrs.uid = st.st_uid;
    result.attrs.gid = st.st_gid;
    result.attrs.size = st.st_size;
    result.attrs.used = st.st_blocks * 512;
    result.attrs.rdev = st.st_rdev;
    result.attrs.fsid = st.st_dev;
    result.attrs.fileid = st.st_ino;
    result.attrs.atime = st.st_atime;
    result.attrs.mtime = st.st_mtime;
    result.attrs.ctime = st.st_ctime;
    
send_response:
    // Send response
    NFSHeader response_header = header;
    response_header.type = 1; // Reply
    write(client_sock, &response_header, sizeof(response_header));
    write(client_sock, &result, sizeof(result));
    if (result.status == NFSStatus::OK) {
        OPERATION_SUCCESS();
    } else {
        OPERATION_FAILURE();
    }
}

void NFSServer::handle_lock(int client_sock, const NFSHeader& header, const uint8_t* data) {
    // Track attempt
    lock_stats_.lock_attempts++;
    auto start_time = std::chrono::steady_clock::now();

    // Initialize all variables at the start
    const NFSLockArgs* args = reinterpret_cast<const NFSLockArgs*>(data);
    NFSLockResult result;
    std::string path;
    
    // Initialize response
    result.status = NFSStatus::OK;
    
    // Get file path
    path = translate_handle_to_path(args->file_handle.handle);
    if (path.empty()) {
        result.status = NFSStatus::BADHANDLE;
        goto send_response;
    }
    
    // Try to acquire the lock
    {
        auto& file_locks = file_locks_[path];
        std::unique_lock<std::mutex> lock(file_locks.mutex);
        
        // Check if this is an upgrade request (READ -> WRITE)
        if (args->type == NFSLockType::WRITE) {
            if (try_upgrade_lock(path, args->offset, args->length, client_sock)) {
                lock_stats_.lock_successes++;
                lock_stats_.upgrades++;
                goto send_response;  // Lock upgraded successfully
            }
        }
        
        // Check if this is a downgrade request (WRITE -> READ)
        if (args->type == NFSLockType::READ) {
            if (try_downgrade_lock(path, args->offset, args->length, client_sock)) {
                lock_stats_.lock_successes++;
                lock_stats_.downgrades++;
                goto send_response;  // Lock downgraded successfully
            }
        }
        
        // Check for deadlocks
        if (check_deadlock(client_sock, path, args->offset, args->length)) {
            remove_waiter(client_sock);
            result.status = NFSStatus::DEADLOCK;
            lock_stats_.deadlocks++;
            lock_stats_.lock_failures++;
            goto send_response;
        }

        // Wait for lock with timeout
        if (args->wait) {
            bool got_lock = file_locks.cond.wait_for(lock, std::chrono::seconds(30), [&]() {
                for (const auto& region : file_locks.regions) {
                    bool overlap = !(args->offset + args->length <= region.offset ||
                                   args->offset >= region.offset + region.length);
                    
                    if (overlap) {
                        if (args->type == NFSLockType::READ && region.type == NFSLockType::READ) {
                            continue;  // Shared read locks don't conflict
                        }
                        return false;  // Lock is still held
                    }
                }
                return true;  // No conflicts
            });
            
            remove_waiter(client_sock);
            
            if (!got_lock) {
                result.status = NFSStatus::LOCKED;
                lock_stats_.timeouts++;
                lock_stats_.lock_failures++;
                goto send_response;
            }
        } else {
            // Check for conflicts without waiting
            for (const auto& region : file_locks.regions) {
                bool overlap = !(args->offset + args->length <= region.offset ||
                               args->offset >= region.offset + region.length);
                
                if (overlap) {
                    if (args->type == NFSLockType::READ && region.type == NFSLockType::READ) {
                        continue;  // Shared read locks don't conflict
                    }
                    result.status = NFSStatus::LOCKED;
                    lock_stats_.lock_failures++;
                    goto send_response;
                }
            }
        }
        
        // No conflicts, add the lock
        LockRegion new_lock{
            args->offset,
            args->length,
            args->type,
            client_sock,
            std::chrono::steady_clock::now()
        };
        
        file_locks.regions.push_back(new_lock);
        lock_stats_.lock_successes++;
        
        // Update wait time statistics
        auto end_time = std::chrono::steady_clock::now();
        auto wait_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        lock_stats_.total_wait_time_ms += wait_time.count();
        
        try_coalesce_locks(path, client_sock);
    }
    
send_response:
    // Send response
    NFSHeader response_header = header;
    response_header.type = 1; // Reply
    write(client_sock, &response_header, sizeof(response_header));
    write(client_sock, &result, sizeof(result));
}

void NFSServer::handle_unlock(int client_sock, const NFSHeader& header, const uint8_t* data) {
    // Initialize all variables at the start
    const NFSUnlockArgs* args = reinterpret_cast<const NFSUnlockArgs*>(data);
    NFSLockResult result;
    std::string path;
    
    // Initialize response
    result.status = NFSStatus::OK;
    
    // Get file path
    path = translate_handle_to_path(args->file_handle.handle);
    if (path.empty()) {
        result.status = NFSStatus::BADHANDLE;
        goto send_response;
    }
    
    // Try to release the lock
    {
        auto it = file_locks_.find(path);
        if (it != file_locks_.end()) {
            auto& file_locks = it->second;
            std::unique_lock<std::mutex> lock(file_locks.mutex);
            
            // Find overlapping lock regions
            for (auto region_it = file_locks.regions.begin(); region_it != file_locks.regions.end();) {
                if (region_it->client_sock == client_sock) {
                    // Check if regions overlap
                    uint64_t region_end = region_it->offset + region_it->length;
                    uint64_t unlock_end = args->offset + args->length;
                    
                    bool overlap = !(args->offset >= region_end || unlock_end <= region_it->offset);
                    
                    if (overlap) {
                        if (args->offset == region_it->offset && args->length == region_it->length) {
                            // Full region unlock
                            region_it = file_locks.regions.erase(region_it);
                        } else {
                            // Partial region unlock - split the region
                            LockRegion region = *region_it;
                            region_it = file_locks.regions.erase(region_it);
                            split_lock_region(path, region, args->offset, args->length);
                        }
                        continue;
                    }
                }
                ++region_it;
            }
            
            // Try to coalesce remaining locks
            try_coalesce_locks(path, client_sock);
            
            file_locks.cond.notify_all();
        }
    }
    
send_response:
    // Send response
    NFSHeader response_header = header;
    response_header.type = 1; // Reply
    write(client_sock, &response_header, sizeof(response_header));
    write(client_sock, &result, sizeof(result));
}

bool NFSServer::translate_operation(const FuseOperation& fuse_op, NFSOperation& nfs_op) {
    // Map FUSE operations to NFS operations
    switch (fuse_op.opcode) {
        case FuseOpcode::LOOKUP:
            nfs_op.procedure = NFSProcedure::LOOKUP;
            break;
        case FuseOpcode::GETATTR:
            nfs_op.procedure = NFSProcedure::GETATTR;
            break;
        // Add more operation mappings
        default:
            return false;
    }
    return true;
}

NFSType NFSServer::get_nfs_type(mode_t mode) {
    if (S_ISREG(mode)) return NFSType::REG;
    if (S_ISDIR(mode)) return NFSType::DIR;
    if (S_ISBLK(mode)) return NFSType::BLK;
    if (S_ISCHR(mode)) return NFSType::CHR;
    if (S_ISLNK(mode)) return NFSType::LNK;
    if (S_ISSOCK(mode)) return NFSType::SOCK;
    if (S_ISFIFO(mode)) return NFSType::FIFO;
    return NFSType::REG;
}

std::string NFSServer::translate_handle_to_path(const std::vector<uint8_t>& handle) {
    std::lock_guard<std::mutex> lock(handle_map_mutex_);
    
    // Convert handle to string for map lookup
    std::string handle_str(handle.begin(), handle.end());
    
    auto it = handle_to_path_.find(handle_str);
    if (it == handle_to_path_.end()) {
        return "";
    }
    
    return it->second;
}

NFSFileHandle NFSServer::create_file_handle(const std::string& path) {
    std::lock_guard<std::mutex> lock(handle_map_mutex_);
    
    // Check if we already have a handle for this path
    auto it = path_to_handle_.find(path);
    if (it != path_to_handle_.end()) {
        NFSFileHandle fh;
        fh.handle = it->second;
        return fh;
    }
    
    // Generate new handle
    NFSFileHandle fh;
    fh.handle = generate_handle(path);
    
    // Store mappings
    std::string handle_str(fh.handle.begin(), fh.handle.end());
    path_to_handle_[path] = fh.handle;
    handle_to_path_[handle_str] = path;
    
    return fh;
}

std::vector<uint8_t> NFSServer::generate_handle(const std::string& path) {
    // Use std::hash instead of SHA-256
    std::hash<std::string> hasher;
    size_t hash = hasher(path);
    
    // Convert hash to bytes
    std::vector<uint8_t> handle;
    handle.reserve(16);
    
    for (size_t i = 0; i < 16; ++i) {
        handle.push_back(static_cast<uint8_t>((hash >> (i * 8)) & 0xFF));
    }
    
    return handle;
}

bool NFSServer::check_deadlock(int client_sock, const std::string& path, uint64_t offset, uint64_t length) {
    std::lock_guard<std::mutex> lock(deadlock_mutex_);
    
    // Keep track of visited clients to detect cycles
    std::unordered_set<int> visited;
    std::vector<int> stack;
    
    // Start with current client
    stack.push_back(client_sock);
    
    while (!stack.empty()) {
        int current = stack.back();
        
        if (visited.find(current) != visited.end()) {
            // We found a cycle - this is a deadlock
            return true;
        }
        
        visited.insert(current);
        
        // Find who holds the locks this client is waiting for
        auto& file_locks = file_locks_[path];
        std::lock_guard<std::mutex> lock(file_locks.mutex);
        
        for (const auto& region : file_locks.regions) {
            bool overlap = !(offset + length <= region.offset ||
                           offset >= region.offset + region.length);
            
            if (overlap) {
                // Add lock holder to stack if not already visited
                if (visited.find(region.client_sock) == visited.end()) {
                    stack.push_back(region.client_sock);
                }
            }
        }
        
        // Check if any of these clients are waiting for locks
        auto it = waiting_for_.find(current);
        if (it != waiting_for_.end()) {
            for (const auto& waiter : it->second) {
                auto& other_locks = file_locks_[waiter.path];
                std::lock_guard<std::mutex> lock(other_locks.mutex);
                
                for (const auto& region : other_locks.regions) {
                    bool overlap = !(waiter.offset + waiter.length <= region.offset ||
                                   waiter.offset >= region.offset + region.length);
                    
                    if (overlap) {
                        // Add lock holder to stack if not already visited
                        if (visited.find(region.client_sock) == visited.end()) {
                            stack.push_back(region.client_sock);
                        }
                    }
                }
            }
        }
        
        stack.pop_back();
    }
    
    return false;
}

void NFSServer::remove_waiter(int client_sock) {
    std::lock_guard<std::mutex> lock(deadlock_mutex_);
    waiting_for_.erase(client_sock);
}

bool NFSServer::try_upgrade_lock(const std::string& path, uint64_t offset, uint64_t length, int client_sock) {
    auto& file_locks = file_locks_[path];
    
    // Find existing read lock for this client
    auto it = std::find_if(file_locks.regions.begin(), file_locks.regions.end(),
        [&](const LockRegion& region) {
            return region.offset == offset &&
                   region.length == length &&
                   region.client_sock == client_sock &&
                   region.type == NFSLockType::READ;
        });
    
    if (it == file_locks.regions.end()) {
        return false;  // No read lock found to upgrade
    }
    
    // Check if there are other read locks in the region
    for (const auto& region : file_locks.regions) {
        if (region.client_sock != client_sock) {
            bool overlap = !(offset + length <= region.offset ||
                           offset >= region.offset + region.length);
            if (overlap) {
                return false;  // Can't upgrade if others hold read locks
            }
        }
    }
    
    // Upgrade to write lock
    it->type = NFSLockType::WRITE;
    it->timestamp = std::chrono::steady_clock::now();  // Refresh timestamp
    return true;
}

bool NFSServer::try_downgrade_lock(const std::string& path, uint64_t offset, uint64_t length, int client_sock) {
    auto& file_locks = file_locks_[path];
    
    // Find existing write lock for this client
    auto it = std::find_if(file_locks.regions.begin(), file_locks.regions.end(),
        [&](const LockRegion& region) {
            return region.offset == offset &&
                   region.length == length &&
                   region.client_sock == client_sock &&
                   region.type == NFSLockType::WRITE;
        });
    
    if (it == file_locks.regions.end()) {
        return false;  // No write lock found to downgrade
    }
    
    // Downgrade to read lock
    it->type = NFSLockType::READ;
    it->timestamp = std::chrono::steady_clock::now();  // Refresh timestamp
    file_locks.cond.notify_all();  // Wake up waiters that might be able to acquire read locks
    return true;
}

bool NFSServer::can_merge_locks(const LockRegion& a, const LockRegion& b) {
    // Can only merge locks from same client with same type
    if (a.client_sock != b.client_sock || a.type != b.type) {
        return false;
    }
    
    // Check if regions are adjacent or overlapping
    if (a.offset + a.length < b.offset || b.offset + b.length < a.offset) {
        // Regions don't touch
        return false;
    }
    
    return true;
}

void NFSServer::try_coalesce_locks(const std::string& path, int client_sock) {
    auto& file_locks = file_locks_[path];
    bool merged;
    
    do {
        merged = false;
        
        // Sort regions by offset for easier merging
        std::sort(file_locks.regions.begin(), file_locks.regions.end(),
            [](const LockRegion& a, const LockRegion& b) {
                return a.offset < b.offset;
            });
        
        // Try to merge adjacent regions
        for (size_t i = 0; i < file_locks.regions.size() - 1; ++i) {
            if (can_merge_locks(file_locks.regions[i], file_locks.regions[i + 1])) {
                // Merge the regions
                LockRegion& a = file_locks.regions[i];
                const LockRegion& b = file_locks.regions[i + 1];
                
                // New region spans both original regions
                a.length = std::max(a.offset + a.length, b.offset + b.length) - a.offset;
                
                // Remove the second region
                file_locks.regions.erase(file_locks.regions.begin() + i + 1);
                
                merged = true;
                break;
            }
        }
    } while (merged); // Keep trying until no more merges are possible
}

void NFSServer::split_lock_region(const std::string& path, const LockRegion& region, uint64_t split_offset, uint64_t split_length) {
    auto& file_locks = file_locks_[path];
    
    // Calculate the boundaries of the region to be removed
    uint64_t split_end = split_offset + split_length;
    uint64_t region_end = region.offset + region.length;
    
    // Create up to two new regions depending on the split position
    if (split_offset > region.offset) {
        // Create left region
        LockRegion left{
            region.offset,
            split_offset - region.offset,
            region.type,
            region.client_sock
        };
        file_locks.regions.push_back(left);
    }
    
    if (split_end < region_end) {
        // Create right region
        LockRegion right{
            split_end,
            region_end - split_end,
            region.type,
            region.client_sock
        };
        file_locks.regions.push_back(right);
    }
}

void NFSServer::cleanup_stale_locks() {
    while (cleanup_running_) {
        auto now = std::chrono::steady_clock::now();
        
        // Check all files
        for (auto& [path, file_locks] : file_locks_) {
            std::unique_lock<std::mutex> lock(file_locks.mutex);
            
            // Remove expired locks
            auto it = file_locks.regions.begin();
            while (it != file_locks.regions.end()) {
                if (now - it->timestamp > LOCK_TIMEOUT) {
                    // Lock has expired
                    it = file_locks.regions.erase(it);
                    file_locks.cond.notify_all();  // Wake up waiters
                } else {
                    ++it;
                }
            }
        }
        
        // Sleep for a while before next cleanup
        std::this_thread::sleep_for(std::chrono::minutes(1));
    }
}

// Update the implementation of get_lock_stats
void NFSServer::get_lock_stats(LockStats& stats) const {
    // Copy each atomic field individually
    stats.lock_attempts = lock_stats_.lock_attempts.load();
    stats.lock_successes = lock_stats_.lock_successes.load();
    stats.lock_failures = lock_stats_.lock_failures.load();
    stats.deadlocks = lock_stats_.deadlocks.load();
    stats.timeouts = lock_stats_.timeouts.load();
    stats.upgrades = lock_stats_.upgrades.load();
    stats.downgrades = lock_stats_.downgrades.load();
    stats.total_wait_time_ms = lock_stats_.total_wait_time_ms.load();
}

bool NFSServer::register_client(const std::string& client_id, const std::vector<uint8_t>& verifier) {
    return state_manager_.register_client(client_id, verifier);
}

bool NFSServer::confirm_client(const std::string& client_id) {
    return state_manager_.confirm_client(client_id);
}

bool NFSServer::renew_client_lease(const std::string& client_id) {
    return state_manager_.renew_lease(client_id);
}

void NFSServer::remove_client(const std::string& client_id) {
    state_manager_.remove_client(client_id);
}

bool NFSServer::add_state(const std::string& client_id, std::unique_ptr<State> state) {
    return state_manager_.add_state(client_id, std::move(state));
}

bool NFSServer::remove_state(const std::string& client_id, StateType type, uint32_t seqid) {
    return state_manager_.remove_state(client_id, type, seqid);
}

State* NFSServer::find_state(const std::string& client_id, StateType type, uint32_t seqid) {
    return state_manager_.find_state(client_id, type, seqid);
}

void NFSServer::handle_reclaim_complete(int client_sock, const NFSHeader& header, const uint8_t* data) {
    SCOPED_OPERATION("reclaim_complete");
    
    // Parse request
    const NFSReclaimCompleteArgs* args = reinterpret_cast<const NFSReclaimCompleteArgs*>(data);
    
    // Prepare response
    NFSReclaimCompleteResult result;
    result.status = NFSStatus::OK;
    
    // Complete client recovery
    complete_client_recovery(args->client_id);
    
    // Send response
    NFSHeader response_header = header;
    response_header.type = 1; // Reply
    write(client_sock, &response_header, sizeof(response_header));
    write(client_sock, &result, sizeof(result));
}

void NFSServer::handle_setclientid(int client_sock, const NFSHeader& header, const uint8_t* data) {
    SCOPED_OPERATION("setclientid");
    
    // Parse request
    const NFSSetClientIDArgs* args = reinterpret_cast<const NFSSetClientIDArgs*>(data);
    
    // Prepare response
    NFSSetClientIDResult result;
    
    // Check if this is a recovery attempt
    if (verify_recovery_verifier(args->client_id, args->verifier)) {
        LOG_INFO("Client {} attempting recovery", args->client_id);
        start_client_recovery(args->client_id);
        result.status = NFSStatus::OK;
    } else {
        // Normal client registration
        if (state_manager_.register_client(args->client_id, args->verifier)) {
            result.status = NFSStatus::OK;
        } else {
            result.status = NFSStatus::CLID_INUSE;
        }
    }
    
    // Send response
    NFSHeader response_header = header;
    response_header.type = 1; // Reply
    write(client_sock, &response_header, sizeof(response_header));
    write(client_sock, &result, sizeof(result));
}

void NFSServer::handle_setclientid_confirm(int client_sock, const NFSHeader& header, const uint8_t* data) {
    SCOPED_OPERATION("setclientid_confirm");
    
    // Parse request
    const NFSSetClientIDConfirmArgs* args = 
        reinterpret_cast<const NFSSetClientIDConfirmArgs*>(data);
    
    // Prepare response
    NFSSetClientIDConfirmResult result;
    
    // Confirm client
    if (state_manager_.confirm_client(args->client_id)) {
        result.status = NFSStatus::OK;
    } else {
        result.status = NFSStatus::CLID_INUSE;
    }
    
    // Send response
    NFSHeader response_header = header;
    response_header.type = 1; // Reply
    write(client_sock, &response_header, sizeof(response_header));
    write(client_sock, &result, sizeof(result));
}

bool NFSServer::verify_recovery_verifier(
    const std::string& client_id, const std::vector<uint8_t>& verifier) {
    return state_manager_.verify_client_recovery(client_id, verifier);
}

void NFSServer::start_client_recovery(const std::string& client_id) {
    LOG_INFO("Starting recovery for client: {}", client_id);
    state_manager_.start_client_recovery(client_id);
}

void NFSServer::complete_client_recovery(const std::string& client_id) {
    LOG_INFO("Completing recovery for client: {}", client_id);
    state_manager_.complete_client_recovery(client_id);
}

void NFSServer::handle_create_session(int client_sock, const NFSHeader& header, const uint8_t* data) {
    SCOPED_OPERATION("create_session");
    
    // Parse request
    const NFSCreateSessionArgs* args = reinterpret_cast<const NFSCreateSessionArgs*>(data);
    
    // Prepare response
    NFSCreateSessionResult result;
    result.status = NFSStatus::OK;
    
    // Create session
    uint32_t session_id;
    if (!session_manager_.create_session(args->client_id, session_id)) {
        result.status = NFSStatus::SERVERFAULT;
        goto send_response;
    }
    
    result.session_id = session_id;
    
send_response:
    // Send response
    NFSHeader response_header = header;
    response_header.type = 1; // Reply
    write(client_sock, &response_header, sizeof(response_header));
    write(client_sock, &result, sizeof(result));
}

void NFSServer::handle_destroy_session(int client_sock, const NFSHeader& header, const uint8_t* data) {
    SCOPED_OPERATION("destroy_session");
    
    // Parse request
    const NFSDestroySessionArgs* args = reinterpret_cast<const NFSDestroySessionArgs*>(data);
    
    // Prepare response
    NFSDestroySessionResult result;
    result.status = NFSStatus::OK;
    
    // Destroy session
    if (!session_manager_.destroy_session(args->session_id)) {
        result.status = NFSStatus::STALE_CLIENTID;
    }
    
    // Send response
    NFSHeader response_header = header;
    response_header.type = 1; // Reply
    write(client_sock, &response_header, sizeof(response_header));
    write(client_sock, &result, sizeof(result));
}

void NFSServer::handle_sequence(int client_sock, const NFSHeader& header, const uint8_t* data) {
    SCOPED_OPERATION("sequence");
    
    // Parse request
    const NFSSequenceArgs* args = reinterpret_cast<const NFSSequenceArgs*>(data);
    
    // Prepare response
    NFSSequenceResult result;
    result.status = NFSStatus::OK;
    
    // Validate and update sequence
    if (!session_manager_.check_sequence(args->session_id, args->sequence_id)) {
        result.status = NFSStatus::SEQ_MISORDERED;
        goto send_response;
    }
    
    session_manager_.update_sequence(args->session_id, args->sequence_id);
    
send_response:
    // Send response
    NFSHeader response_header = header;
    response_header.type = 1; // Reply
    write(client_sock, &response_header, sizeof(response_header));
    write(client_sock, &result, sizeof(result));
}

} // namespace fuse_t 
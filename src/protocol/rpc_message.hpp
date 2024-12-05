#pragma once
#include <vector>
#include "rpc_auth.hpp"

namespace fused {

// RPC message types
enum class RPCMessageType {
    CALL = 0,
    REPLY = 1
};

// RPC call header
struct RPCCallHeader {
    uint32_t xid;
    RPCMessageType type;
    uint32_t rpc_version;
    uint32_t program;
    uint32_t version;
    uint32_t procedure;
    RPCAuthHeader auth;
    uint32_t session_id;
    std::vector<uint8_t> sequence_id;
};

// RPC reply status
enum class RPCReplyStatus {
    MSG_ACCEPTED = 0,
    MSG_DENIED = 1
};

// RPC accept status
enum class RPCAcceptStatus {
    SUCCESS = 0,
    PROG_UNAVAIL = 1,
    PROG_MISMATCH = 2,
    PROC_UNAVAIL = 3,
    GARBAGE_ARGS = 4,
    SYSTEM_ERR = 5,
    AUTH_ERROR = 6
};

// RPC reply header
struct RPCReplyHeader {
    uint32_t xid;
    RPCMessageType type;
    RPCReplyStatus reply_status;
    RPCAcceptStatus accept_status;
    std::vector<uint8_t> verifier;
};

// RPC message handler
class RPCMessageHandler {
public:
    RPCMessageHandler();
    ~RPCMessageHandler() = default;

    // Initialize handler
    bool initialize(const std::string& service_name);

    // Process incoming RPC call
    bool process_call(const std::vector<uint8_t>& call_data,
                     std::vector<uint8_t>& reply_data);

private:
    std::unique_ptr<RPCAuthManager> auth_manager_;
    bool initialized_{false};

    // Helper methods
    bool decode_call_header(const std::vector<uint8_t>& data, 
                          size_t& offset,
                          RPCCallHeader& header);
    
    void encode_reply_header(std::vector<uint8_t>& data,
                           const RPCReplyHeader& header);
    
    bool verify_auth(const RPCCallHeader& call_header,
                    std::vector<uint8_t>& reply_verifier);
    
    bool check_operation_auth(const RPCCallHeader& call_header,
                            const std::string& path,
                            uint32_t access_mask);
};

} // namespace fuse_t 
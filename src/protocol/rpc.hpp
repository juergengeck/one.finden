#pragma once
#include <cstdint>
#include "xdr.hpp"

namespace fused {

// RPC message types
enum class RPCMessageType : uint32_t {
    CALL = 0,
    REPLY = 1
};

// RPC reply status
enum class RPCReplyStatus : uint32_t {
    MSG_ACCEPTED = 0,
    MSG_DENIED = 1
};

// RPC authentication status
enum class RPCAuthStatus : uint32_t {
    AUTH_OK = 0,
    AUTH_BADCRED = 1,
    AUTH_REJECTEDCRED = 2,
    AUTH_BADVERF = 3,
    AUTH_REJECTEDVERF = 4,
    AUTH_TOOWEAK = 5,
    AUTH_INVALIDRESP = 6,
    AUTH_FAILED = 7
};

// RPC authentication flavors
enum class RPCAuthFlavor : uint32_t {
    AUTH_NONE = 0,
    AUTH_SYS = 1,
    AUTH_SHORT = 2,
    RPCSEC_GSS = 6
};

struct RPCCredentials {
    RPCAuthFlavor flavor;
    std::vector<uint8_t> body;
    
    void encode(XDREncoder& encoder) const;
    static RPCCredentials decode(XDRDecoder& decoder);
};

struct RPCHeader {
    uint32_t xid;
    RPCMessageType msg_type;
    
    // For CALL messages
    uint32_t rpc_version;
    uint32_t program;
    uint32_t version;
    uint32_t procedure;
    RPCCredentials cred;
    RPCCredentials verf;
    
    // For REPLY messages
    RPCReplyStatus reply_status;
    RPCAuthStatus auth_status;
    
    void encode(XDREncoder& encoder) const;
    static RPCHeader decode(XDRDecoder& decoder);
};

class RPCConnection {
public:
    explicit RPCConnection(int sock);
    
    // Send an RPC message
    bool send_message(const RPCHeader& header, const std::vector<uint8_t>& body);
    
    // Receive an RPC message
    bool receive_message(RPCHeader& header, std::vector<uint8_t>& body);
    
private:
    int sock_;
};

} // namespace fused 
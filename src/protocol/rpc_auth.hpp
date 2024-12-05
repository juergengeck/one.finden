#pragma once
#include <vector>
#include <memory>
#include "auth_gss.hpp"
#include "nfs_protocol.hpp"

namespace fused {

// RPC authentication flavors
enum class RPCAuthFlavor {
    NONE = 0,
    SYS = 1,
    SHORT = 2,
    GSS = 6
};

// RPC authentication header
struct RPCAuthHeader {
    RPCAuthFlavor flavor;
    std::vector<uint8_t> credentials;
    std::vector<uint8_t> verifier;
};

// RPC authentication manager
class RPCAuthManager {
public:
    RPCAuthManager();
    ~RPCAuthManager() = default;

    // Initialize authentication
    bool initialize(const std::string& service_name);

    // Process authentication header
    bool verify_auth(const RPCAuthHeader& auth_header, 
                    std::vector<uint8_t>& response_verifier);

    // Check operation permissions
    bool check_operation_auth(const RPCAuthHeader& auth_header,
                            const std::string& path,
                            uint32_t access_mask);

private:
    std::unique_ptr<GSSAuthenticator> gss_auth_;
    bool initialized_{false};

    // Helper methods
    bool verify_gss_auth(const RPCAuthHeader& auth_header,
                        std::vector<uint8_t>& response_verifier);
    bool verify_sys_auth(const RPCAuthHeader& auth_header);
};

} // namespace fuse_t 
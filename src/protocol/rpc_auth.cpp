#include "rpc_auth.hpp"
#include "util/logger.hpp"

namespace fused {

RPCAuthManager::RPCAuthManager() 
    : gss_auth_(std::make_unique<GSSAuthenticator>()) {
}

bool RPCAuthManager::initialize(const std::string& service_name) {
    if (initialized_) {
        LOG_INFO("RPC auth manager already initialized");
        return true;
    }

    LOG_INFO("Initializing RPC auth manager");

    // Initialize GSS authenticator
    if (!gss_auth_->initialize(service_name)) {
        LOG_ERROR("Failed to initialize GSS authenticator");
        return false;
    }

    initialized_ = true;
    LOG_INFO("RPC auth manager initialized successfully");
    return true;
}

bool RPCAuthManager::verify_auth(const RPCAuthHeader& auth_header,
                               std::vector<uint8_t>& response_verifier) {
    if (!initialized_) {
        LOG_ERROR("RPC auth manager not initialized");
        return false;
    }

    switch (auth_header.flavor) {
        case RPCAuthFlavor::GSS:
            return verify_gss_auth(auth_header, response_verifier);
        case RPCAuthFlavor::SYS:
            return verify_sys_auth(auth_header);
        case RPCAuthFlavor::NONE:
            LOG_WARN("No authentication provided");
            return false;
        default:
            LOG_ERROR("Unsupported auth flavor: {}", 
                static_cast<int>(auth_header.flavor));
            return false;
    }
}

bool RPCAuthManager::check_operation_auth(const RPCAuthHeader& auth_header,
                                        const std::string& path,
                                        uint32_t access_mask) {
    if (!initialized_) {
        LOG_ERROR("RPC auth manager not initialized");
        return false;
    }

    // Convert RPC auth header to credentials
    AuthCredentials creds;
    creds.flavor = static_cast<uint32_t>(auth_header.flavor);
    creds.data = auth_header.credentials;
    creds.mic = auth_header.verifier;

    // Check permissions using appropriate authenticator
    switch (auth_header.flavor) {
        case RPCAuthFlavor::GSS:
            return gss_auth_->check_permissions(creds, path, access_mask);
        case RPCAuthFlavor::SYS:
            // TODO: Implement SYS auth permission checks
            LOG_WARN("SYS auth permission checks not implemented");
            return true;
        default:
            LOG_ERROR("Unsupported auth flavor for permission check: {}", 
                static_cast<int>(auth_header.flavor));
            return false;
    }
}

bool RPCAuthManager::verify_gss_auth(const RPCAuthHeader& auth_header,
                                   std::vector<uint8_t>& response_verifier) {
    AuthCredentials creds;
    creds.flavor = static_cast<uint32_t>(auth_header.flavor);
    creds.data = auth_header.credentials;
    creds.mic = auth_header.verifier;

    if (!gss_auth_->verify_credentials(creds)) {
        LOG_ERROR("GSS credential verification failed");
        return false;
    }

    // Generate response verifier
    if (!gss_auth_->get_mic(auth_header.credentials, response_verifier)) {
        LOG_ERROR("Failed to generate GSS response verifier");
        return false;
    }

    return true;
}

bool RPCAuthManager::verify_sys_auth(const RPCAuthHeader& auth_header) {
    // TODO: Implement SYS auth verification
    LOG_WARN("SYS auth verification not implemented");
    return true;
}

} // namespace fuse_t 
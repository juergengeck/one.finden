#pragma once
#include <string>
#include <vector>
#include <gssapi/gssapi.h>
#include "auth.hpp"
#include "util/logger.hpp"

namespace fused {

class GSSAuthenticator : public Authenticator {
public:
    GSSAuthenticator();
    ~GSSAuthenticator();

    // Initialize GSS context
    bool initialize(const std::string& service_name);

    // Authentication methods
    bool verify_credentials(const AuthCredentials& creds) override;
    bool check_permissions(const AuthCredentials& creds, 
                         const std::string& path, 
                         uint32_t access_mask) override;

    // GSS-specific methods
    bool accept_sec_context(const std::vector<uint8_t>& token,
                          std::vector<uint8_t>& response_token);
    bool verify_mic(const std::vector<uint8_t>& message,
                   const std::vector<uint8_t>& mic);
    bool get_mic(const std::vector<uint8_t>& message,
                std::vector<uint8_t>& mic);

private:
    gss_ctx_id_t context_{GSS_C_NO_CONTEXT};
    gss_cred_id_t credentials_{GSS_C_NO_CREDENTIAL};
    gss_name_t client_name_{GSS_C_NO_NAME};
    bool initialized_{false};

    // Helper methods
    bool acquire_credentials(const std::string& service_name);
    void cleanup_context();
    std::string get_gss_error(OM_uint32 major, OM_uint32 minor);
};

} // namespace fuse_t 
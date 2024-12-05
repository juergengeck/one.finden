#include "auth_gss.hpp"
#include <sstream>

namespace fused {

GSSAuthenticator::GSSAuthenticator() = default;

GSSAuthenticator::~GSSAuthenticator() {
    cleanup_context();
}

bool GSSAuthenticator::initialize(const std::string& service_name) {
    if (initialized_) {
        LOG_INFO("GSS authenticator already initialized");
        return true;
    }

    LOG_INFO("Initializing GSS authenticator with service: {}", service_name);

    if (!acquire_credentials(service_name)) {
        LOG_ERROR("Failed to acquire GSS credentials");
        return false;
    }

    initialized_ = true;
    LOG_INFO("GSS authenticator initialized successfully");
    return true;
}

bool GSSAuthenticator::verify_credentials(const AuthCredentials& creds) {
    if (!initialized_) {
        LOG_ERROR("GSS authenticator not initialized");
        return false;
    }

    // Check if credentials are GSS type
    if (creds.flavor != AUTH_GSS) {
        LOG_ERROR("Invalid credential flavor: {}", creds.flavor);
        return false;
    }

    // Verify GSS token
    std::vector<uint8_t> response_token;
    if (!accept_sec_context(creds.data, response_token)) {
        LOG_ERROR("Failed to accept GSS security context");
        return false;
    }

    // Verify MIC if present
    if (!creds.mic.empty()) {
        if (!verify_mic(creds.data, creds.mic)) {
            LOG_ERROR("Failed to verify GSS MIC");
            return false;
        }
    }

    return true;
}

bool GSSAuthenticator::check_permissions(const AuthCredentials& creds,
                                       const std::string& path,
                                       uint32_t access_mask) {
    if (!initialized_ || !context_) {
        LOG_ERROR("GSS context not established");
        return false;
    }

    // Get client principal name
    gss_buffer_desc name_buffer{};
    OM_uint32 major_status, minor_status;
    
    major_status = gss_display_name(&minor_status, client_name_, &name_buffer, nullptr);
    if (major_status != GSS_S_COMPLETE) {
        LOG_ERROR("Failed to get client name: {}", 
            get_gss_error(major_status, minor_status));
        return false;
    }

    std::string principal(static_cast<char*>(name_buffer.value), name_buffer.length);
    gss_release_buffer(&minor_status, &name_buffer);

    // Check access based on principal and requested permissions
    LOG_DEBUG("Checking permissions for principal {} on path {}", principal, path);
    
    // TODO: Implement proper access control based on principal
    // For now, just log and allow
    LOG_INFO("Allowing access for principal {} to {} with mask 0x{:x}", 
        principal, path, access_mask);
    
    return true;
}

bool GSSAuthenticator::accept_sec_context(const std::vector<uint8_t>& token,
                                        std::vector<uint8_t>& response_token) {
    if (!initialized_) {
        LOG_ERROR("GSS authenticator not initialized");
        return false;
    }

    gss_buffer_desc input_token{token.size(), const_cast<void*>(static_cast<const void*>(token.data()))};
    gss_buffer_desc output_token{0, nullptr};
    
    OM_uint32 major_status, minor_status;
    gss_name_t src_name;
    gss_OID mech_type;
    OM_uint32 ret_flags;
    OM_uint32 time_rec;

    major_status = gss_accept_sec_context(
        &minor_status,
        &context_,
        credentials_,
        &input_token,
        GSS_C_NO_CHANNEL_BINDINGS,
        &src_name,
        &mech_type,
        &output_token,
        &ret_flags,
        &time_rec,
        nullptr
    );

    if (major_status != GSS_S_COMPLETE && major_status != GSS_S_CONTINUE_NEEDED) {
        LOG_ERROR("Failed to accept security context: {}", 
            get_gss_error(major_status, minor_status));
        return false;
    }

    // Store client name
    if (client_name_ != GSS_C_NO_NAME) {
        gss_release_name(&minor_status, &client_name_);
    }
    client_name_ = src_name;

    // Copy output token if present
    if (output_token.length > 0) {
        response_token.assign(
            static_cast<uint8_t*>(output_token.value),
            static_cast<uint8_t*>(output_token.value) + output_token.length
        );
        gss_release_buffer(&minor_status, &output_token);
    }

    return true;
}

bool GSSAuthenticator::verify_mic(const std::vector<uint8_t>& message,
                                const std::vector<uint8_t>& mic) {
    if (!context_) {
        LOG_ERROR("No security context established");
        return false;
    }

    gss_buffer_desc msg_buffer{message.size(), const_cast<void*>(static_cast<const void*>(message.data()))};
    gss_buffer_desc mic_buffer{mic.size(), const_cast<void*>(static_cast<const void*>(mic.data()))};
    
    OM_uint32 major_status, minor_status;
    gss_qop_t qop_state;

    major_status = gss_verify_mic(
        &minor_status,
        context_,
        &msg_buffer,
        &mic_buffer,
        &qop_state
    );

    if (major_status != GSS_S_COMPLETE) {
        LOG_ERROR("Failed to verify MIC: {}", 
            get_gss_error(major_status, minor_status));
        return false;
    }

    return true;
}

bool GSSAuthenticator::get_mic(const std::vector<uint8_t>& message,
                             std::vector<uint8_t>& mic) {
    if (!context_) {
        LOG_ERROR("No security context established");
        return false;
    }

    gss_buffer_desc msg_buffer{message.size(), const_cast<void*>(static_cast<const void*>(message.data()))};
    gss_buffer_desc mic_buffer;
    
    OM_uint32 major_status, minor_status;

    major_status = gss_get_mic(
        &minor_status,
        context_,
        GSS_C_QOP_DEFAULT,
        &msg_buffer,
        &mic_buffer
    );

    if (major_status != GSS_S_COMPLETE) {
        LOG_ERROR("Failed to generate MIC: {}", 
            get_gss_error(major_status, minor_status));
        return false;
    }

    // Copy MIC
    mic.assign(
        static_cast<uint8_t*>(mic_buffer.value),
        static_cast<uint8_t*>(mic_buffer.value) + mic_buffer.length
    );
    gss_release_buffer(&minor_status, &mic_buffer);

    return true;
}

bool GSSAuthenticator::acquire_credentials(const std::string& service_name) {
    OM_uint32 major_status, minor_status;
    gss_name_t server_name;
    gss_buffer_desc name_buffer{service_name.size(), const_cast<char*>(service_name.data())};

    // Convert service name to internal format
    major_status = gss_import_name(
        &minor_status,
        &name_buffer,
        GSS_C_NT_HOSTBASED_SERVICE,
        &server_name
    );

    if (major_status != GSS_S_COMPLETE) {
        LOG_ERROR("Failed to import service name: {}", 
            get_gss_error(major_status, minor_status));
        return false;
    }

    // Acquire credentials
    major_status = gss_acquire_cred(
        &minor_status,
        server_name,
        GSS_C_INDEFINITE,
        GSS_C_NO_OID_SET,
        GSS_C_ACCEPT,
        &credentials_,
        nullptr,
        nullptr
    );

    gss_release_name(&minor_status, &server_name);

    if (major_status != GSS_S_COMPLETE) {
        LOG_ERROR("Failed to acquire credentials: {}", 
            get_gss_error(major_status, minor_status));
        return false;
    }

    return true;
}

void GSSAuthenticator::cleanup_context() {
    OM_uint32 minor_status;

    if (context_ != GSS_C_NO_CONTEXT) {
        gss_delete_sec_context(&minor_status, &context_, GSS_C_NO_BUFFER);
        context_ = GSS_C_NO_CONTEXT;
    }

    if (credentials_ != GSS_C_NO_CREDENTIAL) {
        gss_release_cred(&minor_status, &credentials_);
        credentials_ = GSS_C_NO_CREDENTIAL;
    }

    if (client_name_ != GSS_C_NO_NAME) {
        gss_release_name(&minor_status, &client_name_);
        client_name_ = GSS_C_NO_NAME;
    }

    initialized_ = false;
}

std::string GSSAuthenticator::get_gss_error(OM_uint32 major, OM_uint32 minor) {
    OM_uint32 msg_ctx = 0;
    OM_uint32 min_status;
    gss_buffer_desc status_string;
    std::stringstream error_msg;

    // Get major status message
    gss_display_status(
        &min_status,
        major,
        GSS_C_GSS_CODE,
        GSS_C_NO_OID,
        &msg_ctx,
        &status_string
    );
    error_msg << static_cast<char*>(status_string.value);
    gss_release_buffer(&min_status, &status_string);

    // Get minor status message
    msg_ctx = 0;
    gss_display_status(
        &min_status,
        minor,
        GSS_C_MECH_CODE,
        GSS_C_NO_OID,
        &msg_ctx,
        &status_string
    );
    error_msg << " (" << static_cast<char*>(status_string.value) << ")";
    gss_release_buffer(&min_status, &status_string);

    return error_msg.str();
}

} // namespace fuse_t 
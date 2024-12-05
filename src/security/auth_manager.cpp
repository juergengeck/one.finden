#include "auth_manager.hpp"
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/err.h>
#include <cstring>

namespace fused {

AuthenticationManager::AuthenticationManager() {
    // Initialize OpenSSL
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();
}

AuthenticationManager::~AuthenticationManager() {
    // Cleanup OpenSSL
    EVP_cleanup();
    ERR_free_strings();

    // Free certificates
    for (auto& [serial, cert] : certificates_) {
        X509_free(cert);
    }
}

bool AuthenticationManager::initialize() {
    LOG_INFO("Initializing authentication manager");
    
    // Initialize secure random
    if (RAND_load_file("/dev/urandom", 32) != 32) {
        LOG_ERROR("Failed to initialize secure random");
        return false;
    }

    return true;
}

bool AuthenticationManager::add_user(const std::string& username, 
                                   const std::string& password) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (users_.find(username) != users_.end()) {
        LOG_ERROR("User already exists: {}", username);
        return false;
    }

    // Validate password
    if (!validate_password_strength(password)) {
        LOG_ERROR("Password too weak for user: {}", username);
        return false;
    }

    // Check if password is compromised
    if (is_password_compromised(password)) {
        LOG_ERROR("Password compromised for user: {}", username);
        return false;
    }

    // Create credentials
    AuthCredentials creds{
        username,
        hash_password(password),
        {},  // no groups
        std::chrono::system_clock::now() + std::chrono::days(90),  // 90 day expiry
        AuthMethod::SIMPLE,
        true
    };

    users_[username] = std::move(creds);
    LOG_INFO("Added user: {}", username);
    return true;
}

bool AuthenticationManager::remove_user(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = users_.find(username);
    if (it == users_.end()) {
        return false;
    }

    // Revoke all tokens for this user
    for (auto& [token, auth_token] : tokens_) {
        if (auth_token.username == username) {
            auth_token.revoked = true;
        }
    }

    users_.erase(it);
    LOG_INFO("Removed user: {}", username);
    return true;
}

bool AuthenticationManager::update_user(const std::string& username,
                                      const AuthCredentials& creds) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = users_.find(username);
    if (it == users_.end()) {
        return false;
    }

    it->second = creds;
    LOG_INFO("Updated user: {}", username);
    return true;
}

bool AuthenticationManager::disable_user(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = users_.find(username);
    if (it == users_.end()) {
        return false;
    }

    it->second.enabled = false;
    LOG_INFO("Disabled user: {}", username);
    return true;
}

bool AuthenticationManager::authenticate(const std::string& username,
                                      const std::string& password) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = users_.find(username);
    if (it == users_.end()) {
        log_auth_attempt(username, false);
        return false;
    }

    const auto& creds = it->second;

    // Check if user is enabled and not expired
    if (!creds.enabled || 
        std::chrono::system_clock::now() >= creds.expiry) {
        log_auth_attempt(username, false);
        return false;
    }

    // Verify password
    bool success = verify_password(password, creds.password_hash);
    log_auth_attempt(username, success);
    return success;
}

bool AuthenticationManager::verify_token(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = tokens_.find(token);
    if (it == tokens_.end()) {
        return false;
    }

    const auto& auth_token = it->second;

    // Check if token is valid
    if (auth_token.revoked || 
        std::chrono::system_clock::now() >= auth_token.expiry) {
        return false;
    }

    return true;
}

std::string AuthenticationManager::generate_token(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Generate secure token
    std::string token = generate_secure_token();
    if (token.empty()) {
        return "";
    }

    // Create auth token
    AuthToken auth_token{
        token,
        username,
        std::chrono::system_clock::now() + TOKEN_VALIDITY,
        {},  // no scopes
        false
    };

    tokens_[token] = std::move(auth_token);
    cleanup_expired_tokens();
    return token;
}

bool AuthenticationManager::revoke_token(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = tokens_.find(token);
    if (it == tokens_.end()) {
        return false;
    }

    it->second.revoked = true;
    return true;
}

bool AuthenticationManager::refresh_token(const std::string& old_token,
                                       std::string& new_token) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = tokens_.find(old_token);
    if (it == tokens_.end() || it->second.revoked) {
        return false;
    }

    // Generate new token
    new_token = generate_token(it->second.username);
    if (new_token.empty()) {
        return false;
    }

    // Revoke old token
    it->second.revoked = true;
    return true;
}

// Private methods
std::string AuthenticationManager::hash_password(const std::string& password) {
    unsigned char hash[SHA512_DIGEST_LENGTH];
    
    // Generate salt
    unsigned char salt[32];
    if (RAND_bytes(salt, sizeof(salt)) != 1) {
        return "";
    }

    // Hash password with salt
    unsigned char salted[password.length() + sizeof(salt)];
    std::memcpy(salted, password.c_str(), password.length());
    std::memcpy(salted + password.length(), salt, sizeof(salt));

    SHA512_CTX sha512;
    SHA512_Init(&sha512);
    SHA512_Update(&sha512, salted, sizeof(salted));
    SHA512_Final(hash, &sha512);

    // Format as base64
    std::string result;
    result.reserve(SHA512_DIGEST_LENGTH * 2);
    for (unsigned char i : hash) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", i);
        result += buf;
    }

    return result;
}

bool AuthenticationManager::verify_password(const std::string& password,
                                         const std::string& hash) {
    return hash_password(password) == hash;
}

bool AuthenticationManager::validate_password_strength(const std::string& password) {
    if (password.length() < MIN_PASSWORD_LENGTH) {
        return false;
    }

    bool has_upper = false;
    bool has_lower = false;
    bool has_digit = false;
    bool has_special = false;

    for (char c : password) {
        if (std::isupper(c)) has_upper = true;
        else if (std::islower(c)) has_lower = true;
        else if (std::isdigit(c)) has_digit = true;
        else has_special = true;
    }

    return has_upper && has_lower && has_digit && has_special;
}

std::string AuthenticationManager::generate_secure_token() {
    unsigned char random[TOKEN_LENGTH];
    if (RAND_bytes(random, TOKEN_LENGTH) != 1) {
        return "";
    }

    std::string token;
    token.reserve(TOKEN_LENGTH * 2);
    for (unsigned char i : random) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", i);
        token += buf;
    }

    return token;
}

void AuthenticationManager::cleanup_expired_tokens() {
    auto now = std::chrono::system_clock::now();
    
    for (auto it = tokens_.begin(); it != tokens_.end();) {
        if (it->second.revoked || now >= it->second.expiry) {
            it = tokens_.erase(it);
        } else {
            ++it;
        }
    }
}

void AuthenticationManager::log_auth_attempt(const std::string& username,
                                          bool success) {
    if (success) {
        LOG_INFO("Successful authentication for user: {}", username);
    } else {
        LOG_WARN("Failed authentication attempt for user: {}", username);
    }
}

bool AuthenticationManager::add_certificate(X509* cert) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!cert || !validate_certificate(cert)) {
        LOG_ERROR("Invalid certificate provided");
        return false;
    }

    // Get certificate serial number
    ASN1_INTEGER* serial = X509_get_serialNumber(cert);
    BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
    char* serial_str = BN_bn2hex(bn);
    std::string serial_number(serial_str);
    
    OPENSSL_free(serial_str);
    BN_free(bn);

    // Store certificate
    certificates_[serial_number] = X509_dup(cert);
    LOG_INFO("Added certificate with serial: {}", serial_number);
    return true;
}

bool AuthenticationManager::revoke_certificate(const std::string& serial) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = certificates_.find(serial);
    if (it == certificates_.end()) {
        return false;
    }

    X509_free(it->second);
    certificates_.erase(it);
    LOG_INFO("Revoked certificate with serial: {}", serial);
    return true;
}

bool AuthenticationManager::verify_certificate_chain(STACK_OF(X509)* chain) {
    if (!chain || sk_X509_num(chain) == 0) {
        return false;
    }

    // Create certificate store context
    X509_STORE_CTX* ctx = X509_STORE_CTX_new();
    if (!ctx) {
        return false;
    }

    // Create certificate store
    X509_STORE* store = X509_STORE_new();
    if (!store) {
        X509_STORE_CTX_free(ctx);
        return false;
    }

    // Add trusted certificates to store
    for (const auto& [serial, cert] : certificates_) {
        X509_STORE_add_cert(store, cert);
    }

    // Initialize verification context
    X509* leaf = sk_X509_value(chain, 0);
    if (!X509_STORE_CTX_init(ctx, store, leaf, chain)) {
        X509_STORE_free(store);
        X509_STORE_CTX_free(ctx);
        return false;
    }

    // Set verification parameters
    X509_VERIFY_PARAM* param = X509_VERIFY_PARAM_new();
    X509_VERIFY_PARAM_set_flags(param, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
    X509_STORE_CTX_set0_param(ctx, param);

    // Verify certificate chain
    bool result = X509_verify_cert(ctx) == 1;
    
    // Check verification error if failed
    if (!result) {
        int error = X509_STORE_CTX_get_error(ctx);
        LOG_ERROR("Certificate verification failed: {}", 
                 X509_verify_cert_error_string(error));
    }

    // Check for revocation
    if (result) {
        for (int i = 0; i < sk_X509_num(chain); i++) {
            X509* cert = sk_X509_value(chain, i);
            if (check_certificate_revocation(cert)) {
                result = false;
                LOG_WARN("Certificate in chain is revoked");
                break;
            }
        }
    }

    X509_STORE_free(store);
    X509_STORE_CTX_free(ctx);
    return result;
}

bool AuthenticationManager::verify_certificate(X509* cert) {
    if (!cert) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    
    // Validate certificate
    if (!validate_certificate(cert)) {
        return false;
    }

    // Get certificate serial number
    ASN1_INTEGER* serial = X509_get_serialNumber(cert);
    BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
    char* serial_str = BN_bn2hex(bn);
    std::string serial_number(serial_str);
    
    OPENSSL_free(serial_str);
    BN_free(bn);

    auto it = certificates_.find(serial_number);
    if (it == certificates_.end()) {
        LOG_ERROR("Certificate not found in trust store: {}", serial_number);
        return false;
    }

    // Check if certificate is revoked
    if (check_certificate_revocation(cert)) {
        LOG_ERROR("Certificate is revoked: {}", serial_number);
        return false;
    }

    // Verify certificate signature
    EVP_PKEY* pub_key = X509_get_pubkey(it->second);
    int verify_result = X509_verify(cert, pub_key);
    EVP_PKEY_free(pub_key);

    if (verify_result != 1) {
        LOG_ERROR("Certificate signature verification failed: {}", serial_number);
        return false;
    }

    return true;
}

bool AuthenticationManager::validate_certificate(X509* cert) {
    // Check validity period
    if (X509_cmp_current_time(X509_get_notBefore(cert)) >= 0 ||
        X509_cmp_current_time(X509_get_notAfter(cert)) <= 0) {
        LOG_ERROR("Certificate is outside validity period");
        return false;
    }

    // Verify basic constraints
    BASIC_CONSTRAINTS* bs = static_cast<BASIC_CONSTRAINTS*>(
        X509_get_ext_d2i(cert, NID_basic_constraints, nullptr, nullptr));
    
    if (!bs) {
        LOG_ERROR("Certificate missing basic constraints");
        return false;
    }
    BASIC_CONSTRAINTS_free(bs);

    // Verify key usage
    ASN1_BIT_STRING* usage = static_cast<ASN1_BIT_STRING*>(
        X509_get_ext_d2i(cert, NID_key_usage, nullptr, nullptr));
    
    if (!usage) {
        LOG_ERROR("Certificate missing key usage");
        return false;
    }
    ASN1_BIT_STRING_free(usage);

    return true;
}

bool AuthenticationManager::check_certificate_revocation(X509* cert) {
    // Get certificate serial number
    ASN1_INTEGER* serial = X509_get_serialNumber(cert);
    BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
    char* serial_str = BN_bn2hex(bn);
    std::string serial_number(serial_str);
    
    OPENSSL_free(serial_str);
    BN_free(bn);

    // Check if certificate is in our store
    return certificates_.find(serial_number) == certificates_.end();
}

void AuthenticationManager::cleanup_expired_certificates() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::system_clock::now();
    
    for (auto it = certificates_.begin(); it != certificates_.end();) {
        X509* cert = it->second;
        if (X509_cmp_current_time(X509_get_notAfter(cert)) <= 0) {
            LOG_INFO("Removing expired certificate: {}", it->first);
            X509_free(cert);
            it = certificates_.erase(it);
        } else {
            ++it;
        }
    }
}

bool AuthenticationManager::check_permission(const std::string& username,
                                          const std::string& path,
                                          Permission required) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Root always has access if allowed
    if (username == "root") {
        for (const auto& rule : access_rules_) {
            if (match_path_rule(path, rule) && rule.allow_root) {
                return true;
            }
        }
    }

    // Check each applicable rule
    for (const auto& rule : access_rules_) {
        if (match_path_rule(path, rule)) {
            // Check if user has required permissions
            if ((rule.required_permissions & required) == required &&
                user_has_permission(username, rule)) {
                return true;
            }
        }
    }

    LOG_WARN("Permission denied for user {} on path {}", username, path);
    return false;
}

bool AuthenticationManager::add_access_rule(const AccessRule& rule) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check for conflicting rules
    for (const auto& existing : access_rules_) {
        if (existing.path == rule.path) {
            LOG_ERROR("Access rule already exists for path: {}", rule.path);
            return false;
        }
    }

    access_rules_.push_back(rule);
    LOG_INFO("Added access rule for path: {}", rule.path);
    return true;
}

bool AuthenticationManager::remove_access_rule(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = std::find_if(access_rules_.begin(), access_rules_.end(),
                          [&path](const AccessRule& rule) {
                              return rule.path == path;
                          });

    if (it == access_rules_.end()) {
        return false;
    }

    access_rules_.erase(it);
    LOG_INFO("Removed access rule for path: {}", path);
    return true;
}

bool AuthenticationManager::update_access_rule(const std::string& path,
                                            const AccessRule& rule) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = std::find_if(access_rules_.begin(), access_rules_.end(),
                          [&path](const AccessRule& r) {
                              return r.path == path;
                          });

    if (it == access_rules_.end()) {
        return false;
    }

    *it = rule;
    LOG_INFO("Updated access rule for path: {}", path);
    return true;
}

bool AuthenticationManager::add_group(const std::string& group_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (groups_.find(group_name) != groups_.end()) {
        LOG_ERROR("Group already exists: {}", group_name);
        return false;
    }

    groups_[group_name] = {};
    LOG_INFO("Added group: {}", group_name);
    return true;
}

bool AuthenticationManager::remove_group(const std::string& group_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = groups_.find(group_name);
    if (it == groups_.end()) {
        return false;
    }

    groups_.erase(it);
    LOG_INFO("Removed group: {}", group_name);
    return true;
}

bool AuthenticationManager::add_user_to_group(const std::string& username,
                                            const std::string& group) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = groups_.find(group);
    if (it == groups_.end()) {
        LOG_ERROR("Group not found: {}", group);
        return false;
    }

    // Check if user exists
    if (users_.find(username) == users_.end()) {
        LOG_ERROR("User not found: {}", username);
        return false;
    }

    // Add user to group if not already a member
    auto& members = it->second;
    if (std::find(members.begin(), members.end(), username) == members.end()) {
        members.push_back(username);
        LOG_INFO("Added user {} to group {}", username, group);
    }

    return true;
}

bool AuthenticationManager::remove_user_from_group(const std::string& username,
                                                 const std::string& group) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = groups_.find(group);
    if (it == groups_.end()) {
        return false;
    }

    auto& members = it->second;
    auto user_it = std::find(members.begin(), members.end(), username);
    if (user_it == members.end()) {
        return false;
    }

    members.erase(user_it);
    LOG_INFO("Removed user {} from group {}", username, group);
    return true;
}

// Private helper methods
bool AuthenticationManager::match_path_rule(const std::string& path,
                                          const AccessRule& rule) {
    if (rule.recursive) {
        // Check if path starts with rule path
        return path.find(rule.path) == 0;
    } else {
        // Exact path match
        return path == rule.path;
    }
}

bool AuthenticationManager::user_has_permission(const std::string& username,
                                             const AccessRule& rule) {
    // Check direct user permission
    if (std::find(rule.allowed_users.begin(),
                  rule.allowed_users.end(),
                  username) != rule.allowed_users.end()) {
        return true;
    }

    // Check group permissions
    auto user_groups = get_user_groups(username);
    for (const auto& group : rule.allowed_groups) {
        if (std::find(user_groups.begin(),
                     user_groups.end(),
                     group) != user_groups.end()) {
            return true;
        }
    }

    return false;
}

std::vector<std::string> AuthenticationManager::get_user_groups(
    const std::string& username) {
    std::vector<std::string> user_groups;
    
    for (const auto& [group_name, members] : groups_) {
        if (std::find(members.begin(),
                     members.end(),
                     username) != members.end()) {
            user_groups.push_back(group_name);
        }
    }

    return user_groups;
}

} // namespace fuse_t 
#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <unordered_map>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include "util/logger.hpp"

namespace fused {

enum class AuthMethod {
    SIMPLE,     // Username/password
    KERBEROS,   // Kerberos authentication
    CERTIFICATE,// X.509 certificates
    TOKEN      // Bearer tokens
};

enum class Permission {
    NONE = 0,
    READ = 1 << 0,
    WRITE = 1 << 1,
    EXECUTE = 1 << 2,
    DELETE = 1 << 3,
    ADMIN = 1 << 4,
    ALL = READ | WRITE | EXECUTE | DELETE | ADMIN
};

struct AuthCredentials {
    std::string username;
    std::string password_hash;
    std::vector<std::string> groups;
    std::chrono::system_clock::time_point expiry;
    AuthMethod method{AuthMethod::SIMPLE};
    bool enabled{true};
};

struct AuthToken {
    std::string token;
    std::string username;
    std::chrono::system_clock::time_point expiry;
    std::vector<std::string> scopes;
    bool revoked{false};
};

struct AccessRule {
    std::string path;
    Permission required_permissions;
    std::vector<std::string> allowed_users;
    std::vector<std::string> allowed_groups;
    bool allow_root{false};
    bool recursive{false};
};

class AuthenticationManager {
public:
    AuthenticationManager();
    ~AuthenticationManager();

    // Initialize authentication system
    bool initialize();

    // User management
    bool add_user(const std::string& username, const std::string& password);
    bool remove_user(const std::string& username);
    bool update_user(const std::string& username, const AuthCredentials& creds);
    bool disable_user(const std::string& username);

    // Authentication
    bool authenticate(const std::string& username, const std::string& password);
    bool verify_token(const std::string& token);
    bool verify_certificate(X509* cert);

    // Token management
    std::string generate_token(const std::string& username);
    bool revoke_token(const std::string& token);
    bool refresh_token(const std::string& old_token, std::string& new_token);

    // Certificate management
    bool add_certificate(X509* cert);
    bool revoke_certificate(const std::string& serial);
    bool verify_certificate_chain(STACK_OF(X509)* chain);

    // Authorization
    bool check_permission(const std::string& username, 
                         const std::string& path,
                         Permission required);
    bool add_access_rule(const AccessRule& rule);
    bool remove_access_rule(const std::string& path);
    bool update_access_rule(const std::string& path, const AccessRule& rule);

    // Group management
    bool add_group(const std::string& group_name);
    bool remove_group(const std::string& group_name);
    bool add_user_to_group(const std::string& username, const std::string& group);
    bool remove_user_from_group(const std::string& username, const std::string& group);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, AuthCredentials> users_;
    std::unordered_map<std::string, AuthToken> tokens_;
    std::unordered_map<std::string, X509*> certificates_;
    std::unordered_map<std::string, std::vector<std::string>> groups_;  // group -> users
    std::vector<AccessRule> access_rules_;

    static constexpr auto TOKEN_VALIDITY = std::chrono::hours(24);
    static constexpr auto TOKEN_LENGTH = 32;
    static constexpr size_t MIN_PASSWORD_LENGTH = 8;

    // Password handling
    std::string hash_password(const std::string& password);
    bool verify_password(const std::string& password, const std::string& hash);
    bool validate_password_strength(const std::string& password);

    // Token handling
    std::string generate_secure_token();
    bool validate_token_format(const std::string& token);
    void cleanup_expired_tokens();

    // Certificate handling
    bool validate_certificate(X509* cert);
    bool check_certificate_revocation(X509* cert);
    void cleanup_expired_certificates();

    // Security helpers
    bool is_password_compromised(const std::string& password);
    bool enforce_password_history(const std::string& username, 
                                const std::string& new_password);
    void log_auth_attempt(const std::string& username, bool success);

    bool match_path_rule(const std::string& path, const AccessRule& rule);
    bool user_has_permission(const std::string& username, const AccessRule& rule);
    std::vector<std::string> get_user_groups(const std::string& username);
};

} // namespace fuse_t 
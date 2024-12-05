struct ServerConfig {
    // ... existing config options ...

    // Authentication settings
    struct AuthConfig {
        std::string service_name{"nfs@localhost"};
        bool require_auth{true};
        bool allow_sys_auth{true};
        bool allow_gss_auth{true};
        std::string keytab_path{"/etc/krb5.keytab"};
        std::vector<std::string> allowed_principals;
    } auth;
}; 
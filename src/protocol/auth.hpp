#pragma once
#include <string>
#include <vector>

namespace fused {

struct AuthCredentials {
    std::string username;
    std::vector<uint8_t> data;
    std::vector<uint8_t> mic;
    uint32_t flavor;
};

class Authenticator {
public:
    virtual ~Authenticator() = default;
    virtual bool verify_credentials(const AuthCredentials& creds) = 0;
    virtual bool check_permissions(const AuthCredentials& creds, 
                                 const std::string& path,
                                 uint32_t access_mask) = 0;
};

} // namespace fused 
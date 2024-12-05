#include "auth.hpp"
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <limits.h>
#include <stdexcept>

namespace fused {

// AuthSysParams implementation
void AuthSysParams::encode(XDREncoder& encoder) const {
    encoder.encode_uint32(stamp);
    encoder.encode_string(machine_name);
    encoder.encode_uint32(uid);
    encoder.encode_uint32(gid);
    
    // Encode auxiliary groups
    encoder.encode_uint32(aux_gids.size());
    for (uint32_t gid : aux_gids) {
        encoder.encode_uint32(gid);
    }
}

AuthSysParams AuthSysParams::decode(XDRDecoder& decoder) {
    AuthSysParams params;
    params.stamp = decoder.decode_uint32();
    params.machine_name = decoder.decode_string();
    params.uid = decoder.decode_uint32();
    params.gid = decoder.decode_uint32();
    
    // Decode auxiliary groups
    uint32_t num_groups = decoder.decode_uint32();
    if (num_groups > NGRPS) {
        throw std::runtime_error("Too many auxiliary groups");
    }
    
    params.aux_gids.reserve(num_groups);
    for (uint32_t i = 0; i < num_groups; ++i) {
        params.aux_gids.push_back(decoder.decode_uint32());
    }
    
    return params;
}

// AuthSysCredsGenerator implementation
std::vector<uint8_t> AuthSysCredsGenerator::generate_creds() {
    // Get current user info
    uid_t uid = getuid();
    gid_t gid = getgid();
    
    // Get auxiliary groups
    std::vector<uint32_t> aux_gids;
    gid_t groups[NGRPS];
    int ngroups = NGRPS;
    
    if (getgroups(ngroups, groups) >= 0) {
        for (int i = 0; i < ngroups; ++i) {
            aux_gids.push_back(groups[i]);
        }
    }
    
    return generate_creds(uid, gid, aux_gids);
}

std::vector<uint8_t> AuthSysCredsGenerator::generate_creds(
    uint32_t uid, uint32_t gid, const std::vector<uint32_t>& aux_gids) {
    
    AuthSysParams params;
    params.stamp = generate_stamp();
    params.machine_name = get_machine_name();
    params.uid = uid;
    params.gid = gid;
    params.aux_gids = aux_gids;
    
    XDREncoder encoder;
    params.encode(encoder);
    return encoder.get_buffer();
}

std::string AuthSysCredsGenerator::get_machine_name() {
    char hostname[HOST_NAME_MAX + 1];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        return "localhost";
    }
    return hostname;
}

uint32_t AuthSysCredsGenerator::generate_stamp() {
    return static_cast<uint32_t>(time(nullptr));
}

// AuthSysVerifier implementation
bool AuthSysVerifier::verify_creds(const std::vector<uint8_t>& creds, AuthSysParams& params) {
    try {
        XDRDecoder decoder(creds.data(), creds.size());
        params = AuthSysParams::decode(decoder);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<uint8_t> AuthSysVerifier::generate_verf(const AuthSysParams& params) {
    // For AUTH_SYS, the verifier is typically empty
    return std::vector<uint8_t>();
}

} // namespace fuse_t 
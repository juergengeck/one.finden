#pragma once
#include <vector>
#include <memory>
#include "xdr.hpp"
#include "auth.hpp"

namespace fused {

// NFSv4 operation codes
enum class NFSv4Op : uint32_t {
    ACCESS      = 3,
    CLOSE       = 4,
    COMMIT      = 5,
    CREATE      = 6,
    DELEGPURGE  = 7,
    DELEGRETURN = 8,
    GETATTR     = 9,
    GETFH       = 10,
    LINK        = 11,
    LOCK        = 12,
    LOCKT       = 13,
    LOCKU       = 14,
    LOOKUP      = 15,
    LOOKUPP     = 16,
    NVERIFY     = 17,
    OPEN        = 18,
    OPENATTR    = 19,
    OPEN_CONFIRM = 20,
    OPEN_DOWNGRADE = 21,
    PUTFH       = 22,
    PUTPUBFH    = 23,
    PUTROOTFH   = 24,
    READ        = 25,
    READDIR     = 26,
    READLINK    = 27,
    REMOVE      = 28,
    RENAME      = 29,
    RENEW       = 30,
    RESTOREFH   = 31,
    SAVEFH      = 32,
    SECINFO     = 33,
    SETATTR     = 34,
    SETCLIENTID = 35,
    SETCLIENTID_CONFIRM = 36,
    VERIFY      = 37,
    WRITE       = 38,
    RELEASE_LOCKOWNER = 39
};

// Base class for operation arguments
struct NFSv4OpArgs {
    virtual ~NFSv4OpArgs() = default;
    virtual void encode(XDREncoder& encoder) const = 0;
    virtual NFSv4Op opcode() const = 0;
};

// Base class for operation results
struct NFSv4OpResult {
    virtual ~NFSv4OpResult() = default;
    virtual void encode(XDREncoder& encoder) const = 0;
    virtual void decode(XDRDecoder& decoder) = 0;
};

// Compound operation
struct NFSv4CompoundOp {
    NFSv4Op op;
    std::unique_ptr<NFSv4OpArgs> args;
    std::unique_ptr<NFSv4OpResult> result;
};

// Compound request
struct NFSv4CompoundRequest {
    AuthSysParams auth;
    std::string tag;
    uint32_t minorversion;
    std::vector<NFSv4CompoundOp> operations;

    void encode(XDREncoder& encoder) const;
    static NFSv4CompoundRequest decode(XDRDecoder& decoder);
};

// Compound response
struct NFSv4CompoundResponse {
    std::string tag;
    uint32_t status;
    std::vector<NFSv4CompoundOp> operations;

    void encode(XDREncoder& encoder) const;
    static NFSv4CompoundResponse decode(XDRDecoder& decoder);
};

// Compound procedure processor
class CompoundProc {
public:
    explicit CompoundProc(AuthSysVerifier& auth_verifier);

    // Process a compound request
    NFSv4CompoundResponse process(const NFSv4CompoundRequest& request);

private:
    AuthSysVerifier& auth_verifier_;
    
    // Current state during compound processing
    struct {
        AuthSysParams auth;
        NFSFileHandle current_fh;
        NFSFileHandle saved_fh;
    } state_;

    // Process individual operations
    std::unique_ptr<NFSv4OpResult> process_op(const NFSv4CompoundOp& op);
};

} // namespace fuse_t 
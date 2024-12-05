#pragma once
#include "compound.hpp"
#include "nfs_protocol.hpp"

namespace fused {

// Common status for all operations
struct NFSv4Status {
    uint32_t status;
    
    void encode(XDREncoder& encoder) const {
        encoder.encode_uint32(status);
    }
    
    void decode(XDRDecoder& decoder) {
        status = decoder.decode_uint32();
    }
};

// PUTFH operation
struct PutFHArgs : NFSv4OpArgs {
    NFSFileHandle handle;
    
    NFSv4Op opcode() const override { return NFSv4Op::PUTFH; }
    void encode(XDREncoder& encoder) const override;
};

struct PutFHResult : NFSv4OpResult {
    NFSv4Status status;
    
    void encode(XDREncoder& encoder) const override;
    void decode(XDRDecoder& decoder) override;
};

// GETFH operation
struct GetFHArgs : NFSv4OpArgs {
    NFSv4Op opcode() const override { return NFSv4Op::GETFH; }
    void encode(XDREncoder& encoder) const override {}  // No arguments
};

struct GetFHResult : NFSv4OpResult {
    NFSv4Status status;
    NFSFileHandle handle;
    
    void encode(XDREncoder& encoder) const override;
    void decode(XDRDecoder& decoder) override;
};

// GETATTR operation
struct GetAttrArgs : NFSv4OpArgs {
    uint32_t attr_request;  // Bitmap of requested attributes
    
    NFSv4Op opcode() const override { return NFSv4Op::GETATTR; }
    void encode(XDREncoder& encoder) const override;
};

struct GetAttrResult : NFSv4OpResult {
    NFSv4Status status;
    NFSFattr4 attrs;
    
    void encode(XDREncoder& encoder) const override;
    void decode(XDRDecoder& decoder) override;
};

// LOOKUP operation
struct LookupArgs : NFSv4OpArgs {
    std::string name;
    
    NFSv4Op opcode() const override { return NFSv4Op::LOOKUP; }
    void encode(XDREncoder& encoder) const override;
};

struct LookupResult : NFSv4OpResult {
    NFSv4Status status;
    
    void encode(XDREncoder& encoder) const override;
    void decode(XDRDecoder& decoder) override;
};

// CREATE operation
struct CreateArgs : NFSv4OpArgs {
    std::string name;
    NFSType type;
    NFSFattr4 attrs;
    
    NFSv4Op opcode() const override { return NFSv4Op::CREATE; }
    void encode(XDREncoder& encoder) const override;
};

struct CreateResult : NFSv4OpResult {
    NFSv4Status status;
    NFSFileHandle handle;    // Handle of created file
    NFSFattr4 attrs;        // Attributes of created file
    
    void encode(XDREncoder& encoder) const override;
    void decode(XDRDecoder& decoder) override;
};

// REMOVE operation
struct RemoveArgs : NFSv4OpArgs {
    std::string name;
    
    NFSv4Op opcode() const override { return NFSv4Op::REMOVE; }
    void encode(XDREncoder& encoder) const override;
};

struct RemoveResult : NFSv4OpResult {
    NFSv4Status status;
    
    void encode(XDREncoder& encoder) const override;
    void decode(XDRDecoder& decoder) override;
};

// RENAME operation
struct RenameArgs : NFSv4OpArgs {
    std::string old_name;
    std::string new_name;
    NFSFileHandle dst_dir_handle;  // Handle of destination directory
    
    NFSv4Op opcode() const override { return NFSv4Op::RENAME; }
    void encode(XDREncoder& encoder) const override;
};

struct RenameResult : NFSv4OpResult {
    NFSv4Status status;
    
    void encode(XDREncoder& encoder) const override;
    void decode(XDRDecoder& decoder) override;
};

// SETATTR operation
struct SetAttrArgs : NFSv4OpArgs {
    NFSFattr4 attrs;        // New attributes to set
    uint32_t attr_mask;     // Bitmap of attributes to set
    
    NFSv4Op opcode() const override { return NFSv4Op::SETATTR; }
    void encode(XDREncoder& encoder) const override;
};

struct SetAttrResult : NFSv4OpResult {
    NFSv4Status status;
    NFSFattr4 attrs;        // Current attributes after the operation
    
    void encode(XDREncoder& encoder) const override;
    void decode(XDRDecoder& decoder) override;
};

// READLINK operation
struct ReadLinkArgs : NFSv4OpArgs {
    NFSv4Op opcode() const override { return NFSv4Op::READLINK; }
    void encode(XDREncoder& encoder) const override {}  // No arguments needed
};

struct ReadLinkResult : NFSv4OpResult {
    NFSv4Status status;
    std::string link_content;  // Target path of the symbolic link
    
    void encode(XDREncoder& encoder) const override;
    void decode(XDRDecoder& decoder) override;
};

// SYMLINK operation
struct SymLinkArgs : NFSv4OpArgs {
    std::string name;         // Name of the symlink
    std::string link_data;    // Target path
    NFSFattr4 attrs;         // Initial attributes
    
    NFSv4Op opcode() const override { return NFSv4Op::SYMLINK; }
    void encode(XDREncoder& encoder) const override;
};

struct SymLinkResult : NFSv4OpResult {
    NFSv4Status status;
    NFSFileHandle handle;    // Handle of created symlink
    NFSFattr4 attrs;        // Attributes of created symlink
    
    void encode(XDREncoder& encoder) const override;
    void decode(XDRDecoder& decoder) override;
};

// Add attribute mask constants at the top with other constants
constexpr uint32_t FATTR4_TYPE  = 0x00000001;  // File type
constexpr uint32_t FATTR4_MODE  = 0x00000002;  // Protection mode bits
constexpr uint32_t FATTR4_NUMLINKS = 0x00000004;  // Number of hard links
constexpr uint32_t FATTR4_OWNER = 0x00000008;  // Owner user ID
constexpr uint32_t FATTR4_OWNER_GROUP = 0x00000010;  // Owner group ID
constexpr uint32_t FATTR4_SIZE  = 0x00000020;  // File size in bytes
constexpr uint32_t FATTR4_SPACE_USED = 0x00000040;  // Bytes allocated
constexpr uint32_t FATTR4_FSID  = 0x00000080;  // File system ID
constexpr uint32_t FATTR4_FILEID = 0x00000100;  // File ID
constexpr uint32_t FATTR4_TIME_ACCESS = 0x00000200;  // Time of last access
constexpr uint32_t FATTR4_TIME_MODIFY = 0x00000400;  // Time of last modification
constexpr uint32_t FATTR4_TIME_METADATA = 0x00000800;  // Time of last metadata change

} // namespace fuse_t 
#pragma once
#include <vector>
#include <memory>
#include "operations.hpp"
#include "nfs_protocol.hpp"

namespace fused {

// Compound operation context
struct CompoundContext {
    NFSFileHandle current_fh;    // Current filehandle
    NFSFileHandle saved_fh;      // Saved filehandle
    AuthSysParams auth;          // Current auth context
    uint32_t status{NFS4_OK};    // Current operation status
};

// Compound operation sequence
struct CompoundSequence {
    std::string tag;             // Client-provided tag
    uint32_t minorversion{0};    // NFSv4 minor version
    std::vector<NFSv4CompoundOp> operations;
    CompoundContext context;
};

// Compound operation processor
class CompoundProcessor {
public:
    explicit CompoundProcessor(NFSServer& server);

    // Process a compound sequence
    NFSv4CompoundResponse process(const CompoundSequence& sequence);

private:
    // Process individual operations
    bool process_op(NFSv4CompoundOp& op, CompoundContext& ctx);
    
    // Operation handlers
    bool handle_putfh(const PutFHArgs& args, CompoundContext& ctx);
    bool handle_getfh(const GetFHArgs& args, CompoundContext& ctx);
    bool handle_putrootfh(CompoundContext& ctx);
    bool handle_savefh(CompoundContext& ctx);
    bool handle_restorefh(CompoundContext& ctx);
    bool handle_lookup(const LookupArgs& args, CompoundContext& ctx);
    bool handle_getattr(const GetAttrArgs& args, CompoundContext& ctx);
    bool handle_create(const CreateArgs& args, CompoundContext& ctx);
    bool handle_remove(const RemoveArgs& args, CompoundContext& ctx);
    bool handle_read(const ReadArgs& args, CompoundContext& ctx);
    bool handle_write(const WriteArgs& args, CompoundContext& ctx);
    bool handle_readdir(const ReadDirArgs& args, CompoundContext& ctx);
    bool handle_rename(const RenameArgs& args, CompoundContext& ctx);
    bool handle_setattr(const SetAttrArgs& args, CompoundContext& ctx);

    NFSServer& server_;
};

} // namespace fuse_t 
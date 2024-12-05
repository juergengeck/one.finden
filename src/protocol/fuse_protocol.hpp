#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fused {

enum class FuseOpcode {
    LOOKUP = 1,
    GETATTR = 3,
    SETATTR = 4,
    READ = 5,
    WRITE = 6,
    CREATE = 8,
    UNLINK = 10,
    MKDIR = 11,
    RMDIR = 12,
    RENAME = 13,
    READDIR = 15
};

struct FuseOperation {
    FuseOpcode opcode;
    uint64_t nodeid;
    uint32_t uid;
    uint32_t gid;
    uint32_t pid;
    std::string name;
    std::vector<uint8_t> data;
};

} // namespace fused 
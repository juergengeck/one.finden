#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fused {

// NFS constants
constexpr uint32_t NFS_OK = 0;
constexpr uint32_t NFS_PROG = 100003;    // NFS program number
constexpr uint32_t NFS_V4 = 4;           // NFSv4
constexpr uint32_t FATTR4_MODE  = 0x00000001;  // Change file mode
constexpr uint32_t FATTR4_UID   = 0x00000002;  // Change user ID
constexpr uint32_t FATTR4_GID   = 0x00000004;  // Change group ID
constexpr uint32_t FATTR4_SIZE  = 0x00000008;  // Change file size (truncate)
constexpr uint32_t FATTR4_ATIME = 0x00000010;  // Change access time
constexpr uint32_t FATTR4_MTIME = 0x00000020;  // Change modification time

// NFS procedures
enum class NFSProcedure {
    NULL_PROC = 0,
    GETATTR = 1,
    SETATTR = 2,
    LOOKUP = 3,
    READ = 6,
    WRITE = 7,
    CREATE = 8,
    REMOVE = 9,
    RENAME = 10,
    MKDIR = 11,
    RMDIR = 12,
    READDIR = 13,
    LOCK = 14,
    UNLOCK = 15
};

struct NFSFileHandle {
    std::vector<uint8_t> handle;
};

// NFS status codes
enum class NFSStatus : uint32_t {
    OK = 0,
    PERM = 1,
    NOENT = 2,
    IO = 5,
    NXIO = 6,
    ACCESS = 13,
    EXIST = 17,
    XDEV = 18,
    NOTDIR = 20,
    ISDIR = 21,
    INVAL = 22,
    FBIG = 27,
    NOSPC = 28,
    ROFS = 30,
    NAMETOOLONG = 63,
    NOTEMPTY = 66,
    DQUOT = 69,
    STALE = 70,
    BADHANDLE = 10001,
    NOT_SYNC = 10002,
    BAD_COOKIE = 10003,
    NOTSUPP = 10004,
    TOOSMALL = 10005,
    SERVERFAULT = 10006,
    BADTYPE = 10007,
    DELAY = 10008,
    SAME = 10009,
    DENIED = 10010,
    EXPIRED = 10011,
    LOCKED = 10012,
    GRACE = 10013,
    FHEXPIRED = 10014,
    SHARE_DENIED = 10015,
    WRONGSEC = 10016,
    CLID_INUSE = 10017,
    RESOURCE = 10018,
    MOVED = 10019,
    NOFILEHANDLE = 10020,
    MINOR_VERS_MISMATCH = 10021,
    STALE_CLIENTID = 10022,
    STALE_STATEID = 10023,
    OLD_STATEID = 10024,
    BAD_STATEID = 10025,
    BAD_SEQID = 10026,
    NOT_SAME = 10027,
    LOCK_RANGE = 10028,
    SYMLINK = 10029,
    RESTOREFH = 10030,
    LEASE_MOVED = 10031,
    ATTRNOTSUPP = 10032,
    NO_GRACE = 10033,
    RECLAIM_BAD = 10034,
    RECLAIM_CONFLICT = 10035,
    BADXDR = 10036,
    LOCKS_HELD = 10037,
    OPENMODE = 10038,
    BADOWNER = 10039,
    BADCHAR = 10040,
    BADNAME = 10041,
    BAD_RANGE = 10042,
    LOCK_NOTSUPP = 10043,
    OP_ILLEGAL = 10044,
    DEADLOCK = 10045,
    FILE_OPEN = 10046,
    ADMIN_REVOKED = 10047,
    CB_PATH_DOWN = 10048
};

// File types
enum class NFSType : uint32_t {
    REG = 1,
    DIR = 2,
    BLK = 3,
    CHR = 4,
    LNK = 5,
    SOCK = 6,
    FIFO = 7
};

struct NFSFattr4 {
    NFSType type;
    uint32_t mode;
    uint32_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    uint64_t used;
    uint64_t rdev;
    uint64_t fsid;
    uint64_t fileid;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
};

struct NFSLookupResult {
    NFSStatus status;
    NFSFileHandle file_handle;
    NFSFattr4 attrs;
};

struct NFSReadArgs {
    NFSFileHandle file_handle;
    uint64_t offset;
    uint32_t count;
};

struct NFSReadResult {
    NFSStatus status;
    uint32_t count;
    bool eof;
    std::vector<uint8_t> data;
};

struct NFSWriteArgs {
    NFSFileHandle file_handle;
    uint64_t offset;
    uint32_t count;
    std::vector<uint8_t> data;
};

struct NFSWriteResult {
    NFSStatus status;
    uint32_t count;
    bool committed;
};

struct NFSOperation {
    NFSProcedure procedure;
    NFSFileHandle file_handle;
    std::vector<uint8_t> data;
};

struct NFSDirEntry {
    uint64_t fileid;      // File ID (inode number)
    std::string name;     // File name
    NFSFattr4 attrs;      // File attributes
};

struct NFSReadDirArgs {
    NFSFileHandle dir_handle;
    uint64_t cookie;      // Position in directory
    uint32_t count;       // Max size of response
};

struct NFSReadDirResult {
    NFSStatus status;
    std::vector<NFSDirEntry> entries;
    bool eof;             // True if no more entries
};

struct NFSCreateArgs {
    NFSFileHandle dir_handle;    // Parent directory handle
    std::string name;           // Name of file to create
    uint32_t mode;             // File mode/permissions
};

struct NFSCreateResult {
    NFSStatus status;
    NFSFileHandle file_handle;  // Handle of created file
    NFSFattr4 attrs;           // Attributes of created file
};

struct NFSRemoveArgs {
    NFSFileHandle dir_handle;   // Parent directory handle
    std::string name;          // Name of file to remove
};

struct NFSRemoveResult {
    NFSStatus status;
};

struct NFSMkdirArgs {
    NFSFileHandle parent_handle;  // Parent directory handle
    std::string name;            // Name of directory to create
    uint32_t mode;              // Directory mode/permissions
};

struct NFSMkdirResult {
    NFSStatus status;
    NFSFileHandle dir_handle;    // Handle of created directory
    NFSFattr4 attrs;            // Attributes of created directory
};

struct NFSRmdirArgs {
    NFSFileHandle parent_handle; // Parent directory handle
    std::string name;           // Name of directory to remove
};

struct NFSRmdirResult {
    NFSStatus status;
};

struct NFSRenameArgs {
    NFSFileHandle src_dir_handle;  // Source directory handle
    std::string src_name;         // Source file name
    NFSFileHandle dst_dir_handle; // Destination directory handle
    std::string dst_name;        // Destination file name
};

struct NFSRenameResult {
    NFSStatus status;
};

struct NFSSetAttrArgs {
    NFSFileHandle file_handle;
    NFSFattr4 attrs;        // New attributes to set
    uint32_t attr_mask;     // Bit mask indicating which attributes to set
};

struct NFSSetAttrResult {
    NFSStatus status;
    NFSFattr4 attrs;        // Current attributes after the operation
};

// Add after NFSSetAttrResult
enum class NFSLockType : uint32_t {
    READ = 1,    // Shared read lock
    WRITE = 2    // Exclusive write lock
};

struct NFSLockArgs {
    NFSFileHandle file_handle;
    uint64_t offset;     // Starting offset
    uint64_t length;     // Length of region to lock
    NFSLockType type;    // Type of lock
    bool wait;          // Wait if lock is busy
};

struct NFSUnlockArgs {
    NFSFileHandle file_handle;
    uint64_t offset;     // Starting offset
    uint64_t length;     // Length of region to unlock
};

struct NFSLockResult {
    NFSStatus status;
};

} // namespace fused 
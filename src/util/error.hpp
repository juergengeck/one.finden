#pragma once
#include <string>
#include <system_error>
#include "logger.hpp"

namespace fused {

// Error handling wrapper with logging
class Error {
public:
    static bool check(bool condition, const char* msg) {
        if (!condition) {
            LOG_ERROR(msg);
            return false;
        }
        return true;
    }
    
    static bool check(bool condition, const char* msg, int err) {
        if (!condition) {
            LOG_ERROR("{}: {}", msg, std::system_error(err, std::system_category()).what());
            return false;
        }
        return true;
    }
    
    template<typename T>
    static bool check_not_null(const T* ptr, const char* msg) {
        if (!ptr) {
            LOG_ERROR("{}: null pointer", msg);
            return false;
        }
        return true;
    }
    
    static bool check_errno(int ret, const char* msg) {
        if (ret < 0) {
            LOG_ERROR("{}: {}", msg, std::system_error(errno, std::system_category()).what());
            return false;
        }
        return true;
    }
    
    static bool check_path(const std::string& path, const char* msg) {
        if (path.empty()) {
            LOG_ERROR("{}: empty path", msg);
            return false;
        }
        return true;
    }
};

// Convenience macros
#define CHECK(condition, msg) \
    do { if (!Error::check(condition, msg)) return false; } while(0)

#define CHECK_ERRNO(ret, msg) \
    do { if (!Error::check_errno(ret, msg)) return false; } while(0)

#define CHECK_PATH(path, msg) \
    do { if (!Error::check_path(path, msg)) return false; } while(0)

#define CHECK_NOT_NULL(ptr, msg) \
    do { if (!Error::check_not_null(ptr, msg)) return false; } while(0)

} // namespace fuse_t 
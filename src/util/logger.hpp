#pragma once
#include <string>
#include <sstream>
#include <mutex>
#include <fstream>

namespace fused {

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    void initialize(const std::string& log_file, LogLevel min_level = LogLevel::INFO);
    
    template<typename... Args>
    void debug(const char* fmt, const Args&... args) {
        log(LogLevel::DEBUG, fmt, args...);
    }
    
    template<typename... Args>
    void info(const char* fmt, const Args&... args) {
        log(LogLevel::INFO, fmt, args...);
    }
    
    template<typename... Args>
    void warn(const char* fmt, const Args&... args) {
        log(LogLevel::WARN, fmt, args...);
    }
    
    template<typename... Args>
    void error(const char* fmt, const Args&... args) {
        log(LogLevel::ERROR, fmt, args...);
    }
    
    template<typename... Args>
    void fatal(const char* fmt, const Args&... args) {
        log(LogLevel::FATAL, fmt, args...);
    }

private:
    Logger() = default;
    ~Logger();
    
    template<typename... Args>
    void log(LogLevel level, const char* fmt, const Args&... args);
    
    std::string format_time();
    const char* level_to_string(LogLevel level);
    
    std::mutex mutex_;
    std::ofstream log_file_;
    LogLevel min_level_{LogLevel::INFO};
    bool initialized_{false};
};

// Helper macros
#define LOG_DEBUG(...) Logger::instance().debug(__VA_ARGS__)
#define LOG_INFO(...)  Logger::instance().info(__VA_ARGS__)
#define LOG_WARN(...)  Logger::instance().warn(__VA_ARGS__)
#define LOG_ERROR(...) Logger::instance().error(__VA_ARGS__)
#define LOG_FATAL(...) Logger::instance().fatal(__VA_ARGS__)

} // namespace fuse_t 
#include "logger.hpp"
#include <ctime>
#include <iomanip>
#include <iostream>

namespace fused {

void Logger::initialize(const std::string& log_file, LogLevel min_level) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return;
    }
    
    log_file_.open(log_file, std::ios::app);
    if (!log_file_) {
        std::cerr << "Failed to open log file: " << log_file << std::endl;
        return;
    }
    
    min_level_ = min_level;
    initialized_ = true;
    
    info("Logging initialized at level {}", level_to_string(min_level));
}

Logger::~Logger() {
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

template<typename... Args>
void Logger::log(LogLevel level, const char* fmt, const Args&... args) {
    if (!initialized_ || level < min_level_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::stringstream ss;
    ss << format_time() << " [" << level_to_string(level) << "] ";
    
    // Format message
    std::string msg = fmt;
    int unpack[] = {0, ((void)format_arg(ss, msg, args), 0)...};
    (void)unpack;
    
    ss << msg << std::endl;
    
    log_file_ << ss.str();
    log_file_.flush();
    
    // Also output to stderr for ERROR and FATAL
    if (level >= LogLevel::ERROR) {
        std::cerr << ss.str();
    }
}

std::string Logger::format_time() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms;
    return ss.str();
}

const char* Logger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default:             return "UNKNOWN";
    }
}

// Helper function to format arguments
template<typename T>
void format_arg(std::stringstream& ss, std::string& fmt, const T& arg) {
    size_t pos = fmt.find("{}");
    if (pos != std::string::npos) {
        ss << fmt.substr(0, pos) << arg;
        fmt = fmt.substr(pos + 2);
    }
}

} // namespace fuse_t 
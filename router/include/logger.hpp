#pragma once
#include <fstream>
#include <mutex>
#include <string>
#include <sstream>
#include <ctime>
#include <iomanip>
#include <cstdlib>
#include <atomic>

namespace razor {

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3 };

class Logger {
public:
    static Logger& Instance() {
        static Logger instance;
        return instance;
    }

    // Set minimum level (default: DEBUG — capture everything)
    void SetLevel(LogLevel level) { min_level_ = level; }

    void Log(LogLevel level, const char* file, int line, const std::string& msg) {
        if (static_cast<int>(level) < static_cast<int>(min_level_.load())) return;

        const char* level_str = "DEBUG";
        switch (level) {
            case LogLevel::DEBUG: level_str = "DEBUG"; break;
            case LogLevel::INFO:  level_str = "INFO";  break;
            case LogLevel::WARN:  level_str = "WARN";  break;
            case LogLevel::ERROR: level_str = "ERROR"; break;
        }

        // Shorten file to basename only
        const char* slash = nullptr;
        for (const char* p = file; *p; ++p) if (*p == '/') slash = p;
        const char* basename = slash ? slash + 1 : file;

        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) % 1000;

        std::lock_guard<std::mutex> lock(mutex_);
        if (!ofs_.is_open()) return;

        ofs_ << "["
             << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S")
             << "." << std::setfill('0') << std::setw(3) << ms.count()
             << "] [" << level_str << "] "
             << basename << ":" << line << " | "
             << msg << "\n";
        ofs_.flush();
    }

private:
    Logger() {
        const char* home = std::getenv("HOME");
        std::string path = home ? std::string(home) + "/.razor/debug.log" : "/tmp/razor_debug.log";
        ofs_.open(path, std::ios::app);
        // Write a session separator so log is easy to scan
        if (ofs_.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto t   = std::chrono::system_clock::to_time_t(now);
            ofs_ << "\n============================================================\n"
                 << "  Razor Debug Session — "
                 << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S")
                 << "\n============================================================\n";
            ofs_.flush();
        }
    }

    std::ofstream ofs_;
    std::mutex    mutex_;
    std::atomic<LogLevel> min_level_{LogLevel::DEBUG};
};

} // namespace razor

// ─── Convenience macros ───────────────────────────────────────────────────────
#define RLOG_DEBUG(msg) do { \
    std::ostringstream _rlog_ss; _rlog_ss << msg; \
    razor::Logger::Instance().Log(razor::LogLevel::DEBUG, __FILE__, __LINE__, _rlog_ss.str()); \
} while(0)

#define RLOG_INFO(msg) do { \
    std::ostringstream _rlog_ss; _rlog_ss << msg; \
    razor::Logger::Instance().Log(razor::LogLevel::INFO, __FILE__, __LINE__, _rlog_ss.str()); \
} while(0)

#define RLOG_WARN(msg) do { \
    std::ostringstream _rlog_ss; _rlog_ss << msg; \
    razor::Logger::Instance().Log(razor::LogLevel::WARN, __FILE__, __LINE__, _rlog_ss.str()); \
} while(0)

#define RLOG_ERROR(msg) do { \
    std::ostringstream _rlog_ss; _rlog_ss << msg; \
    razor::Logger::Instance().Log(razor::LogLevel::ERROR, __FILE__, __LINE__, _rlog_ss.str()); \
} while(0)

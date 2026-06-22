#include "Logger.hpp"
 
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
    std::ofstream g_logFile;
    bool g_initialized = false;
 
    // True if `level` should be treated as >= kConsoleMinLevel.
    // LogLevel has no operator< by default, so compare explicitly
    // rather than relying on enum ordinal order staying meaningful
    // if the enum is ever reordered.
    int LevelRank(LogLevel level) {
        switch (level) {
            case LogLevel::Info:    return 0;
            case LogLevel::Warning: return 1;
            case LogLevel::Error:   return 2;
            case LogLevel::Fatal:   return 3;
            default:                return 0;
        }
    }
}

void Logger::Init(const std::string& directory) {
    if (g_initialized) return;

    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    // If directory creation fails (ec set), we still attempt to open
    // the file below — ofstream will simply fail to open and every
    // subsequent Log() call falls back to console-only, which is an
    // acceptable degradation rather than a crash.
 
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime = *std::localtime(&time);

    std::ostringstream filename;
    filename << directory << "/"
        << std::put_time(&localTime, "%Y-%m-%d_%H-%M-%S") << ".log";
 
    g_logFile.open(filename.str(), std::ios::out | std::ios::trunc);
 
    g_initialized = true;
 
    // This line always goes through Log() itself so it lands in the
    // file too, and (being Info) stays out of the console.
    Log(LogLevel::Info, "Logger", "Log file opened: " + filename.str());
}

void Logger::Shutdown() {
    if (!g_logFile.is_open()) return;
    Log(LogLevel::Info, "Logger", "Shutting down logger");
    g_logFile.flush();
    g_logFile.close();
    g_initialized = false;
}

const char* Logger::GetLevelString(LogLevel level) {
    switch (level) {
        case LogLevel::Info:
            return "INFO";

        case LogLevel::Warning:
            return "WARN";

        case LogLevel::Error:
            return "ERROR";

        case LogLevel::Fatal:
            return "FATAL";
        
        default:
            return "UNKNOWN";
    }
}

const char* Logger::GetLevelColor(LogLevel level) {
    switch (level) {
        case LogLevel::Info:
            return "\033[32m";

        case LogLevel::Warning:
            return "\033[33m";

        case LogLevel::Error:
            return "\033[31m";

        case LogLevel::Fatal:
            return "\033[35m";
        
        default:
            return "\033[0m";
    }
}

void Logger::Log(LogLevel level, const std::string &category, const std::string &message) {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime = *std::localtime(&time);

    std::ostringstream line;
    line << "[" << std::put_time(&localTime, "%H:%M:%S") << "]"
        << "[" << GetLevelString(level) << "]"
        << "[" << category << "]"
        << message;

    // File sink — receives every level, no color codes (plain text
    // for easy grepping/post-mortem reading).
    if (g_logFile.is_open()) {
        g_logFile << line.str() << '\n';
        // Flush per line: log volume here is low (engine/game events,
        // not per-frame), and we'd rather not lose the tail of the
        // file if the process is killed/crashes mid-run.
        g_logFile.flush();
    }

    // Console sink — only Warning and above.
    if (LevelRank(level) >= LevelRank(kConsoleMinLevel)) {
        std::ostream& out = (level == LogLevel::Error || level == LogLevel::Fatal)
            ? std::cerr
            : std::cout;
 
        out << GetLevelColor(level) << line.str() << "\033[0m" << '\n';
    }
}
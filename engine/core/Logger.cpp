#include "Logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>

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
            return "\033[32m";

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

    std::cout << GetLevelColor(level)
        << "[" << std::put_time(&localTime, "%H:%M:%S") << "]"
        << "[" << GetLevelString(level) << "]"
        << "[" << category << "]"
        << message << "\033[0m" << '\n';
}
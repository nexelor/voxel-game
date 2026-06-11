#pragma once

#include <string>
enum class LogLevel {
    Info,
    Warning,
    Error,
    Fatal
};

class Logger {
public:
    static void Log(
        LogLevel level,
        const std::string& category,
        const std::string& message
    );

private:
    static const char* GetLevelString(
        LogLevel level
    );

    static const char* GetLevelColor(
        LogLevel level
    );
};
#pragma once

#include <string>
enum class LogLevel {
    Info,
    Warning,
    Error,
    Fatal
};

// ─────────────────────────────────────────────
//  Logger
//
//  Every call to Log() always writes to the log
//  file (see Init()), regardless of level.
//
//  Only Warning/Error/Fatal are also echoed to
//  stdout/stderr — Info is file-only, so the
//  console stays readable during normal play
//  while the file keeps the full history for
//  post-mortem debugging.
//
//  Usage (once, near the very start of main /
//  Application::Initialize, before any other
//  Logger::Log call):
//
//      Logger::Init("logs");   // creates logs/<timestamp>.log
//      ...
//      Logger::Shutdown();     // flush + close, at the very end
//
//  If Init() is never called, Log() still works —
//  it just writes to console only (Warning+), with
//  no file sink. This avoids crashing/throwing for
//  any call site that fires before Init(), at the
//  cost of losing that line to the log file.
// ─────────────────────────────────────────────

class Logger {
public:
    // Creates `directory` if needed and opens
    // `directory/<timestamp>.log` for the rest of
    // the program's lifetime. Safe to call once.
    static void Init(const std::string& directory = "logs");

    // Flushes and closes the log file. Safe to call
    // even if Init() was never called.
    static void Shutdown();

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

    // Minimum level that gets echoed to the console.
    // File sink always receives everything regardless
    // of this threshold.
    static constexpr LogLevel kConsoleMinLevel = LogLevel::Warning;
};
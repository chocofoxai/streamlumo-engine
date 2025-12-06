// streamlumo-engine/src/logging.cpp
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo

#include "logging.h"

#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <mutex>
#include <unistd.h>

namespace streamlumo {

LogLevel Logging::s_level = LogLevel::Info;
std::string Logging::s_logFile;
void* Logging::s_fileHandle = nullptr;

static std::mutex s_logMutex;

void Logging::init(LogLevel level, const std::string& logFile) {
    s_level = level;
    s_logFile = logFile;
    
    if (!logFile.empty()) {
        s_fileHandle = fopen(logFile.c_str(), "a");
        if (!s_fileHandle) {
            fprintf(stderr, "[streamlumo-engine] Warning: Could not open log file: %s\n", logFile.c_str());
        }
    }
}

void Logging::shutdown() {
    if (s_fileHandle) {
        fclose(static_cast<FILE*>(s_fileHandle));
        s_fileHandle = nullptr;
    }
}

LogLevel Logging::getLevel() {
    return s_level;
}

void Logging::log(LogLevel level, const char* format, ...) {
    // Check if we should log this level
    if (static_cast<int>(level) < static_cast<int>(s_level)) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(s_logMutex);
    
    // Get timestamp
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Get level string
    const char* levelStr;
    const char* colorCode = "";
    const char* resetCode = "";
    
    // Use colors for terminal output
    FILE* output = s_fileHandle ? static_cast<FILE*>(s_fileHandle) : stdout;
    bool useColors = !s_fileHandle && isatty(fileno(stdout));
    
    switch (level) {
        case LogLevel::Debug:
            levelStr = "DEBUG";
            if (useColors) { colorCode = "\033[36m"; resetCode = "\033[0m"; }
            break;
        case LogLevel::Info:
            levelStr = "INFO ";
            if (useColors) { colorCode = "\033[32m"; resetCode = "\033[0m"; }
            break;
        case LogLevel::Warning:
            levelStr = "WARN ";
            if (useColors) { colorCode = "\033[33m"; resetCode = "\033[0m"; }
            break;
        case LogLevel::Error:
            levelStr = "ERROR";
            if (useColors) { colorCode = "\033[31m"; resetCode = "\033[0m"; }
            output = s_fileHandle ? static_cast<FILE*>(s_fileHandle) : stderr;
            break;
        default:
            levelStr = "?????";
            break;
    }
    
    // Print prefix
    fprintf(output, "%s[%s] [%s%s%s] ", 
            timestamp, 
            "streamlumo-engine",
            colorCode, levelStr, resetCode);
    
    // Print message
    va_list args;
    va_start(args, format);
    vfprintf(output, format, args);
    va_end(args);
    
    fprintf(output, "\n");
    fflush(output);
}

} // namespace streamlumo

// streamlumo-engine/src/logging.h
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo

#pragma once

#include "config.h"
#include <string>

namespace streamlumo {

/**
 * @brief Logging utility for StreamLumo Engine
 */
class Logging {
public:
    /**
     * @brief Initialize logging system
     * @param level Minimum log level to output
     * @param logFile Optional file path for logging (empty = stdout)
     */
    static void init(LogLevel level, const std::string& logFile = "");
    
    /**
     * @brief Shutdown logging system
     */
    static void shutdown();
    
    /**
     * @brief Log a message
     * @param level Log level
     * @param format Printf-style format string
     * @param ... Format arguments
     */
    static void log(LogLevel level, const char* format, ...);
    
    /**
     * @brief Get current log level
     */
    static LogLevel getLevel();
    
private:
    static LogLevel s_level;
    static std::string s_logFile;
    static void* s_fileHandle;
};

} // namespace streamlumo

// Convenience macros
#define log_debug(fmt, ...) streamlumo::Logging::log(streamlumo::LogLevel::Debug, fmt, ##__VA_ARGS__)
#define log_info(fmt, ...) streamlumo::Logging::log(streamlumo::LogLevel::Info, fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...) streamlumo::Logging::log(streamlumo::LogLevel::Warning, fmt, ##__VA_ARGS__)
#define log_error(fmt, ...) streamlumo::Logging::log(streamlumo::LogLevel::Error, fmt, ##__VA_ARGS__)

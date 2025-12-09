// streamlumo-engine/src/config.h
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo

#pragma once

#include <string>

#define STREAMLUMO_ENGINE_VERSION "1.0.0"

namespace streamlumo {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

/**
 * @brief Configuration class for StreamLumo Engine
 * 
 * Handles command-line argument parsing and stores runtime configuration.
 */
class Config {
public:
    Config();
    
    /**
     * @brief Parse command-line arguments
     * @param argc Argument count
     * @param argv Argument values
     * @return true if parsing succeeded, false if should exit (e.g., --help)
     */
    bool parseArgs(int argc, char* argv[]);
    
    // WebSocket configuration
    int getWebSocketPort() const { return m_websocketPort; }
    const std::string& getWebSocketPassword() const { return m_websocketPassword; }
    
    // Video configuration
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    int getFPS() const { return m_fps; }
    
    // Path configuration
    const std::string& getConfigPath() const { return m_configPath; }
    const std::string& getPluginPath() const { return m_pluginPath; }
    const std::string& getDataPath() const { return m_dataPath; }
    
    // Logging configuration
    LogLevel getLogLevel() const { return m_logLevel; }
    const std::string& getLogFile() const { return m_logFile; }
    bool isQuiet() const { return m_quiet; }

    // Browser helper IPC
    int getHelperPort() const { return m_helperPort; }
    const std::string& getHelperToken() const { return m_helperToken; }
    
private:
    void printHelp() const;
    void printVersion() const;
    
    // WebSocket settings
    int m_websocketPort = 4466;
    std::string m_websocketPassword;
    
    // Video settings
    int m_width = 1920;
    int m_height = 1080;
    int m_fps = 30;
    
    // Paths
    std::string m_configPath;
    std::string m_pluginPath;
    std::string m_dataPath;
    
    // Logging
    LogLevel m_logLevel = LogLevel::Info;
    std::string m_logFile;
    bool m_quiet = false;

    // Browser helper IPC
    int m_helperPort = 4777;
    std::string m_helperToken;
    
    // Test mode
    std::string m_testBrowserUrl;
    
public:
    // Test mode accessors
    const std::string& getTestBrowserUrl() const { return m_testBrowserUrl; }
    bool hasTestBrowserUrl() const { return !m_testBrowserUrl.empty(); }
};

} // namespace streamlumo

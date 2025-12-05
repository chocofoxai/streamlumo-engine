// streamlumo-engine/src/config.cpp
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo / Intelli-SAAS

#include "config.h"

#include <iostream>
#include <cstring>
#include <cstdlib>

namespace streamlumo {

Config::Config() = default;

bool Config::parseArgs(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        // Help
        if (arg == "-h" || arg == "--help") {
            printHelp();
            return false;
        }
        
        // Version
        if (arg == "-v" || arg == "--version") {
            printVersion();
            return false;
        }
        
        // Quiet mode
        if (arg == "-q" || arg == "--quiet") {
            m_quiet = true;
            continue;
        }
        
        // WebSocket port
        if ((arg == "-p" || arg == "--port" || arg == "--websocket-port") && i + 1 < argc) {
            m_websocketPort = std::atoi(argv[++i]);
            if (m_websocketPort <= 0 || m_websocketPort > 65535) {
                std::cerr << "Error: Invalid port number" << std::endl;
                return false;
            }
            continue;
        }
        
        // WebSocket password
        if ((arg == "--password" || arg == "--websocket-password") && i + 1 < argc) {
            m_websocketPassword = argv[++i];
            continue;
        }
        
        // Resolution
        if ((arg == "-r" || arg == "--resolution") && i + 1 < argc) {
            std::string res = argv[++i];
            if (sscanf(res.c_str(), "%dx%d", &m_width, &m_height) != 2) {
                std::cerr << "Error: Invalid resolution format. Use WIDTHxHEIGHT (e.g., 1920x1080)" << std::endl;
                return false;
            }
            continue;
        }
        
        // FPS
        if ((arg == "-f" || arg == "--fps") && i + 1 < argc) {
            m_fps = std::atoi(argv[++i]);
            if (m_fps <= 0 || m_fps > 120) {
                std::cerr << "Error: Invalid FPS (must be 1-120)" << std::endl;
                return false;
            }
            continue;
        }
        
        // Config path
        if ((arg == "-c" || arg == "--config" || arg == "--config-path") && i + 1 < argc) {
            m_configPath = argv[++i];
            continue;
        }
        
        // Plugin path
        if (arg == "--plugin-path" && i + 1 < argc) {
            m_pluginPath = argv[++i];
            continue;
        }
        
        // Data path
        if (arg == "--data-path" && i + 1 < argc) {
            m_dataPath = argv[++i];
            continue;
        }
        
        // Log level
        if ((arg == "-l" || arg == "--log-level") && i + 1 < argc) {
            std::string level = argv[++i];
            if (level == "debug") {
                m_logLevel = LogLevel::Debug;
            } else if (level == "info") {
                m_logLevel = LogLevel::Info;
            } else if (level == "warn" || level == "warning") {
                m_logLevel = LogLevel::Warning;
            } else if (level == "error") {
                m_logLevel = LogLevel::Error;
            } else {
                std::cerr << "Error: Invalid log level. Use: debug, info, warn, error" << std::endl;
                return false;
            }
            continue;
        }
        
        // Log file
        if (arg == "--log-file" && i + 1 < argc) {
            m_logFile = argv[++i];
            continue;
        }
        
        // Unknown argument
        std::cerr << "Error: Unknown argument: " << arg << std::endl;
        std::cerr << "Use --help for usage information" << std::endl;
        return false;
    }
    
    return true;
}

void Config::printHelp() const {
    std::cout << "StreamLumo Engine v" << STREAMLUMO_ENGINE_VERSION << "\n";
    std::cout << "Headless OBS Server for StreamLumo\n\n";
    
    std::cout << "USAGE:\n";
    std::cout << "  streamlumo-engine [OPTIONS]\n\n";
    
    std::cout << "OPTIONS:\n";
    std::cout << "  -h, --help                    Show this help message\n";
    std::cout << "  -v, --version                 Show version information\n";
    std::cout << "  -q, --quiet                   Suppress banner output\n\n";
    
    std::cout << "  -p, --port <PORT>             WebSocket server port (default: 4466)\n";
    std::cout << "      --websocket-password <PW> WebSocket authentication password\n\n";
    
    std::cout << "  -r, --resolution <WxH>        Output resolution (default: 1920x1080)\n";
    std::cout << "  -f, --fps <FPS>               Output framerate (default: 30)\n\n";
    
    std::cout << "  -c, --config-path <PATH>      Path to OBS config directory\n";
    std::cout << "      --plugin-path <PATH>      Path to OBS plugins directory\n";
    std::cout << "      --data-path <PATH>        Path to OBS data directory\n\n";
    
    std::cout << "  -l, --log-level <LEVEL>       Log level: debug, info, warn, error\n";
    std::cout << "      --log-file <PATH>         Log to file instead of stdout\n\n";
    
    std::cout << "EXAMPLES:\n";
    std::cout << "  streamlumo-engine --port 4466 --resolution 1920x1080 --fps 30\n";
    std::cout << "  streamlumo-engine -p 4455 -r 1280x720 -f 60 --log-level debug\n\n";
    
    std::cout << "For more information, visit: https://github.com/Intelli-SAAS/streamlumo-engine\n";
}

void Config::printVersion() const {
    std::cout << "StreamLumo Engine v" << STREAMLUMO_ENGINE_VERSION << "\n";
    std::cout << "Licensed under GPL-2.0\n";
    std::cout << "Based on OBS Studio (https://obsproject.com)\n";
}

} // namespace streamlumo

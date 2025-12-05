// streamlumo-engine/src/main.cpp
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo / Intelli-SAAS
//
// This file is part of streamlumo-engine, a headless OBS server for StreamLumo.
// streamlumo-engine is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.

#include "engine.h"
#include "config.h"
#include "logging.h"

#include <csignal>
#include <iostream>
#include <atomic>

static std::atomic<bool> g_running{true};
static streamlumo::Engine* g_engine = nullptr;

void signal_handler(int sig) {
    log_info("Received signal %d, initiating shutdown...", sig);
    g_running = false;
    
    if (g_engine) {
        g_engine->requestShutdown();
    }
}

void print_banner() {
    std::cout << R"(
  _____ _                            _                         
 / ____| |                          | |                        
| (___ | |_ _ __ ___  __ _ _ __ ___ | |    _   _ _ __ ___   ___ 
 \___ \| __| '__/ _ \/ _` | '_ ` _ \| |   | | | | '_ ` _ \ / _ \
 ____) | |_| | |  __/ (_| | | | | | | |___| |_| | | | | | | (_) |
|_____/ \__|_|  \___|\__,_|_| |_| |_|______\__,_|_| |_| |_|\___/ 
                                                                 
    )" << std::endl;
    std::cout << "StreamLumo Engine v" << STREAMLUMO_ENGINE_VERSION << std::endl;
    std::cout << "Headless OBS Server - Licensed under GPL-2.0" << std::endl;
    std::cout << "================================================\n" << std::endl;
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    streamlumo::Config config;
    if (!config.parseArgs(argc, argv)) {
        return 0;  // --help was shown or invalid args
    }
    
    // Initialize logging
    streamlumo::Logging::init(config.getLogLevel(), config.getLogFile());
    
    // Print banner
    if (!config.isQuiet()) {
        print_banner();
    }
    
    log_info("Starting StreamLumo Engine...");
    log_info("WebSocket port: %d", config.getWebSocketPort());
    log_info("Resolution: %dx%d @ %d fps", 
             config.getWidth(), config.getHeight(), config.getFPS());
    
    // Set up signal handlers for graceful shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#ifndef _WIN32
    std::signal(SIGHUP, signal_handler);
#endif
    
    // Create and initialize the engine
    streamlumo::Engine engine(config);
    g_engine = &engine;
    
    if (!engine.initialize()) {
        log_error("Failed to initialize engine");
        return 1;
    }
    
    log_info("Engine initialized successfully");
    log_info("WebSocket server ready on port %d", config.getWebSocketPort());
    log_info("Waiting for connections...");
    
    // Run the main event loop
    int exitCode = engine.run(g_running);
    
    // Cleanup
    log_info("Shutting down...");
    engine.shutdown();
    
    log_info("Shutdown complete");
    streamlumo::Logging::shutdown();
    
    g_engine = nullptr;
    return exitCode;
}

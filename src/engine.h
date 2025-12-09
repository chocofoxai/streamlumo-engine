// streamlumo-engine/src/engine.h
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo

#pragma once

#include "config.h"
#include <atomic>
#include <memory>

// Forward declarations for OBS types
struct obs_video_info;
struct obs_audio_info;
#include "browser_helper_launcher.h"
#include "browser_helper_client.h"

namespace streamlumo {

/**
 * @brief Main engine class that wraps libobs functionality
 * 
 * This class provides a headless OBS server that:
 * - Initializes libobs without UI
 * - Loads plugins (including obs-websocket for remote control)
 * - Manages scenes, sources, and outputs
 * - Runs an event loop for processing
 */
class Engine {
public:
    explicit Engine(const Config& config);
    ~Engine();
    
    // Disable copy
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    
    /**
     * @brief Initialize the OBS engine
     * @return true if initialization succeeded
     */
    bool initialize();
    
    /**
     * @brief Run the main event loop
     * @param running Atomic bool that controls the loop
     * @return Exit code (0 for success)
     */
    int run(std::atomic<bool>& running);
    
    /**
     * @brief Request graceful shutdown
     */
    void requestShutdown();
    
    /**
     * @brief Perform cleanup and shutdown
     */
    void shutdown();
    
    /**
     * @brief Check if engine is running
     */
    bool isRunning() const { return m_initialized && !m_shutdownRequested; }
    
private:
    // Initialization steps
    bool initOBS();
    bool initVideo();
    bool initAudio();
    bool loadModules();
    bool setupDefaultScene();
    bool setupDefaultTransition();
    
    // Path helpers
    std::string getPluginPath() const;
    std::string getDataPath() const;
    std::string getModuleConfigPath() const;
    
    // Configuration
    const Config& m_config;
    
    // State
    bool m_initialized = false;
    std::atomic<bool> m_shutdownRequested{false};
    
    // Test mode browser source
    void* m_testBrowserSource = nullptr;  // obs_source_t*
    bool createTestBrowserSource(const std::string& url);

#ifdef STREAMLUMO_ENABLE_BROWSER_HELPER
    BrowserHelperLauncher m_browserHelper;
    std::unique_ptr<BrowserHelperClient> m_browserHelperClient;
    int m_helperPort = 4777;
    std::string m_helperToken;
    std::chrono::steady_clock::time_point m_lastHelperPing;
    std::string m_helperBundlePath;
#endif
};

} // namespace streamlumo

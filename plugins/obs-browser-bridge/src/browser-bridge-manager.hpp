/**
 * @file browser-bridge-manager.hpp
 * @brief Singleton manager for CEF browser helper communication
 * 
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 StreamLumo
 *
 * ## Overview
 * 
 * The BrowserBridgeManager is a singleton that:
 * 1. Launches/manages the browser-helper process
 * 2. Maintains the TCP IPC connection on port 4777
 * 3. Routes frame data to the correct browser source instances
 * 
 * ## Authentication
 * 
 * The manager reads the authentication token from the BROWSER_HELPER_TOKEN
 * environment variable and includes it in all IPC messages. Without the
 * correct token, the helper rejects commands with "unauthorized" errors.
 * 
 * ## Thread Safety
 * 
 * All public methods are thread-safe via m_mutex. The IPC client runs
 * its own receive thread for non-blocking frame delivery.
 * 
 * ## Usage
 * 
 * ```cpp
 * auto& mgr = BrowserBridgeManager::instance();
 * mgr.registerSource("browser_123", this);
 * mgr.initBrowser("browser_123", "https://example.com", 1920, 1080, 30);
 * // Frames arrive via BrowserBridgeSource::receiveFrame()
 * mgr.disposeBrowser("browser_123");
 * mgr.unregisterSource("browser_123");
 * ```
 * 
 * @see IPCClient for TCP communication
 * @see BrowserBridgeSource for OBS source integration
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace browser_bridge {

// Frame data delivered to sources
struct FrameData {
    int width;
    int height;
    std::vector<uint8_t> bgra; // BGRA pixel data
};

class BrowserBridgeSource;  // Forward declaration
class IPCClient;             // Forward declaration

class BrowserBridgeManager {
public:
    // Singleton access
    static BrowserBridgeManager &instance();

    // Non-copyable
    BrowserBridgeManager(const BrowserBridgeManager &) = delete;
    BrowserBridgeManager &operator=(const BrowserBridgeManager &) = delete;

    // Initialize the manager (called on first browser source creation)
    bool ensureInitialized();

    // Shutdown (called on plugin unload)
    void shutdown();

    // Source registration (for frame routing)
    void registerSource(const std::string &browserId, BrowserBridgeSource *source);
    void unregisterSource(const std::string &browserId);

    // Browser instance management (called by sources)
    bool initBrowser(const std::string &browserId, const std::string &url,
                     int width, int height, int fps);
    bool updateBrowser(const std::string &browserId, const std::string &url,
                       int width, int height);
    void disposeBrowser(const std::string &browserId);

    // Check if helper is running
    bool isHelperRunning() const;

private:
    BrowserBridgeManager();
    ~BrowserBridgeManager();

    // Helper process management
    bool launchHelper();
    void stopHelper();
    std::string resolveHelperPath();

    // Frame routing
    void dispatchFrame(const std::string &browserId, const uint8_t *data,
                       size_t size, int width, int height);

    // State
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_running{false};
    std::mutex m_mutex;

    // Helper process
#ifdef _WIN32
    void *m_helperProcess{nullptr}; // HANDLE on Windows
#else
    pid_t m_helperPid{-1};
#endif
    uint16_t m_port{4777};
    std::string m_helperPath;

    // IPC
    std::unique_ptr<IPCClient> m_ipcClient;
    
    /**
     * Authentication token from BROWSER_HELPER_TOKEN environment variable.
     * 
     * This token is:
     * 1. Read during ensureInitialized()
     * 2. Sent in the handshake after connecting
     * 3. Included in ALL subsequent IPC commands
     * 
     * Without a valid token, the helper rejects commands with "unauthorized".
     */
    std::string m_authToken;

    // Registered browser sources (for frame routing)
    // Key is browserId, value is the source pointer
    std::unordered_map<std::string, BrowserBridgeSource *> m_sources;
};

} // namespace browser_bridge

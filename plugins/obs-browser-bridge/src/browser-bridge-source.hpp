// obs-browser-bridge/src/browser-bridge-source.hpp
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo
//
// OBS source implementation for browser rendering via external helper process

#pragma once

#include <obs-module.h>
#include <string>
#include <mutex>
#include <atomic>
#include <vector>
#include <cstdint>
#include <memory>

// Forward declaration
namespace browser_bridge {
    class BrowserShmReader;
}

namespace browser_bridge {

/**
 * BrowserBridgeSource - OBS video source that renders web content
 * 
 * This source communicates with an external browser-helper process via IPC.
 * The helper uses CEF to render web pages and sends frame data back.
 * 
 * Properties:
 *   - url: The URL to render
 *   - width: Browser viewport width
 *   - height: Browser viewport height
 *   - css: Custom CSS to inject
 *   - shutdown_on_hidden: Stop rendering when source is hidden
 *   - restart_on_active: Restart browser when source becomes active
 *   - fps: Target frame rate (default 30)
 */
class BrowserBridgeSource {
public:
    // OBS source info registration
    static obs_source_info *getSourceInfo();

    // Static callbacks for OBS
    static const char *getName(void *unused);
    static void *create(obs_data_t *settings, obs_source_t *source);
    static void destroy(void *data);
    static void update(void *data, obs_data_t *settings);
    static void getDefaults(obs_data_t *settings);
    static obs_properties_t *getProperties(void *data);
    static uint32_t getWidth(void *data);
    static uint32_t getHeight(void *data);
    static void videoTick(void *data, float seconds);
    static void videoRender(void *data, gs_effect_t *effect);
    static void activate(void *data);
    static void deactivate(void *data);
    static void show(void *data);
    static void hide(void *data);

private:
    BrowserBridgeSource(obs_data_t *settings, obs_source_t *source);
    ~BrowserBridgeSource();

    // Internal methods
    void applySettings(obs_data_t *settings);
    void initBrowser();
    void disposeBrowser();
    void receiveFrame(const uint8_t *data, size_t size, int width, int height);
    void updateTexture();
    void updateTextureFromShm();
    void onConnectionEstablished();
    void onConnectionLost();

    // Friend for frame callback
    friend class BrowserBridgeManager;

private:
    obs_source_t *m_source = nullptr;
    std::string m_browserId;
    
    // Properties
    std::string m_url;
    int m_width = 1280;
    int m_height = 720;
    std::string m_css;
    bool m_shutdownOnHidden = false;
    bool m_restartOnActive = false;
    int m_fps = 30;
    
    // Frame buffer (double-buffered for smooth updates)
    std::mutex m_frameMutex;
    std::vector<uint8_t> m_frameData[2];  // Double buffer
    int m_writeBuffer = 0;  // Current write buffer index
    int m_readBuffer = 0;   // Current read buffer index  
    int m_frameWidth = 0;
    int m_frameHeight = 0;
    std::atomic<bool> m_frameReady{false};
    std::atomic<bool> m_newFrameAvailable{false};
    
    // OBS texture
    gs_texture_t *m_texture = nullptr;
    int m_textureWidth = 0;
    int m_textureHeight = 0;
    
    // State
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_visible{true};
    std::atomic<bool> m_browserInitialized{false};
    std::atomic<bool> m_pendingInit{false};
    
    // Shared memory reader (zero-copy frame transport)
    std::unique_ptr<BrowserShmReader> m_shmReader;
    std::atomic<bool> m_useShmTransport{true};  // Enable SHM by default
    std::vector<uint8_t> m_shmFrameBuffer;      // Local buffer for SHM reads
};

} // namespace browser_bridge

// C-linkage registration function for OBS
extern "C" void browser_bridge_source_register();

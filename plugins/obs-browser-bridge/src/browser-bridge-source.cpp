// obs-browser-bridge/src/browser-bridge-source.cpp
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo
//
// OBS source implementation

#include "browser-bridge-source.hpp"
#include "browser-bridge-manager.hpp"
#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/platform.h>
#include <random>
#include <sstream>
#include <iomanip>

namespace browser_bridge {

// Static source info instance
static obs_source_info s_sourceInfo = {};

// Generate unique browser ID
static std::string generateBrowserId() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    
    std::stringstream ss;
    ss << "browser_" << std::hex << std::setfill('0') << std::setw(16) << dis(gen);
    return ss.str();
}

// ============================================================================
// OBS Source Info Registration
// ============================================================================

obs_source_info *BrowserBridgeSource::getSourceInfo() {
    s_sourceInfo.id = "browser_bridge_source";
    s_sourceInfo.type = OBS_SOURCE_TYPE_INPUT;
    s_sourceInfo.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW |
                                 OBS_SOURCE_INTERACTION | OBS_SOURCE_DO_NOT_DUPLICATE;
    s_sourceInfo.get_name = getName;
    s_sourceInfo.create = create;
    s_sourceInfo.destroy = destroy;
    s_sourceInfo.update = update;
    s_sourceInfo.get_defaults = getDefaults;
    s_sourceInfo.get_properties = getProperties;
    s_sourceInfo.get_width = getWidth;
    s_sourceInfo.get_height = getHeight;
    s_sourceInfo.video_tick = videoTick;
    s_sourceInfo.video_render = videoRender;
    s_sourceInfo.activate = activate;
    s_sourceInfo.deactivate = deactivate;
    s_sourceInfo.show = show;
    s_sourceInfo.hide = hide;
    
    return &s_sourceInfo;
}

// ============================================================================
// Static Callbacks
// ============================================================================

const char *BrowserBridgeSource::getName(void *) {
    return obs_module_text("BrowserSource");
}

void *BrowserBridgeSource::create(obs_data_t *settings, obs_source_t *source) {
    return new BrowserBridgeSource(settings, source);
}

void BrowserBridgeSource::destroy(void *data) {
    delete static_cast<BrowserBridgeSource *>(data);
}

void BrowserBridgeSource::update(void *data, obs_data_t *settings) {
    auto *self = static_cast<BrowserBridgeSource *>(data);
    self->applySettings(settings);
}

void BrowserBridgeSource::getDefaults(obs_data_t *settings) {
    obs_data_set_default_string(settings, "url", "https://example.com");
    obs_data_set_default_int(settings, "width", 1280);
    obs_data_set_default_int(settings, "height", 720);
    obs_data_set_default_string(settings, "css", "");
    obs_data_set_default_bool(settings, "shutdown_on_hidden", false);
    obs_data_set_default_bool(settings, "restart_on_active", false);
    obs_data_set_default_int(settings, "fps", 30);
}

obs_properties_t *BrowserBridgeSource::getProperties(void *) {
    obs_properties_t *props = obs_properties_create();
    
    obs_properties_add_text(props, "url", obs_module_text("URL"), OBS_TEXT_DEFAULT);
    obs_properties_add_int(props, "width", obs_module_text("Width"), 1, 8192, 1);
    obs_properties_add_int(props, "height", obs_module_text("Height"), 1, 8192, 1);
    obs_properties_add_text(props, "css", obs_module_text("CustomCSS"), OBS_TEXT_MULTILINE);
    obs_properties_add_bool(props, "shutdown_on_hidden", obs_module_text("ShutdownOnHidden"));
    obs_properties_add_bool(props, "restart_on_active", obs_module_text("RestartOnActive"));
    obs_properties_add_int(props, "fps", obs_module_text("FPS"), 1, 120, 1);
    
    return props;
}

uint32_t BrowserBridgeSource::getWidth(void *data) {
    auto *self = static_cast<BrowserBridgeSource *>(data);
    return self->m_width;
}

uint32_t BrowserBridgeSource::getHeight(void *data) {
    auto *self = static_cast<BrowserBridgeSource *>(data);
    return self->m_height;
}

void BrowserBridgeSource::videoTick(void *data, float) {
    auto *self = static_cast<BrowserBridgeSource *>(data);
    
    // Check if we need to create/update texture
    if (self->m_frameReady.load()) {
        self->updateTexture();
    }
    
    // Handle pending browser initialization
    if (self->m_pendingInit.load()) {
        self->initBrowser();
        self->m_pendingInit.store(false);
    }
}

void BrowserBridgeSource::videoRender(void *data, gs_effect_t *effect) {
    auto *self = static_cast<BrowserBridgeSource *>(data);
    
    if (!self->m_texture) {
        return;
    }
    
    // Use default effect if none provided
    gs_effect_t *useEffect = effect;
    if (!useEffect) {
        useEffect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    }
    
    gs_eparam_t *image = gs_effect_get_param_by_name(useEffect, "image");
    gs_effect_set_texture(image, self->m_texture);
    
    while (gs_effect_loop(useEffect, "Draw")) {
        gs_draw_sprite(self->m_texture, 0, self->m_width, self->m_height);
    }
}

void BrowserBridgeSource::activate(void *data) {
    auto *self = static_cast<BrowserBridgeSource *>(data);
    self->m_active.store(true);
    
    if (self->m_restartOnActive && !self->m_browserInitialized.load()) {
        self->m_pendingInit.store(true);
    }
}

void BrowserBridgeSource::deactivate(void *data) {
    auto *self = static_cast<BrowserBridgeSource *>(data);
    self->m_active.store(false);
}

void BrowserBridgeSource::show(void *data) {
    auto *self = static_cast<BrowserBridgeSource *>(data);
    self->m_visible.store(true);
    
    if (!self->m_browserInitialized.load()) {
        self->m_pendingInit.store(true);
    }
}

void BrowserBridgeSource::hide(void *data) {
    auto *self = static_cast<BrowserBridgeSource *>(data);
    self->m_visible.store(false);
    
    if (self->m_shutdownOnHidden && self->m_browserInitialized.load()) {
        self->disposeBrowser();
    }
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

BrowserBridgeSource::BrowserBridgeSource(obs_data_t *settings, obs_source_t *source)
    : m_source(source)
    , m_browserId(generateBrowserId())
{
    applySettings(settings);
    
    // Register with manager
    BrowserBridgeManager::instance().registerSource(m_browserId, this);
    
    // Initialize browser on first show
    m_pendingInit.store(true);
}

BrowserBridgeSource::~BrowserBridgeSource() {
    // Dispose browser
    if (m_browserInitialized.load()) {
        disposeBrowser();
    }
    
    // Unregister from manager
    BrowserBridgeManager::instance().unregisterSource(m_browserId);
    
    // Clean up texture on graphics thread
    obs_enter_graphics();
    if (m_texture) {
        gs_texture_destroy(m_texture);
        m_texture = nullptr;
    }
    obs_leave_graphics();
}

// ============================================================================
// Internal Methods
// ============================================================================

void BrowserBridgeSource::applySettings(obs_data_t *settings) {
    std::string newUrl = obs_data_get_string(settings, "url");
    int newWidth = static_cast<int>(obs_data_get_int(settings, "width"));
    int newHeight = static_cast<int>(obs_data_get_int(settings, "height"));
    std::string newCss = obs_data_get_string(settings, "css");
    bool newShutdownOnHidden = obs_data_get_bool(settings, "shutdown_on_hidden");
    bool newRestartOnActive = obs_data_get_bool(settings, "restart_on_active");
    int newFps = static_cast<int>(obs_data_get_int(settings, "fps"));
    
    // Check if we need to recreate the browser
    bool needsRecreate = m_browserInitialized.load() &&
                         (newUrl != m_url || newWidth != m_width || 
                          newHeight != m_height || newCss != m_css);
    
    m_url = newUrl;
    m_width = newWidth;
    m_height = newHeight;
    m_css = newCss;
    m_shutdownOnHidden = newShutdownOnHidden;
    m_restartOnActive = newRestartOnActive;
    m_fps = newFps;
    
    if (needsRecreate) {
        disposeBrowser();
        m_pendingInit.store(true);
    }
}

void BrowserBridgeSource::initBrowser() {
    if (m_browserInitialized.load()) {
        return;
    }
    
    blog(LOG_INFO, "[browser-bridge] Initializing browser: id=%s url=%s size=%dx%d",
         m_browserId.c_str(), m_url.c_str(), m_width, m_height);
    
    // Send init command to helper
    bool success = BrowserBridgeManager::instance().initBrowser(
        m_browserId, m_url, m_width, m_height, m_fps);
    
    if (success) {
        m_browserInitialized.store(true);
    } else {
        blog(LOG_WARNING, "[browser-bridge] Failed to initialize browser: %s", m_browserId.c_str());
    }
}

void BrowserBridgeSource::disposeBrowser() {
    if (!m_browserInitialized.load()) {
        return;
    }
    
    blog(LOG_INFO, "[browser-bridge] Disposing browser: %s", m_browserId.c_str());
    
    BrowserBridgeManager::instance().disposeBrowser(m_browserId);
    m_browserInitialized.store(false);
}

void BrowserBridgeSource::receiveFrame(const uint8_t *data, size_t size, int width, int height) {
    // Copy frame data to buffer
    std::lock_guard<std::mutex> lock(m_frameMutex);
    
    m_frameData.assign(data, data + size);
    m_frameWidth = width;
    m_frameHeight = height;
    m_frameReady.store(true);
}

void BrowserBridgeSource::updateTexture() {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    
    if (!m_frameReady.load() || m_frameData.empty()) {
        return;
    }
    
    obs_enter_graphics();
    
    // Recreate texture if size changed
    if (!m_texture || m_textureWidth != m_frameWidth || m_textureHeight != m_frameHeight) {
        if (m_texture) {
            gs_texture_destroy(m_texture);
        }
        
        m_texture = gs_texture_create(m_frameWidth, m_frameHeight, GS_BGRA, 1, nullptr, GS_DYNAMIC);
        m_textureWidth = m_frameWidth;
        m_textureHeight = m_frameHeight;
        
        blog(LOG_DEBUG, "[browser-bridge] Created texture: %dx%d", m_frameWidth, m_frameHeight);
    }
    
    // Update texture data
    if (m_texture && m_frameData.size() == static_cast<size_t>(m_frameWidth * m_frameHeight * 4)) {
        gs_texture_set_image(m_texture, m_frameData.data(), m_frameWidth * 4, false);
    }
    
    obs_leave_graphics();
    
    m_frameReady.store(false);
}

void BrowserBridgeSource::onConnectionEstablished() {
    // Re-initialize browser if it was previously active
    if (m_browserInitialized.load()) {
        m_pendingInit.store(true);
    }
}

void BrowserBridgeSource::onConnectionLost() {
    m_browserInitialized.store(false);
}

} // namespace browser_bridge

// ============================================================================
// C Registration Function
// ============================================================================

extern "C" void browser_bridge_source_register() {
    obs_register_source(browser_bridge::BrowserBridgeSource::getSourceInfo());
}

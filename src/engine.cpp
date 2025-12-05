// streamlumo-engine/src/engine.cpp
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo / Intelli-SAAS

#include "engine.h"
#include "logging.h"
#include "frontend-stubs.h"

#include <obs.h>
#include <obs-module.h>

#include <thread>
#include <chrono>
#include <filesystem>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <libgen.h>
#endif

namespace fs = std::filesystem;

namespace streamlumo {

Engine::Engine(const Config& config) 
    : m_config(config) 
{
}

Engine::~Engine() {
    if (m_initialized) {
        shutdown();
    }
}

bool Engine::initialize() {
    log_info("Initializing StreamLumo Engine...");
    
    if (!initOBS()) {
        log_error("Failed to initialize OBS core");
        return false;
    }
    
    if (!initVideo()) {
        log_error("Failed to initialize video subsystem");
        return false;
    }
    
    if (!initAudio()) {
        log_error("Failed to initialize audio subsystem");
        return false;
    }
    
    if (!loadModules()) {
        log_error("Failed to load OBS modules");
        return false;
    }
    
    if (!setupDefaultScene()) {
        log_warn("Failed to setup default scene (non-fatal)");
    }
    
    // Signal that OBS has finished loading - this enables obs-websocket to accept requests
    HeadlessFrontend* frontend = HeadlessFrontend::instance();
    if (frontend) {
        frontend->signalFinishedLoading();
    }
    
    m_initialized = true;
    return true;
}

bool Engine::initOBS() {
    log_info("Initializing OBS core...");
    
    // Set up module paths before startup
    std::string pluginPath = getPluginPath();
    std::string dataPath = getDataPath();
    
    log_info("Plugin path: %s", pluginPath.c_str());
    log_info("Data path: %s", dataPath.c_str());
    
    // Get config path
    std::string configPath = m_config.getConfigPath();
    if (configPath.empty()) {
        configPath = getModuleConfigPath();
    }
    log_info("Config path: %s", configPath.c_str());
    
    // Initialize OBS
    if (!obs_startup("en-US", configPath.c_str(), nullptr)) {
        log_error("obs_startup() failed");
        return false;
    }
    
    // Add module search paths AFTER obs_startup
    // On macOS, plugins are in .plugin bundles with structure:
    // <base>/%module%.plugin/Contents/MacOS/%module%
    // <base>/%module%.plugin/Contents/Resources/
    if (!pluginPath.empty() && fs::exists(pluginPath)) {
        // Format for macOS .plugin bundles
        std::string binPath = pluginPath + "/%module%.plugin/Contents/MacOS/";
        std::string dataModulePath = pluginPath + "/%module%.plugin/Contents/Resources/";
        log_info("Adding module path: bin=%s, data=%s", binPath.c_str(), dataModulePath.c_str());
        obs_add_module_path(binPath.c_str(), dataModulePath.c_str());
    }
    
    log_info("OBS core initialized (version %s)", obs_get_version_string());
    return true;
}

bool Engine::initVideo() {
    log_info("Initializing video subsystem...");
    
    struct obs_video_info ovi = {};
    
    // Graphics module - use OpenGL on macOS
#ifdef __APPLE__
    ovi.graphics_module = "libobs-opengl";
#else
    ovi.graphics_module = "libobs-opengl";  // Or D3D11 on Windows
#endif
    
    // Frame rate
    ovi.fps_num = m_config.getFPS();
    ovi.fps_den = 1;
    
    // Resolution
    ovi.base_width = m_config.getWidth();
    ovi.base_height = m_config.getHeight();
    ovi.output_width = m_config.getWidth();
    ovi.output_height = m_config.getHeight();
    
    // Format
    ovi.output_format = VIDEO_FORMAT_NV12;
    ovi.colorspace = VIDEO_CS_709;
    ovi.range = VIDEO_RANGE_PARTIAL;
    
    // GPU settings
    ovi.adapter = 0;
    ovi.gpu_conversion = true;
    ovi.scale_type = OBS_SCALE_BICUBIC;
    
    int result = obs_reset_video(&ovi);
    
    if (result != OBS_VIDEO_SUCCESS) {
        const char* errorMsg;
        switch (result) {
            case OBS_VIDEO_MODULE_NOT_FOUND:
                errorMsg = "Graphics module not found";
                break;
            case OBS_VIDEO_NOT_SUPPORTED:
                errorMsg = "Graphics not supported";
                break;
            case OBS_VIDEO_INVALID_PARAM:
                errorMsg = "Invalid parameters";
                break;
            case OBS_VIDEO_CURRENTLY_ACTIVE:
                errorMsg = "Video already active";
                break;
            case OBS_VIDEO_FAIL:
            default:
                errorMsg = "Unknown error";
                break;
        }
        log_error("obs_reset_video() failed: %s (code: %d)", errorMsg, result);
        return false;
    }
    
    log_info("Video initialized: %dx%d @ %d fps", 
             m_config.getWidth(), m_config.getHeight(), m_config.getFPS());
    return true;
}

bool Engine::initAudio() {
    log_info("Initializing audio subsystem...");
    
    struct obs_audio_info oai = {};
    oai.samples_per_sec = 48000;
    oai.speakers = SPEAKERS_STEREO;
    
    if (!obs_reset_audio(&oai)) {
        log_error("obs_reset_audio() failed");
        return false;
    }
    
    log_info("Audio initialized: 48kHz stereo");
    return true;
}

// Callback for enumerating found modules
static void log_found_module(void* param, const struct obs_module_info2* info) {
    (void)param;
    log_info("  Found module: %s", info->name);
    log_info("    bin_path: %s", info->bin_path);
    log_info("    data_path: %s", info->data_path);
}

// List of modules to skip in headless mode (they require Qt GUI or are not needed)
static const char* headless_skip_modules[] = {
    "frontend-tools",      // Requires Qt GUI
    "decklink-output-ui",  // Requires Qt GUI
    "decklink-captions",   // Requires Decklink hardware
    "decklink",            // Requires Decklink hardware
    "obs-vst",             // VST plugins typically need GUI
    // "mac-virtualcam",   // Virtual camera output - trying to enable
    // "obs-browser",      // Browser sources - trying to enable
    nullptr
};

static bool should_skip_module(const char* name) {
    for (int i = 0; headless_skip_modules[i] != nullptr; i++) {
        if (strcmp(name, headless_skip_modules[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool Engine::loadModules() {
    log_info("Loading OBS modules...");
    
    // Install headless frontend stubs BEFORE loading modules
    // This provides the obs_frontend_* API that plugins like obs-websocket need
#ifdef HAS_FRONTEND_API
    log_info("Installing headless frontend stubs...");
    HeadlessFrontend::install();
#endif
    
    // First, enumerate what modules can be found
    log_info("Searching for modules in registered paths...");
    obs_find_modules2(log_found_module, nullptr);
    
    // Mark modules to skip in headless mode
    for (int i = 0; headless_skip_modules[i] != nullptr; i++) {
        log_info("Disabling module for headless mode: %s", headless_skip_modules[i]);
        obs_add_disabled_module(headless_skip_modules[i]);
    }
    
    // Load all modules from the search paths
    obs_load_all_modules();
    
    // Log what was loaded
    obs_log_loaded_modules();
    
    // Post-load initialization
    obs_post_load_modules();
    
    log_info("Modules loaded successfully");
    return true;
}

bool Engine::setupDefaultScene() {
    log_info("Setting up default scene...");
    
    // Create a default scene
    obs_scene_t* scene = obs_scene_create("StreamLumo Default");
    if (!scene) {
        log_warn("Failed to create default scene");
        return false;
    }
    
    // Set as the current scene
    obs_source_t* sceneSource = obs_scene_get_source(scene);
    obs_set_output_source(0, sceneSource);
    
    // Release our reference (output source holds a reference now)
    obs_scene_release(scene);
    
    log_info("Default scene created");
    return true;
}

int Engine::run(std::atomic<bool>& running) {
    log_info("Entering main event loop...");
    
    while (running && !m_shutdownRequested) {
        // The obs-websocket plugin handles its own event loop for WebSocket connections
        // We just need to keep the process alive and process any pending events
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    log_info("Exiting main event loop");
    return 0;
}

void Engine::requestShutdown() {
    m_shutdownRequested = true;
}

void Engine::shutdown() {
    if (!m_initialized) {
        return;
    }
    
    log_info("Shutting down engine...");
    
    // Clear output sources
    for (int i = 0; i < MAX_CHANNELS; i++) {
        obs_set_output_source(i, nullptr);
    }
    
    // Uninstall headless frontend stubs
#ifdef HAS_FRONTEND_API
    log_info("Uninstalling headless frontend stubs...");
    HeadlessFrontend::uninstall();
#endif
    
    // Shutdown OBS
    obs_shutdown();
    
    m_initialized = false;
    log_info("Engine shutdown complete");
}

std::string Engine::getPluginPath() const {
    // If explicitly set in config, use that
    if (!m_config.getPluginPath().empty()) {
        return m_config.getPluginPath();
    }
    
#ifdef __APPLE__
    // Get path relative to executable
    char exePath[PATH_MAX];
    uint32_t size = sizeof(exePath);
    
    if (_NSGetExecutablePath(exePath, &size) == 0) {
        char* dir = dirname(exePath);
        fs::path appPath = fs::path(dir).parent_path();  // Contents/
        return (appPath / "PlugIns" / "obs-plugins").string();
    }
#endif
    
    return "";
}

std::string Engine::getDataPath() const {
    // If explicitly set in config, use that
    if (!m_config.getDataPath().empty()) {
        return m_config.getDataPath();
    }
    
#ifdef __APPLE__
    char exePath[PATH_MAX];
    uint32_t size = sizeof(exePath);
    
    if (_NSGetExecutablePath(exePath, &size) == 0) {
        char* dir = dirname(exePath);
        fs::path appPath = fs::path(dir).parent_path();  // Contents/
        return (appPath / "Resources" / "obs-data").string();
    }
#endif
    
    return "";
}

std::string Engine::getModuleConfigPath() const {
#ifdef __APPLE__
    char exePath[PATH_MAX];
    uint32_t size = sizeof(exePath);
    
    if (_NSGetExecutablePath(exePath, &size) == 0) {
        char* dir = dirname(exePath);
        fs::path appPath = fs::path(dir).parent_path();  // Contents/
        return (appPath / "Resources" / "obs-config").string();
    }
#endif
    
    return "";
}

} // namespace streamlumo

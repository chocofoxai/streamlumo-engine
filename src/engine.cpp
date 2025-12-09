// streamlumo-engine/src/engine.cpp
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo

#include "engine.h"
#include "logging.h"
#include "browser_helper_launcher.h"
#include "frontend-stubs.h"
#include "platform/platform.h"

#include <random>

#include <obs.h>
#include <obs-module.h>

#include <thread>
#include <chrono>
#include <filesystem>
#include <cstdio>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <libgen.h>
#endif

namespace fs = std::filesystem;

namespace streamlumo {

static std::string generateHelperToken()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;
    uint64_t a = dist(gen);
    uint64_t b = dist(gen);
    char buf[33];
    snprintf(buf, sizeof(buf), "%016llx%016llx", static_cast<unsigned long long>(a), static_cast<unsigned long long>(b));
    return std::string(buf);
}

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

#ifdef STREAMLUMO_ENABLE_BROWSER_HELPER
    // Attempt to launch the external browser helper app on macOS to host CEF safely.
    // The helper bundle is expected at ../Helpers/streamlumo-browser-helper.app relative to the engine binary.
    char exePath[PATH_MAX];
    uint32_t size = sizeof(exePath);
    std::string helperBundlePath;
    if (_NSGetExecutablePath(exePath, &size) == 0) {
        char *dir = dirname(exePath);
        fs::path appPath = fs::path(dir).parent_path(); // Contents/
        helperBundlePath = (appPath / "Helpers" / "streamlumo-browser-helper.app").string();
    }
    if (!helperBundlePath.empty()) {
        m_helperPort = m_config.getHelperPort();
        m_helperToken = m_config.getHelperToken();
        if (m_helperToken.empty()) {
            m_helperToken = generateHelperToken();
            log_info("Generated ephemeral helper token");
        }

        // Export port/token so the helper process can read them from its environment.
        platform::setEnv("BROWSER_HELPER_PORT", std::to_string(m_helperPort));
        platform::setEnv("BROWSER_HELPER_TOKEN", m_helperToken);

        m_helperBundlePath = helperBundlePath;
        if (m_browserHelper.start(helperBundlePath)) {
            // Try to connect to the helper over local TCP JSON-line IPC.
            m_browserHelperClient = std::make_unique<BrowserHelperClient>();
            if (!m_browserHelperClient->start(static_cast<uint16_t>(m_helperPort), m_helperToken)) {
                log_warn("Helper IPC client failed to connect on port %d", m_helperPort);
            }
        } else {
            log_warn("Browser helper failed to launch; browser sources will remain unavailable.");
        }
    } else {
        log_warn("Could not resolve browser helper path; browser sources will remain unavailable.");
    }
#endif
    
    // Set environment variables for obs-websocket before loading modules
    // This follows the industry-standard pattern of fixed port + configuration
    // Port 4466 is used by default (different from OBS Studio's 4455)
    platform::setEnv("SL_WEBSOCKET_PORT", std::to_string(m_config.getWebSocketPort()));
    platform::setEnv("SL_WEBSOCKET_AUTH_DISABLED", "1"); // Disable auth for local IPC
    log_info("Set SL_WEBSOCKET_PORT=%d for obs-websocket", m_config.getWebSocketPort());
    
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
    
    if (!setupDefaultTransition()) {
        log_warn("Failed to setup default transition (non-fatal)");
    }
    
    // Signal that OBS has finished loading - this enables obs-websocket to accept requests
    HeadlessFrontend* frontend = HeadlessFrontend::instance();
    if (frontend) {
        frontend->signalFinishedLoading();
    }
    
    // If test browser URL is specified, create a test browser source
    if (m_config.hasTestBrowserUrl()) {
        if (!createTestBrowserSource(m_config.getTestBrowserUrl())) {
            log_warn("Failed to create test browser source (non-fatal)");
        }
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
        // Format for macOS .plugin bundles - standard bundled structure
        std::string binPath = pluginPath + "/%module%.plugin/Contents/MacOS/";
        std::string dataModulePath = pluginPath + "/%module%.plugin/Contents/Resources/";
        log_info("Adding module path: bin=%s, data=%s", binPath.c_str(), dataModulePath.c_str());
        obs_add_module_path(binPath.c_str(), dataModulePath.c_str());
        
        // Also add OBS build directory structure for development:
        // <base>/%module%/Release/%module%.plugin/Contents/MacOS/%module%
        // This allows loading plugins from obs-studio/build_macos/plugins/
        std::string devBinPath = pluginPath + "/%module%/Release/%module%.plugin/Contents/MacOS/";
        std::string devDataPath = pluginPath + "/%module%/Release/%module%.plugin/Contents/Resources/";
        log_info("Adding OBS dev module path: bin=%s, data=%s", devBinPath.c_str(), devDataPath.c_str());
        obs_add_module_path(devBinPath.c_str(), devDataPath.c_str());
    }
    
    // Also add path for directly loading .so plugins (like obs-browser-bridge)
    // These are simple .so files without bundle structure
#ifdef __APPLE__
    char pathBuf[PATH_MAX];
    uint32_t size = sizeof(pathBuf);
    if (_NSGetExecutablePath(pathBuf, &size) == 0) {
        fs::path engineDir = fs::path(pathBuf).parent_path();
        fs::path plugInsDir = engineDir / ".." / "PlugIns";
        if (fs::exists(plugInsDir)) {
            std::string plugInsBin = plugInsDir.string() + "/";
            std::string plugInsData = plugInsDir.string() + "/";
            log_info("Adding PlugIns path for .so modules: %s", plugInsBin.c_str());
            obs_add_module_path(plugInsBin.c_str(), plugInsData.c_str());
        }
        // Also check PlugIns next to engine for dev builds
        fs::path devPlugIns = engineDir / "PlugIns";
        if (fs::exists(devPlugIns)) {
            std::string hpBin = devPlugIns.string() + "/";
            std::string hpData = devPlugIns.string() + "/";
            log_info("Adding dev PlugIns path: %s", hpBin.c_str());
            obs_add_module_path(hpBin.c_str(), hpData.c_str());
        }
    }
#endif
    
    log_info("OBS core initialized (version %s)", obs_get_version_string());
    return true;
}

bool Engine::initVideo() {
    log_info("Initializing video subsystem...");
    
    struct obs_video_info ovi = {};
    
    // Graphics module - platform-specific
    // macOS: Must include .dylib extension (libobs appends .so otherwise)
    // Windows: libobs appends .dll automatically
    // Linux: libobs appends .so automatically
#ifdef __APPLE__
    // Use Metal on Apple Silicon for better performance
    ovi.graphics_module = "libobs-metal.dylib";
#elif defined(_WIN32)
    ovi.graphics_module = "libobs-d3d11";
#else
    ovi.graphics_module = "libobs-opengl";
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
    
    // Get the scene's source
    obs_source_t* sceneSource = obs_scene_get_source(scene);
    
    // Set as the output source (for video output pipeline)
    obs_set_output_source(0, sceneSource);
    
    // IMPORTANT: Also set as current program scene via frontend API
    // This is required for obs-websocket to return the current scene correctly
    HeadlessFrontend* frontend = HeadlessFrontend::instance();
    if (frontend) {
        frontend->obs_frontend_set_current_scene(sceneSource);
        log_info("Set default scene as current program scene");
    }
    
    // Release our reference (output source and frontend hold references now)
    obs_scene_release(scene);
    
    log_info("Default scene created");
    return true;
}

bool Engine::setupDefaultTransition() {
    log_info("Setting up default transition...");
    
    // Create a fade transition (built into OBS core)
    obs_source_t* transition = obs_source_create("fade_transition", "Fade", nullptr, nullptr);
    if (!transition) {
        log_warn("Failed to create fade transition, trying cut_transition");
        transition = obs_source_create("cut_transition", "Cut", nullptr, nullptr);
    }
    
    if (!transition) {
        log_warn("Failed to create default transition");
        return false;
    }
    
    // Set as current transition via frontend API
    HeadlessFrontend* frontend = HeadlessFrontend::instance();
    if (frontend) {
        frontend->obs_frontend_set_current_transition(transition);
        frontend->obs_frontend_set_transition_duration(300); // 300ms fade
        log_info("Set default transition: %s", obs_source_get_name(transition));
    }
    
    // Release our reference (frontend holds a reference now)
    obs_source_release(transition);
    
    return true;
}

int Engine::run(std::atomic<bool>& running) {
    log_info("Entering main event loop...");
    m_lastHelperPing = std::chrono::steady_clock::now();

    while (running && !m_shutdownRequested) {
        // The obs-websocket plugin handles its own event loop for WebSocket connections
        // We just need to keep the process alive and process any pending events

#ifdef STREAMLUMO_ENABLE_BROWSER_HELPER
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastHelperPing).count();
        if (elapsed >= 2000) { // ping every 2s
            bool healthy = false;
            if (m_browserHelperClient && m_browserHelperClient->isConnected()) {
                healthy = m_browserHelperClient->ping();
                if (!healthy) {
                    log_warn("Helper ping failed; will attempt reconnect");
                    m_browserHelperClient->stop();
                }
            }

            if (!healthy) {
                if (!m_browserHelper.checkAlive() && !m_helperBundlePath.empty()) {
                    log_warn("Helper process not alive; restarting...");
                    m_browserHelper.start(m_helperBundlePath);
                }
                if (!m_browserHelperClient) {
                    m_browserHelperClient = std::make_unique<BrowserHelperClient>();
                }
                m_browserHelperClient->start(static_cast<uint16_t>(m_helperPort), m_helperToken);
            }
            m_lastHelperPing = now;
        }
#endif

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
    
    // Release test browser source if created
    if (m_testBrowserSource) {
        obs_source_release(static_cast<obs_source_t*>(m_testBrowserSource));
        m_testBrowserSource = nullptr;
    }
    
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
    
#ifdef STREAMLUMO_ENABLE_BROWSER_HELPER
    if (m_browserHelperClient) {
        m_browserHelperClient->stop();
        m_browserHelperClient.reset();
    }
    m_browserHelper.stop();
#endif
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

bool Engine::createTestBrowserSource(const std::string& url) {
    log_info("Creating test browser source with URL: %s", url.c_str());
    
    // Get the current scene
    obs_source_t* currentScene = obs_get_output_source(0);
    if (!currentScene) {
        log_error("No output source set");
        return false;
    }
    
    obs_scene_t* scene = obs_scene_from_source(currentScene);
    if (!scene) {
        log_error("Output source is not a scene");
        obs_source_release(currentScene);
        return false;
    }
    
    // Create settings for the browser source
    obs_data_t* settings = obs_data_create();
    obs_data_set_string(settings, "url", url.c_str());
    obs_data_set_int(settings, "width", m_config.getWidth());
    obs_data_set_int(settings, "height", m_config.getHeight());
    obs_data_set_int(settings, "fps", m_config.getFPS());
    obs_data_set_string(settings, "css", "");
    obs_data_set_bool(settings, "shutdown_on_hidden", false);
    obs_data_set_bool(settings, "restart_on_active", false);
    
    // Create the browser source
    obs_source_t* browserSource = obs_source_create(
        "browser_bridge_source",  // Our custom source type
        "Test Browser Source",     // Source name
        settings,
        nullptr
    );
    
    obs_data_release(settings);
    
    if (!browserSource) {
        log_error("Failed to create browser_bridge_source - is the plugin loaded?");
        obs_source_release(currentScene);
        return false;
    }
    
    // Add to the scene
    obs_sceneitem_t* sceneItem = obs_scene_add(scene, browserSource);
    if (!sceneItem) {
        log_error("Failed to add browser source to scene");
        obs_source_release(browserSource);
        obs_source_release(currentScene);
        return false;
    }
    
    // Store reference for cleanup
    m_testBrowserSource = browserSource;
    
    log_info("Test browser source created successfully: %s", url.c_str());
    log_info("  Size: %dx%d @ %d fps", m_config.getWidth(), m_config.getHeight(), m_config.getFPS());
    
    obs_source_release(currentScene);
    return true;
}

} // namespace streamlumo

/**
 * @file browser-bridge-manager.cpp
 * @brief Manager for CEF browser helper IPC communication
 * 
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 StreamLumo
 * 
 * ## Architecture Overview
 * 
 * This file implements the BrowserBridgeManager singleton which manages communication
 * between OBS browser sources and an external CEF browser helper process.
 * 
 * ## Why External Helper?
 * 
 * OBS's built-in browser source (obs-browser) requires a full Qt-based OBS Studio
 * environment with CEF integrated directly. For headless operation, we instead:
 * 
 * 1. Run an external CEF helper process (streamlumo-browser-helper)
 * 2. Communicate via TCP JSON-line protocol on port 4777
 * 3. Receive rendered frames as base64-encoded BGRA data
 * 4. Upload frames to OBS textures for compositing
 * 
 * ## Authentication
 * 
 * The browser helper requires token-based authentication to prevent unauthorized
 * connections. The token is passed via the BROWSER_HELPER_TOKEN environment variable
 * and must be included in:
 * - Initial handshake message
 * - All subsequent IPC commands (initBrowser, disposeBrowser, etc.)
 * 
 * ## Field Naming Convention
 * 
 * IMPORTANT: The helper expects "id" (not "browserId") in all JSON messages.
 * This was a source of bugs during development - the helper returns "missing_id"
 * errors if the wrong field name is used.
 * 
 * ## Connection Strategy
 * 
 * The manager first tries to connect to an existing helper (the engine may have
 * launched one). If that fails, it launches its own helper instance. This prevents
 * duplicate helper processes and port conflicts.
 * 
 * ## Cross-Platform Notes
 * 
 * - macOS: Helper is streamlumo-browser-helper.app bundle
 * - Windows: Helper is streamlumo-browser-helper.exe (TODO)
 * - Linux: Helper is streamlumo-browser-helper binary (TODO)
 * 
 * @see IPCClient for TCP communication details
 * @see BrowserBridgeSource for OBS source implementation
 * @see FrameDecoder for base64 frame decoding
 */

#include "browser-bridge-manager.hpp"
#include "browser-bridge-source.hpp"
#include "ipc-client.hpp"
#include "frame-decoder.hpp"
#include <obs.h>
#include <sstream>
#include <filesystem>

#ifdef __APPLE__
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <mach-o/dyld.h>
extern char **environ;
#elif defined(_WIN32)
#include <windows.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace fs = std::filesystem;

namespace browser_bridge {

BrowserBridgeManager &BrowserBridgeManager::instance()
{
    static BrowserBridgeManager inst;
    return inst;
}

BrowserBridgeManager::BrowserBridgeManager()
{
    m_port = 4777;
}

BrowserBridgeManager::~BrowserBridgeManager()
{
    shutdown();
}

/**
 * Ensures the manager is initialized and connected to the browser helper.
 * 
 * This method is called lazily on first browser creation. It:
 * 1. Reads the auth token from BROWSER_HELPER_TOKEN environment variable
 * 2. Tries to connect to an existing helper (engine may have started one)
 * 3. If no helper exists, resolves the helper path and launches a new one
 * 4. Sends authentication handshake after connecting
 * 
 * ## Connection Order
 * 
 * We try connecting to an existing helper first because:
 * - The engine may have launched the helper during startup
 * - Avoids duplicate helper processes
 * - Prevents "port already in use" errors
 * 
 * @return true if connected and ready, false on failure
 */
bool BrowserBridgeManager::ensureInitialized()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_initialized.load()) {
        return m_running.load();
    }

    m_initialized.store(true);

    // Get token from environment (set by the engine)
    // The token is required for authentication with the browser helper
    const char* tokenEnv = std::getenv("BROWSER_HELPER_TOKEN");
    if (tokenEnv) {
        m_authToken = tokenEnv;
        blog(LOG_INFO, "[browser-bridge] Using token from BROWSER_HELPER_TOKEN");
    }

    // Connect IPC with frame callback
    // Frames are delivered as base64-encoded BGRA data via frameReady messages
    m_ipcClient = std::make_unique<IPCClient>();
    m_ipcClient->setFrameCallback([this](const std::string &browserId,
                                          const uint8_t *data, size_t size,
                                          int width, int height) {
        dispatchFrame(browserId, data, size, width, height);
    });

    // IMPORTANT: Try existing helper first to avoid duplicate processes
    // The engine typically launches the helper during startup and sets the token
    blog(LOG_INFO, "[browser-bridge] Trying to connect to existing helper on port %u", m_port);
    if (m_ipcClient->connect("127.0.0.1", m_port, 2000)) { // 2 second timeout
        // Send handshake with token for authentication
        // Without this, the helper will reject all subsequent commands with "unauthorized"
        if (!m_authToken.empty()) {
            m_ipcClient->sendHandshake(m_authToken);
        }
        blog(LOG_INFO, "[browser-bridge] Connected to existing helper on port %u", m_port);
        m_running.store(true);
        return true;
    }

    // No existing helper, resolve path and launch our own
    m_helperPath = resolveHelperPath();
    if (m_helperPath.empty()) {
        blog(LOG_WARNING,
             "[browser-bridge] Could not find browser helper; browser sources disabled");
        return false;
    }

    blog(LOG_INFO, "[browser-bridge] Helper path: %s", m_helperPath.c_str());

    // Launch helper
    if (!launchHelper()) {
        blog(LOG_ERROR, "[browser-bridge] Failed to launch browser helper");
        return false;
    }

    // Give helper time to start listening
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Try to connect to the helper we just launched
    if (!m_ipcClient->connect("127.0.0.1", m_port, 5000)) { // 5 second timeout
        blog(LOG_ERROR, "[browser-bridge] Failed to connect to helper on port %u",
             m_port);
        stopHelper();
        return false;
    }

    // Send handshake with token for authentication
    if (!m_authToken.empty()) {
        m_ipcClient->sendHandshake(m_authToken);
    }

    blog(LOG_INFO, "[browser-bridge] Connected to helper on port %u", m_port);

    m_running.store(true);
    return true;
}

/**
 * Shuts down the browser bridge manager.
 * 
 * Disposes all active browsers, disconnects IPC, and stops the helper process.
 * 
 * NOTE: All JSON messages use "id" field (not "browserId") to match helper protocol.
 * The token is included in dispose messages for authentication.
 */
void BrowserBridgeManager::shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized.load()) {
        return;
    }

    blog(LOG_INFO, "[browser-bridge] Shutting down");

    m_running.store(false);

    // Dispose all browsers
    // IMPORTANT: Helper expects "id" not "browserId" - this was a source of bugs
    for (auto &kv : m_sources) {
        if (m_ipcClient && m_ipcClient->isConnected()) {
            std::ostringstream ss;
            ss << "{\"type\":\"disposeBrowser\",\"id\":\"" << kv.first << "\"";
            // Include token for authentication
            if (!m_authToken.empty()) {
                ss << ",\"token\":\"" << m_authToken << "\"";
            }
            ss << "}";
            m_ipcClient->sendLine(ss.str());
        }
    }
    m_sources.clear();

    // Stop IPC
    if (m_ipcClient) {
        m_ipcClient->disconnect();
    }

    // Stop helper
    stopHelper();

    m_ipcClient.reset();
    m_initialized.store(false);

    blog(LOG_INFO, "[browser-bridge] Shutdown complete");
}

void BrowserBridgeManager::registerSource(const std::string &browserId, 
                                          BrowserBridgeSource *source)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sources[browserId] = source;
    blog(LOG_DEBUG, "[browser-bridge] Registered source: %s", browserId.c_str());
}

void BrowserBridgeManager::unregisterSource(const std::string &browserId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sources.erase(browserId);
    blog(LOG_DEBUG, "[browser-bridge] Unregistered source: %s", browserId.c_str());
}

/**
 * Initializes a browser instance in the helper process.
 * 
 * Sends an initBrowser command to create a new CEF browser with the specified
 * URL and dimensions. The helper will begin sending frameReady messages once
 * the page loads and renders.
 * 
 * ## JSON Protocol
 * 
 * Request:
 * ```json
 * {
 *   "type": "initBrowser",
 *   "id": "<browser-id>",          // NOTE: "id" not "browserId"!
 *   "url": "https://example.com",
 *   "width": 1920,
 *   "height": 1080,
 *   "fps": 30,
 *   "token": "<auth-token>"
 * }
 * ```
 * 
 * @param browserId Unique identifier for this browser instance
 * @param url Initial URL to load
 * @param width Browser width in pixels
 * @param height Browser height in pixels  
 * @param fps Frame rate for rendering
 * @return true if command sent successfully
 */
bool BrowserBridgeManager::initBrowser(const std::string &browserId,
                                        const std::string &url,
                                        int width, int height, int fps)
{
    // Ensure initialized on first browser creation
    if (!ensureInitialized()) {
        blog(LOG_ERROR, "[browser-bridge] ensureInitialized() failed");
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_running.load()) {
        blog(LOG_ERROR, "[browser-bridge] Cannot init browser - manager not running");
        return false;
    }
    
    if (!m_ipcClient) {
        blog(LOG_ERROR, "[browser-bridge] Cannot init browser - no IPC client");
        return false;
    }
    
    if (!m_ipcClient->isConnected()) {
        blog(LOG_ERROR, "[browser-bridge] Cannot init browser - IPC not connected");
        return false;
    }

    // Send initBrowser command with token
    // IMPORTANT: Helper expects "id" not "browserId" - using wrong field causes "missing_id" error
    std::ostringstream ss;
    ss << "{\"type\":\"initBrowser\",\"id\":\"" << browserId << "\","
       << "\"url\":\"" << url << "\","
       << "\"width\":" << width << ",\"height\":" << height 
       << ",\"fps\":" << fps;
    if (!m_authToken.empty()) {
        ss << ",\"token\":\"" << m_authToken << "\"";
    }
    ss << "}";

    blog(LOG_INFO, "[browser-bridge] Sending initBrowser: %s", ss.str().c_str());

    if (!m_ipcClient->sendLine(ss.str())) {
        blog(LOG_ERROR, "[browser-bridge] Failed to send initBrowser for %s",
             browserId.c_str());
        return false;
    }

    blog(LOG_INFO, "[browser-bridge] Successfully sent initBrowser for %s (%dx%d @%dfps) url=%s",
         browserId.c_str(), width, height, fps, url.c_str());
    return true;
}

void BrowserBridgeManager::disposeBrowser(const std::string &browserId)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_running.load() || !m_ipcClient || !m_ipcClient->isConnected()) {
        return;
    }

    std::ostringstream ss;
    ss << "{\"type\":\"disposeBrowser\",\"id\":\"" << browserId << "\"";
    if (!m_authToken.empty()) {
        ss << ",\"token\":\"" << m_authToken << "\"";
    }
    ss << "}";
    m_ipcClient->sendLine(ss.str());

    blog(LOG_INFO, "[browser-bridge] Disposed browser %s", browserId.c_str());
}

bool BrowserBridgeManager::updateBrowser(const std::string &browserId,
                                          const std::string &url,
                                          int width, int height)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_running.load() || !m_ipcClient || !m_ipcClient->isConnected()) {
        blog(LOG_ERROR, "[browser-bridge] Cannot update browser - not connected");
        return false;
    }

    std::ostringstream ss;
    ss << "{\"type\":\"updateBrowser\",\"id\":\"" << browserId << "\"";
    if (!url.empty()) {
        ss << ",\"url\":\"" << url << "\"";
    }
    ss << ",\"width\":" << width << ",\"height\":" << height;
    if (!m_authToken.empty()) {
        ss << ",\"token\":\"" << m_authToken << "\"";
    }
    ss << "}";

    blog(LOG_INFO, "[browser-bridge] Sending updateBrowser: %s", ss.str().c_str());
    if (!m_ipcClient->sendLine(ss.str())) {
        blog(LOG_ERROR, "[browser-bridge] Failed to send updateBrowser for %s", browserId.c_str());
        return false;
    }

    blog(LOG_INFO, "[browser-bridge] Successfully sent updateBrowser for %s url=%s",
         browserId.c_str(), url.c_str());
    return true;
}

bool BrowserBridgeManager::isHelperRunning() const
{
#ifdef _WIN32
    return m_running.load() && m_helperProcess != nullptr;
#else
    return m_running.load() && m_helperPid > 0;
#endif
}

void BrowserBridgeManager::dispatchFrame(const std::string &browserId,
                                          const uint8_t *data, size_t size,
                                          int width, int height)
{
    // Only log periodically to avoid performance impact
    static int dispatchCount = 0;
    if (++dispatchCount % 300 == 1) {
        blog(LOG_INFO, "[browser-bridge] Dispatching frame #%d for %s (%dx%d)",
             dispatchCount, browserId.c_str(), width, height);
    }
    
    BrowserBridgeSource *source = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_sources.find(browserId);
        if (it != m_sources.end()) {
            source = it->second;
        } else {
            blog(LOG_WARNING, "[browser-bridge] No source found for browser %s", browserId.c_str());
        }
    }

    if (source) {
        source->receiveFrame(data, size, width, height);
    }
}

std::string BrowserBridgeManager::resolveHelperPath()
{
#ifdef __APPLE__
    // On macOS, helper is in the same Frameworks directory as the engine
    char pathBuf[PATH_MAX];
    uint32_t size = sizeof(pathBuf);
    if (_NSGetExecutablePath(pathBuf, &size) == 0) {
        fs::path execPath(pathBuf);
        fs::path bundlePath = execPath.parent_path().parent_path(); // .app/Contents
        fs::path helperBundle =
            bundlePath / "Frameworks" / BROWSER_HELPER_BUNDLE_NAME;
        if (fs::exists(helperBundle)) {
            return helperBundle.string();
        }
        // Check in Helpers directory (engine convention)
        fs::path helpersPath = bundlePath / "Helpers" / BROWSER_HELPER_BUNDLE_NAME;
        if (fs::exists(helpersPath)) {
            return helpersPath.string();
        }
        // Also check in build directory for development (relative to executable)
        fs::path devPath = execPath.parent_path() / ".." / "Helpers" / BROWSER_HELPER_BUNDLE_NAME;
        if (fs::exists(devPath)) {
            return fs::canonical(devPath).string();
        }
        // Try build/Helpers relative to engine executable
        fs::path buildHelpersPath = execPath.parent_path().parent_path() / "Helpers" / BROWSER_HELPER_BUNDLE_NAME;
        if (fs::exists(buildHelpersPath)) {
            return fs::canonical(buildHelpersPath).string();
        }
    }
#elif defined(_WIN32)
    // On Windows, helper exe is alongside the engine
    wchar_t pathBuf[MAX_PATH];
    if (GetModuleFileNameW(nullptr, pathBuf, MAX_PATH) > 0) {
        fs::path execPath(pathBuf);
        fs::path helperPath =
            execPath.parent_path() / BROWSER_HELPER_EXE_NAME;
        if (fs::exists(helperPath)) {
            return helperPath.string();
        }
    }
#else
    // Linux - check common locations
    fs::path helperPath = fs::path("/usr/lib/streamlumo") / BROWSER_HELPER_EXE_NAME;
    if (fs::exists(helperPath)) {
        return helperPath.string();
    }
    // Development path
    char pathBuf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", pathBuf, sizeof(pathBuf) - 1);
    if (len > 0) {
        pathBuf[len] = '\0';
        fs::path execPath(pathBuf);
        fs::path devPath =
            execPath.parent_path() / "browser-helper" / BROWSER_HELPER_EXE_NAME;
        if (fs::exists(devPath)) {
            return devPath.string();
        }
    }
#endif
    return "";
}

bool BrowserBridgeManager::launchHelper()
{
#ifdef __APPLE__
    // Build command line with port argument
    fs::path helperBinary =
        fs::path(m_helperPath) / "Contents" / "MacOS" / "streamlumo-browser-helper";
    std::string binaryStr = helperBinary.string();
    std::string portArg = "--port=" + std::to_string(m_port);
    const char *argv[] = {binaryStr.c_str(), portArg.c_str(), nullptr};

    pid_t pid;
    int status = posix_spawn(&pid, binaryStr.c_str(), nullptr, nullptr,
                             const_cast<char *const *>(argv), environ);
    if (status != 0) {
        blog(LOG_ERROR, "[browser-bridge] posix_spawn failed: %d", status);
        return false;
    }

    m_helperPid = pid;
    blog(LOG_INFO, "[browser-bridge] Launched helper pid=%d port=%u", pid, m_port);
    return true;

#elif defined(_WIN32)
    // Windows implementation
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};

    std::wstring cmdLine = std::wstring(m_helperPath.begin(), m_helperPath.end()) +
                           L" --port=" + std::to_wstring(m_port);

    if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        blog(LOG_ERROR, "[browser-bridge] CreateProcess failed: %lu",
             GetLastError());
        return false;
    }

    CloseHandle(pi.hThread);
    m_helperProcess = pi.hProcess;
    blog(LOG_INFO, "[browser-bridge] Launched helper pid=%lu port=%u", 
         pi.dwProcessId, m_port);
    return true;

#else
    // Linux
    std::string portArg = "--port=" + std::to_string(m_port);
    const char *argv[] = {m_helperPath.c_str(), portArg.c_str(), nullptr};

    pid_t pid;
    int status = posix_spawn(&pid, m_helperPath.c_str(), nullptr, nullptr,
                             const_cast<char *const *>(argv), environ);
    if (status != 0) {
        blog(LOG_ERROR, "[browser-bridge] posix_spawn failed: %d", status);
        return false;
    }

    m_helperPid = pid;
    blog(LOG_INFO, "[browser-bridge] Launched helper pid=%d port=%u", pid, m_port);
    return true;
#endif
}

void BrowserBridgeManager::stopHelper()
{
#ifdef _WIN32
    if (m_helperProcess) {
        TerminateProcess(m_helperProcess, 0);
        WaitForSingleObject(m_helperProcess, 3000);
        CloseHandle(m_helperProcess);
        m_helperProcess = nullptr;
    }
#else
    if (m_helperPid <= 0) {
        return;
    }
    kill(m_helperPid, SIGTERM);
    int status;
    waitpid(m_helperPid, &status, 0);
    m_helperPid = -1;
#endif

    blog(LOG_INFO, "[browser-bridge] Helper stopped");
}

} // namespace browser_bridge

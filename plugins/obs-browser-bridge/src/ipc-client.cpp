/**
 * @file ipc-client.cpp
 * @brief TCP IPC client for communication with the CEF browser helper
 * 
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 StreamLumo
 * 
 * ## Protocol Overview
 * 
 * The IPC protocol uses newline-delimited JSON messages over TCP:
 * - Each message is a single line of JSON followed by '\n'
 * - All messages include a "type" field indicating the message type
 * - Commands include a "token" field for authentication
 * - Browser operations use "id" field (NOT "browserId")
 * 
 * ## Message Types (Client → Helper)
 * 
 * - `handshake`: Initial authentication with token
 * - `initBrowser`: Create a new browser instance
 * - `disposeBrowser`: Destroy a browser instance
 * - `navigate`: Navigate to a new URL
 * - `executeJS`: Execute JavaScript in a browser
 * 
 * ## Message Types (Helper → Client)
 * 
 * - `browserCreated`: Confirmation of browser creation
 * - `frameReady`: Rendered frame data (base64-encoded BGRA)
 * - `error`: Error message with description
 * 
 * ## Frame Data Format
 * 
 * The `frameReady` message contains:
 * - `id`: Browser identifier (IMPORTANT: not "browserId")
 * - `width`: Frame width in pixels
 * - `height`: Frame height in pixels
 * - `data`: Base64-encoded BGRA pixel data
 * 
 * NOTE: The base64 data may contain JSON-escaped forward slashes (\/)
 * which must be unescaped before decoding. See frame-decoder.cpp.
 * 
 * ## Authentication
 * 
 * After connecting, the client must send a handshake with the token
 * from BROWSER_HELPER_TOKEN environment variable. All subsequent
 * commands must also include the token.
 * 
 * @see BrowserBridgeManager for connection management
 * @see FrameDecoder for base64 frame decoding
 */

#include "ipc-client.hpp"
#include "frame-decoder.hpp"
#include <obs.h>
#include <cstring>
#include <chrono>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#endif

namespace browser_bridge {

IPCClient::IPCClient()
{
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

IPCClient::~IPCClient()
{
    disconnect();
#ifdef _WIN32
    WSACleanup();
#endif
}

void IPCClient::setFrameCallback(FrameCallback callback)
{
    m_frameCallback = std::move(callback);
}

bool IPCClient::connect(const std::string &host, uint16_t port, int timeoutMs)
{
    if (m_connected.load()) {
        return true;
    }

    blog(LOG_INFO, "[ipc-client] Connecting to %s:%u", host.c_str(), port);

    // Retry loop for helper startup
    int retries = timeoutMs / 200;
    for (int i = 0; i < retries; ++i) {
#ifdef _WIN32
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            blog(LOG_ERROR, "[ipc-client] socket() failed");
            return false;
        }
#else
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            blog(LOG_ERROR, "[ipc-client] socket() failed: %s", strerror(errno));
            return false;
        }
#endif

        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

#ifdef _WIN32
        if (::connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            m_socket = reinterpret_cast<void *>(sock);
            m_fd = static_cast<int>(sock);
#else
        if (::connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            m_fd = sock;
            
            // Disable Nagle's algorithm for lower latency
            int flag = 1;
            setsockopt(m_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
#endif
            blog(LOG_INFO, "[ipc-client] Connected to helper");
            
            m_connected.store(true);
            m_running.store(true);
            
            // Start receive thread
            m_receiveThread = std::thread(&IPCClient::receiveLoop, this);
            
            return true;
        }

#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif

        // Wait before retry
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    blog(LOG_ERROR, "[ipc-client] Failed to connect after %d retries", retries);
    return false;
}

void IPCClient::disconnect()
{
    m_running.store(false);
    m_connected.store(false);
    
    if (m_fd >= 0) {
#ifdef _WIN32
        closesocket(static_cast<SOCKET>(m_socket));
        m_socket = nullptr;
#else
        // Shutdown socket to unblock any reads
        shutdown(m_fd, SHUT_RDWR);
        close(m_fd);
#endif
        m_fd = -1;
    }
    
    // Wait for receive thread
    if (m_receiveThread.joinable()) {
        m_receiveThread.join();
    }
    
    m_readBuffer.clear();
}

bool IPCClient::isConnected() const
{
    return m_connected.load();
}

/**
 * Sends authentication handshake to the browser helper.
 * 
 * This MUST be called immediately after connecting to the helper.
 * Without authentication, the helper will reject all subsequent
 * commands with an "unauthorized" error.
 * 
 * ## Handshake Protocol
 * 
 * Request:
 * ```json
 * {
 *   "type": "handshake",
 *   "client": "obs-browser-bridge",
 *   "token": "<auth-token>"
 * }
 * ```
 * 
 * Response (if successful):
 * ```json
 * {
 *   "type": "authenticated",
 *   "status": "ok"
 * }
 * ```
 * 
 * @param token Authentication token from BROWSER_HELPER_TOKEN env var
 * @return true if handshake sent successfully (does not wait for response)
 */
bool IPCClient::sendHandshake(const std::string &token)
{
    if (!isConnected()) {
        blog(LOG_ERROR, "[ipc-client] Cannot send handshake - not connected");
        return false;
    }

    // Build handshake JSON
    // Note: Simple string concatenation since we control the values
    std::string json = "{\"type\":\"handshake\",\"client\":\"obs-browser-bridge\"";
    if (!token.empty()) {
        json += ",\"token\":\"" + token + "\"";
    }
    json += "}";

    blog(LOG_INFO, "[ipc-client] Sending handshake with token=%s", 
         token.empty() ? "(none)" : "(provided)");
    
    return sendLine(json);
}

bool IPCClient::sendLine(const std::string &json)
{
    if (!isConnected()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_writeMutex);

    std::string data = json + "\n";
    const char *ptr = data.c_str();
    size_t remaining = data.length();

    while (remaining > 0) {
#ifdef _WIN32
        int n = send(static_cast<SOCKET>(m_socket), ptr,
                     static_cast<int>(remaining), 0);
#else
        ssize_t n = send(m_fd, ptr, remaining, 0);
#endif
        if (n <= 0) {
            blog(LOG_ERROR, "[ipc-client] send() failed");
            m_connected.store(false);
            return false;
        }
        ptr += n;
        remaining -= static_cast<size_t>(n);
    }

    return true;
}

void IPCClient::receiveLoop()
{
    blog(LOG_INFO, "[ipc-client] Receive loop started");
    
    char buf[262144]; // 256KB buffer for large frames
    
    while (m_running.load()) {
#ifdef _WIN32
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(static_cast<SOCKET>(m_socket), &readSet);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms
        int ret = select(0, &readSet, nullptr, nullptr, &tv);
#else
        struct pollfd pfd;
        pfd.fd = m_fd;
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 100); // 100ms timeout
#endif

        if (ret < 0) {
            if (!m_running.load()) break;
            blog(LOG_ERROR, "[ipc-client] poll/select failed");
            break;
        }
        
        if (ret == 0) {
            continue; // Timeout, check running flag
        }

#ifdef _WIN32
        int n = recv(static_cast<SOCKET>(m_socket), buf, sizeof(buf), 0);
#else
        ssize_t n = recv(m_fd, buf, sizeof(buf), 0);
#endif

        if (n <= 0) {
            if (!m_running.load()) break;
            blog(LOG_WARNING, "[ipc-client] Connection closed");
            m_connected.store(false);
            break;
        }

        m_readBuffer.append(buf, static_cast<size_t>(n));

        // Process complete lines
        size_t newlinePos;
        while ((newlinePos = m_readBuffer.find('\n')) != std::string::npos) {
            std::string line = m_readBuffer.substr(0, newlinePos);
            m_readBuffer.erase(0, newlinePos + 1);
            
            if (!line.empty()) {
                handleMessage(line);
            }
        }
        
        // Prevent buffer from growing too large (guard against malformed data)
        if (m_readBuffer.size() > 50 * 1024 * 1024) { // 50MB max
            blog(LOG_WARNING, "[ipc-client] Read buffer too large, clearing");
            m_readBuffer.clear();
        }
    }
    
    blog(LOG_INFO, "[ipc-client] Receive loop ended");
}

/**
 * Handles an incoming JSON message from the browser helper.
 * 
 * Currently processes:
 * - `frameReady`: Decoded and dispatched to BrowserBridgeSource
 * - `browserCreated`: Logged for confirmation
 * - `error`: Logged as warning
 * 
 * ## Field Naming Convention
 * 
 * IMPORTANT: The helper uses "id" (not "browserId") for browser identification.
 * This was a source of bugs during development. If you see "missing_id" errors
 * from the helper, check that you're using "id" in your JSON.
 * 
 * ## Frame Data Parsing
 * 
 * The base64-encoded frame data may contain JSON-escaped forward slashes (\/).
 * The frame decoder handles this unescaping before base64 decoding.
 * 
 * @param json The raw JSON message line (without trailing newline)
 */
void IPCClient::handleMessage(const std::string &json)
{
    // Simple JSON parsing - look for type field
    // In production, use nlohmann/json or similar
    
    // Lambda helper to extract string values from JSON
    auto findStringValue = [&json](const std::string &key) -> std::string {
        std::string search = "\"" + key + "\":\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return "";
        pos += search.length();
        size_t end = json.find('"', pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    };
    
    // Lambda helper to extract integer values from JSON
    auto findIntValue = [&json](const std::string &key) -> int {
        std::string search = "\"" + key + "\":";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return 0;
        pos += search.length();
        // Skip whitespace
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        size_t end = pos;
        while (end < json.length() && (json[end] >= '0' && json[end] <= '9')) end++;
        if (end == pos) return 0;
        return std::stoi(json.substr(pos, end - pos));
    };
    
    std::string type = findStringValue("type");
    
    if (type == "frameReady") {
        // Only log periodically to avoid performance impact
        static int frameCount = 0;
        if (++frameCount % 300 == 1) {
            blog(LOG_INFO, "[ipc-client] Received frameReady #%d", frameCount);
        }
        // IMPORTANT: Helper sends "id" not "browserId"
        // Using "browserId" here would cause findStringValue to return empty string
        // and frames would fail to dispatch to the correct source
        std::string browserId = findStringValue("id");
        int width = findIntValue("width");
        int height = findIntValue("height");
        
        // Frame details logging removed for performance
        
        // Extract data field (base64)
        // The data can be very large (1920x1080x4 = ~8MB base64)
        std::string dataKey = "\"data\":\"";
        size_t dataPos = json.find(dataKey);
        if (dataPos == std::string::npos) {
            blog(LOG_WARNING, "[ipc-client] frameReady missing data field");
            return;
        }
        dataPos += dataKey.length();
        size_t dataEnd = json.find('"', dataPos);
        if (dataEnd == std::string::npos) {
            blog(LOG_WARNING, "[ipc-client] frameReady data field not terminated");
            return;
        }
        std::string base64Data = json.substr(dataPos, dataEnd - dataPos);
        
        if (browserId.empty() || width <= 0 || height <= 0) {
            blog(LOG_WARNING, "[ipc-client] Invalid frameReady: id=%s w=%d h=%d",
                 browserId.c_str(), width, height);
            return;
        }
        
        // Decode base64 to BGRA
        // NOTE: frame_decoder handles JSON unescaping of \/ sequences
        std::vector<uint8_t> bgra;
        if (!frame_decoder::decodeBase64BGRA(base64Data, bgra)) {
            blog(LOG_WARNING, "[ipc-client] Failed to decode frame data (base64 len=%zu, first chars: %.20s...)",
                 base64Data.size(), base64Data.c_str());
            return;
        }
        
        // Verify size - should be exactly width * height * 4 (BGRA)
        size_t expectedSize = static_cast<size_t>(width) * height * 4;
        if (bgra.size() != expectedSize) {
            blog(LOG_WARNING, "[ipc-client] Frame size mismatch: got %zu expected %zu",
                 bgra.size(), expectedSize);
            return;
        }
        
        // Dispatch to callback
        if (m_frameCallback) {
            m_frameCallback(browserId, bgra.data(), bgra.size(), width, height);
        }
        
    } else if (type == "helper_ready") {
        blog(LOG_INFO, "[ipc-client] Received helper_ready");
    } else if (type == "browserReady") {
        // Helper sends "id" not "browserId"
        std::string browserId = findStringValue("id");
        blog(LOG_INFO, "[ipc-client] Browser ready: %s", browserId.c_str());
    } else if (type == "error") {
        std::string msg = findStringValue("message");
        blog(LOG_WARNING, "[ipc-client] Helper error: %s", msg.c_str());
    } else {
        // Unknown message type, log for debugging
        if (json.length() < 200) {
            blog(LOG_DEBUG, "[ipc-client] Unknown message: %s", json.c_str());
        }
    }
}

} // namespace browser_bridge

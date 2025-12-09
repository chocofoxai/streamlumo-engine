/**
 * @file ipc-client.hpp
 * @brief TCP IPC client for browser helper communication
 * 
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 StreamLumo
 *
 * ## Protocol
 * 
 * Communication uses newline-delimited JSON over TCP (port 4777).
 * Each message is a single JSON object followed by '\n'.
 * 
 * ## Authentication
 * 
 * After connecting, call sendHandshake() with the token from
 * BROWSER_HELPER_TOKEN. All commands must include this token.
 * 
 * ## Threading
 * 
 * The receive loop runs in a dedicated thread. Frames are delivered
 * via the callback on that thread, so the callback should be fast
 * or queue work for another thread.
 * 
 * ## Field Naming
 * 
 * IMPORTANT: The helper uses "id" (not "browserId") for browser
 * identification in all messages. Using the wrong field name causes
 * "missing_id" errors from the helper.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace browser_bridge {

// Callback type for received frames
// Parameters: browserId, BGRA data, size, width, height
using FrameCallback = std::function<void(const std::string &, const uint8_t *, 
                                          size_t, int, int)>;

class IPCClient {
public:
    IPCClient();
    ~IPCClient();

    // Non-copyable
    IPCClient(const IPCClient &) = delete;
    IPCClient &operator=(const IPCClient &) = delete;

    // Set frame callback before connect
    void setFrameCallback(FrameCallback callback);

    // Connect to helper on localhost
    bool connect(const std::string &host, uint16_t port, int timeoutMs = 5000);
    void disconnect();
    bool isConnected() const;

    /**
     * Sends authentication handshake to the browser helper.
     * 
     * MUST be called immediately after connect() succeeds.
     * The token comes from the BROWSER_HELPER_TOKEN environment variable.
     * Without this, all subsequent commands fail with "unauthorized".
     * 
     * @param token Authentication token
     * @return true if handshake was sent (does not wait for response)
     */
    bool sendHandshake(const std::string &token);

    // Send JSON line (adds newline automatically)
    bool sendLine(const std::string &json);

private:
    // Receive thread
    void receiveLoop();
    void handleMessage(const std::string &json);

    int m_fd{-1};
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_running{false};
    std::string m_readBuffer;
    std::mutex m_writeMutex;
    std::thread m_receiveThread;
    FrameCallback m_frameCallback;

#ifdef _WIN32
    void *m_socket{nullptr}; // SOCKET on Windows
#endif
};

} // namespace browser_bridge

// streamlumo-engine/src/browser_helper_client.h
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo

#pragma once

#include <cstdint>
#include <string>

namespace streamlumo {

class BrowserHelperClient {
public:
    BrowserHelperClient() = default;
    ~BrowserHelperClient();

    BrowserHelperClient(const BrowserHelperClient&) = delete;
    BrowserHelperClient& operator=(const BrowserHelperClient&) = delete;

    bool start(uint16_t port, const std::string &token);
    void stop();
    bool isConnected() const { return m_fd >= 0; }
    bool ping();
    
    // Request graceful shutdown of the browser helper
    bool sendShutdown();

private:
    bool connectSocket(uint16_t port);
    bool sendLine(const std::string &line);
    bool readLine(std::string &line, int timeoutMs);

    int m_fd{-1};
    std::string m_token;
};

} // namespace streamlumo

// streamlumo-engine/src/browser_helper_client.cpp
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo

#include "browser_helper_client.h"
#include "logging.h"

#if defined(__APPLE__)

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <cstring>
#include <string>
#include <thread>
#include <chrono>

namespace streamlumo {

BrowserHelperClient::~BrowserHelperClient()
{
    stop();
}

bool BrowserHelperClient::start(uint16_t port, const std::string &token)
{
    if (isConnected()) {
        return true;
    }

    m_token = token;

    constexpr int kMaxAttempts = 3;
    bool connected = false;
    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        if (connectSocket(port)) {
            connected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150 * attempt));
    }

    if (!connected) {
        log_warn("[helper] failed to connect to browser helper on port %u after retries", port);
        return false;
    }

    // Send a simple handshake and wait for ack to confirm connectivity.
    std::string handshake = "{\"type\":\"handshake\",\"client\":\"streamlumo-engine\"";
    if (!m_token.empty()) {
        handshake += ",\"token\":\"" + m_token + "\"";
    }
    handshake += "}\n";
    if (!sendLine(handshake)) {
        stop();
        return false;
    }

    std::string line;
    bool gotAck = false;
    for (int i = 0; i < 4; ++i) {
        if (!readLine(line, 500)) {
            continue;
        }
        log_info("[helper] received: %s", line.c_str());
        if (line.find("handshake_ack") != std::string::npos) {
            gotAck = true;
            break;
        }
    }
    if (!gotAck) {
        log_warn("[helper] handshake_ack not received");
    }

    // Send a ping to verify bidirectional flow.
    std::string ping = "{\"type\":\"ping\",\"client\":\"streamlumo-engine\"";
    if (!m_token.empty()) {
        ping += ",\"token\":\"" + m_token + "\"";
    }
    ping += "}\n";
    sendLine(ping);

    return true;
}

void BrowserHelperClient::stop()
{
    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
}

bool BrowserHelperClient::connectSocket(uint16_t port)
{
    m_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_fd < 0) {
        log_warn("[helper] socket() failed (errno=%d)", errno);
        return false;
    }

    int one = 1;
    setsockopt(m_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // Use a short timeout so engine startup does not hang if helper is absent.
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 300 * 1000; // 300ms
    setsockopt(m_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(m_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(m_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_warn("[helper] connect() failed (errno=%d)", errno);
        stop();
        return false;
    }

    log_info("[helper] connected to browser helper on 127.0.0.1:%u", port);
    return true;
}

bool BrowserHelperClient::sendLine(const std::string &line)
{
    if (m_fd < 0) {
        return false;
    }
    ssize_t n = send(m_fd, line.data(), line.size(), 0);
    if (n < 0) {
        log_warn("[helper] send() failed (errno=%d)", errno);
        return false;
    }
    return true;
}

bool BrowserHelperClient::readLine(std::string &line, int timeoutMs)
{
    if (m_fd < 0) {
        return false;
    }

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(m_fd, &readfds);

    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int ready = select(m_fd + 1, &readfds, nullptr, nullptr, &tv);
    if (ready <= 0) {
        return false;
    }

    char buffer[512];
    ssize_t n = recv(m_fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        stop();
        return false;
    }

    buffer[n] = '\0';
    line.assign(buffer);
    return true;
}

bool BrowserHelperClient::ping()
{
    if (m_fd < 0) {
        return false;
    }
    std::string ping = "{\"type\":\"ping\",\"client\":\"streamlumo-engine\"";
    if (!m_token.empty()) {
        ping += ",\"token\":\"" + m_token + "\"";
    }
    ping += "}\n";
    if (!sendLine(ping)) {
        stop();
        return false;
    }

    std::string line;
    if (!readLine(line, 500)) {
        stop();
        return false;
    }
    if (line.find("pong") == std::string::npos) {
        return false;
    }
    return true;
}

} // namespace streamlumo

#else

namespace streamlumo {

BrowserHelperClient::~BrowserHelperClient() = default;

bool BrowserHelperClient::start(uint16_t, const std::string &)
{
    return false;
}

void BrowserHelperClient::stop()
{
}

bool BrowserHelperClient::connectSocket(uint16_t)
{
    return false;
}

bool BrowserHelperClient::sendLine(const std::string &)
{
    return false;
}

bool BrowserHelperClient::readLine(std::string &, int)
{
    return false;
}

} // namespace streamlumo

#endif

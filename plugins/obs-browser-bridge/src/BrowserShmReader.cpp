// BrowserShmReader.cpp - Shared memory frame reader implementation
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo

#include "BrowserShmReader.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <obs-module.h>

namespace browser_bridge {

BrowserShmReader::BrowserShmReader(const std::string& browserId)
    : m_browserId(browserId)
{
    // Must match writer's naming convention
    m_shmName = "/streamlumo_browser_" + browserId;
}

BrowserShmReader::~BrowserShmReader() {
    disconnect();
}

bool BrowserShmReader::connect() {
    if (m_shmPtr) {
        // Already connected
        return true;
    }
    
    // Open existing shared memory (read-write for updating read_index)
    m_shmFd = shm_open(m_shmName.c_str(), O_RDWR, 0666);
    if (m_shmFd == -1) {
        // Not an error - writer may not have created it yet
        return false;
    }
    
    // Map to memory
    size_t totalSize = sizeof(BrowserFrameBufferReader);
    void* ptr = mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_shmFd, 0);
    if (ptr == MAP_FAILED) {
        blog(LOG_WARNING, "[BrowserShmReader] Failed to map SHM %s: %s",
             m_shmName.c_str(), strerror(errno));
        close(m_shmFd);
        m_shmFd = -1;
        return false;
    }
    
    m_shmPtr = static_cast<BrowserFrameBufferReader*>(ptr);
    
    // Initialize tracking
    m_lastReadIndex = m_shmPtr->write_index.load(std::memory_order_acquire);
    m_lastFrameCounter = m_shmPtr->frame_counter.load(std::memory_order_relaxed);
    
    blog(LOG_INFO, "[BrowserShmReader] Connected to SHM %s (%dx%d)",
         m_shmName.c_str(), m_shmPtr->width, m_shmPtr->height);
    
    return true;
}

bool BrowserShmReader::hasNewFrame() const {
    if (!m_shmPtr) {
        return false;
    }
    
    uint64_t currentFrameCounter = m_shmPtr->frame_counter.load(std::memory_order_acquire);
    return currentFrameCounter > m_lastFrameCounter;
}

bool BrowserShmReader::readFrame(void* buffer, size_t maxSize, int& outWidth, int& outHeight) {
    if (!m_shmPtr || !buffer) {
        return false;
    }
    
    // write_index points to the LAST COMPLETED write buffer
    uint64_t writeIndex = m_shmPtr->write_index.load(std::memory_order_acquire);
    
    // Check if we have a new frame (write index changed)
    uint64_t currentFrameCounter = m_shmPtr->frame_counter.load(std::memory_order_acquire);
    if (currentFrameCounter == m_lastFrameCounter) {
        // No new frame
        return false;
    }
    
    // Get frame metadata
    outWidth = static_cast<int>(m_shmPtr->width);
    outHeight = static_cast<int>(m_shmPtr->height);
    size_t frameSize = m_shmPtr->frame_size;
    
    if (frameSize > maxSize) {
        blog(LOG_WARNING, "[BrowserShmReader] Buffer too small: %zu < %zu", maxSize, frameSize);
        return false;
    }
    
    // Read from the last completed write buffer
    // This buffer is safe to read (writer has moved to next buffer)
    uint64_t readIndex = writeIndex;
    
    // Tell writer we're reading from this buffer so it doesn't overwrite
    m_shmPtr->read_index.store(readIndex, std::memory_order_release);
    
    // Copy frame data (safe because writer is on next buffer)
    const unsigned char* src = m_shmPtr->frames[readIndex];
    std::memcpy(buffer, src, frameSize);
    
    // Update tracking
    m_lastReadIndex = readIndex;
    m_lastFrameCounter = currentFrameCounter;
    
    return true;
}

void BrowserShmReader::disconnect() {
    if (m_shmPtr) {
        munmap(m_shmPtr, sizeof(BrowserFrameBufferReader));
        m_shmPtr = nullptr;
    }
    
    if (m_shmFd != -1) {
        close(m_shmFd);
        m_shmFd = -1;
    }
    
    blog(LOG_INFO, "[BrowserShmReader] Disconnected from SHM %s", m_shmName.c_str());
}

} // namespace browser_bridge

// BrowserShmWriter.mm - Shared memory frame writer implementation (macOS)
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo

#include "BrowserShmWriter.h"
#import <Foundation/Foundation.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <chrono>

namespace browser_bridge {

BrowserShmWriter::BrowserShmWriter(const std::string& browserId)
    : m_browserId(browserId)
{
    // Create unique SHM name for this browser
    // Format: /streamlumo_browser_<id>
    m_shmName = "/streamlumo_browser_" + browserId;
}

BrowserShmWriter::~BrowserShmWriter() {
    destroy();
}

bool BrowserShmWriter::create(int width, int height) {
    if (m_shmPtr) {
        // Already created
        return true;
    }
    
    m_width = width;
    m_height = height;
    
    // Calculate actual frame size for this resolution
    size_t frameSize = static_cast<size_t>(width) * height * SHM_FRAME_CHANNELS;
    size_t totalSize = sizeof(BrowserFrameBuffer);
    
    // Ensure frame fits in our buffer
    if (frameSize > SHM_FRAME_SIZE) {
        NSLog(@"[BrowserShmWriter] Frame too large: %dx%d (%zu bytes) > max %zu",
              width, height, frameSize, SHM_FRAME_SIZE);
        return false;
    }
    
    // Create shared memory
    m_shmFd = shm_open(m_shmName.c_str(), O_CREAT | O_RDWR, 0666);
    if (m_shmFd == -1) {
        NSLog(@"[BrowserShmWriter] Failed to create SHM %s: %s",
              m_shmName.c_str(), strerror(errno));
        return false;
    }
    
    // Set size
    if (ftruncate(m_shmFd, totalSize) == -1) {
        NSLog(@"[BrowserShmWriter] Failed to set SHM size: %s", strerror(errno));
        close(m_shmFd);
        m_shmFd = -1;
        return false;
    }
    
    // Map to memory
    void* ptr = mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_shmFd, 0);
    if (ptr == MAP_FAILED) {
        NSLog(@"[BrowserShmWriter] Failed to map SHM: %s", strerror(errno));
        close(m_shmFd);
        m_shmFd = -1;
        return false;
    }
    
    m_shmPtr = static_cast<BrowserFrameBuffer*>(ptr);
    
    // Initialize metadata
    m_shmPtr->write_index.store(0, std::memory_order_release);
    m_shmPtr->read_index.store(0, std::memory_order_release);
    m_shmPtr->width = width;
    m_shmPtr->height = height;
    m_shmPtr->frame_size = static_cast<uint32_t>(frameSize);
    m_shmPtr->format = 1;  // BGRA
    m_shmPtr->frame_counter.store(0, std::memory_order_release);
    m_shmPtr->dropped_frames.store(0, std::memory_order_release);
    m_shmPtr->last_write_timestamp_ns = 0;
    m_shmPtr->pause_requested.store(0, std::memory_order_release);
    m_shmPtr->producer_paused.store(0, std::memory_order_release);
    
    NSLog(@"[BrowserShmWriter] Created SHM %s (%dx%d, %zu bytes)",
          m_shmName.c_str(), width, height, totalSize);
    
    return true;
}

bool BrowserShmWriter::writeFrame(const void* buffer, int width, int height) {
    if (!m_shmPtr || !buffer) {
        return false;
    }
    
    size_t frameSize = static_cast<size_t>(width) * height * SHM_FRAME_CHANNELS;
    
    // Ensure frame fits
    if (frameSize > SHM_FRAME_SIZE) {
        return false;
    }
    
    // Update dimensions if changed
    if (m_shmPtr->width != static_cast<uint32_t>(width) || 
        m_shmPtr->height != static_cast<uint32_t>(height)) {
        m_shmPtr->width = width;
        m_shmPtr->height = height;
        m_shmPtr->frame_size = static_cast<uint32_t>(frameSize);
    }
    
    // Triple buffering: write_index points to the LAST COMPLETED write
    // We write to the next buffer
    uint64_t lastCompletedWrite = m_shmPtr->write_index.load(std::memory_order_acquire);
    uint64_t currentReadIndex = m_shmPtr->read_index.load(std::memory_order_acquire);
    uint64_t nextWriteIndex = (lastCompletedWrite + 1) % SHM_NUM_BUFFERS;
    
    // Check if we'd overwrite the buffer reader is currently using
    // With triple buffering (3 buffers), reader can be 1 frame behind
    if (nextWriteIndex == currentReadIndex) {
        // Reader is too slow, drop this frame to avoid tearing
        m_shmPtr->dropped_frames.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    
    // Copy frame data to next buffer
    unsigned char* dest = m_shmPtr->frames[nextWriteIndex];
    std::memcpy(dest, buffer, frameSize);
    
    // Update timestamp
    auto now = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    m_shmPtr->last_write_timestamp_ns = static_cast<uint64_t>(ns);
    
    // Publish write as completed (memory barrier ensures memcpy finished)
    m_shmPtr->write_index.store(nextWriteIndex, std::memory_order_release);
    
    // Update counter
    m_shmPtr->frame_counter.fetch_add(1, std::memory_order_relaxed);
    
    return true;
}

void BrowserShmWriter::destroy() {
    if (m_shmPtr) {
        munmap(m_shmPtr, sizeof(BrowserFrameBuffer));
        m_shmPtr = nullptr;
    }
    
    if (m_shmFd != -1) {
        close(m_shmFd);
        m_shmFd = -1;
    }
    
    // Unlink to cleanup
    shm_unlink(m_shmName.c_str());
    
    NSLog(@"[BrowserShmWriter] Destroyed SHM %s", m_shmName.c_str());
}

} // namespace browser_bridge

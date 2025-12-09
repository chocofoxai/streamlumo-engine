// BrowserShmWriter.h - Shared memory frame writer for browser sources
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo

#pragma once

#include <string>
#include <atomic>
#include <cstdint>

namespace browser_bridge {

// Frame buffer constants (matching native-shm)
constexpr int SHM_FRAME_WIDTH = 1920;
constexpr int SHM_FRAME_HEIGHT = 1080;
constexpr int SHM_FRAME_CHANNELS = 4;  // BGRA
constexpr size_t SHM_FRAME_SIZE = SHM_FRAME_WIDTH * SHM_FRAME_HEIGHT * SHM_FRAME_CHANNELS;
constexpr int SHM_NUM_BUFFERS = 3;  // Triple buffering

// Shared frame buffer structure (must match native-shm exactly)
struct BrowserFrameBuffer {
    std::atomic<uint64_t> write_index;
    std::atomic<uint64_t> read_index;
    uint32_t width;
    uint32_t height;
    uint32_t frame_size;
    uint32_t format;
    std::atomic<uint64_t> frame_counter;
    std::atomic<uint64_t> dropped_frames;
    std::atomic<uint64_t> last_write_timestamp_ns;
    std::atomic<uint8_t> pause_requested;
    std::atomic<uint8_t> producer_paused;
    uint8_t reserved[6];
    unsigned char frames[SHM_NUM_BUFFERS][SHM_FRAME_SIZE];
} __attribute__((aligned(64)));

/**
 * BrowserShmWriter - Writes browser frames to shared memory
 * 
 * Each browser source gets its own SHM region for zero-copy frame transfer.
 * The OBS plugin reads from this SHM to update textures.
 */
class BrowserShmWriter {
public:
    explicit BrowserShmWriter(const std::string& browserId);
    ~BrowserShmWriter();
    
    // Create shared memory region
    bool create(int width, int height);
    
    // Write frame to shared memory (called from OnPaint)
    bool writeFrame(const void* buffer, int width, int height);
    
    // Close and cleanup
    void destroy();
    
    // Get SHM name for this browser
    const std::string& getShmName() const { return m_shmName; }
    
    // Check if connected
    bool isCreated() const { return m_shmPtr != nullptr; }
    
private:
    std::string m_browserId;
    std::string m_shmName;
    int m_shmFd = -1;
    BrowserFrameBuffer* m_shmPtr = nullptr;
    int m_width = 0;
    int m_height = 0;
};

} // namespace browser_bridge

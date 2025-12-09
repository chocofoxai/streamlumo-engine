// BrowserShmReader.h - Shared memory frame reader for OBS browser bridge plugin
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2024 StreamLumo

#pragma once

#include <string>
#include <atomic>
#include <cstdint>

namespace browser_bridge {

// Must match BrowserShmWriter.h exactly
constexpr size_t SHM_FRAME_WIDTH = 1920;
constexpr size_t SHM_FRAME_HEIGHT = 1080;
constexpr size_t SHM_FRAME_CHANNELS_READER = 4;  // BGRA
constexpr size_t SHM_FRAME_SIZE_READER = SHM_FRAME_WIDTH * SHM_FRAME_HEIGHT * SHM_FRAME_CHANNELS_READER;
constexpr size_t SHM_NUM_BUFFERS_READER = 3;

// Shared memory structure (must match writer exactly)
struct BrowserFrameBufferReader {
    std::atomic<uint64_t> write_index;  // Current write buffer index (0-2)
    std::atomic<uint64_t> read_index;   // Current read buffer index (0-2)
    uint32_t width;                      // Current frame width
    uint32_t height;                     // Current frame height
    uint32_t frame_size;                 // Size of single frame in bytes
    uint32_t format;                     // Pixel format (1 = BGRA)
    std::atomic<uint64_t> frame_counter; // Total frames written
    std::atomic<uint64_t> dropped_frames; // Frames dropped by writer
    uint64_t last_write_timestamp_ns;    // Timestamp of last write (nanoseconds)
    std::atomic<uint32_t> pause_requested; // Reader requests pause
    std::atomic<uint32_t> producer_paused; // Producer acknowledges pause
    uint8_t reserved[24];                // Reserved for future use (alignment)
    
    // Triple-buffered frame data
    unsigned char frames[SHM_NUM_BUFFERS_READER][SHM_FRAME_SIZE_READER];
};

/**
 * @brief Reads video frames from shared memory written by browser-helper.
 * 
 * This is the OBS plugin side of the SHM transport, replacing the
 * TCP/JSON IPC client for frame data. Uses lock-free triple buffering.
 */
class BrowserShmReader {
public:
    explicit BrowserShmReader(const std::string& browserId);
    ~BrowserShmReader();
    
    // Non-copyable
    BrowserShmReader(const BrowserShmReader&) = delete;
    BrowserShmReader& operator=(const BrowserShmReader&) = delete;
    
    /**
     * @brief Connect to existing shared memory.
     * @return true on success, false on failure
     */
    bool connect();
    
    /**
     * @brief Check if a new frame is available.
     * @return true if new frame ready, false otherwise
     */
    bool hasNewFrame() const;
    
    /**
     * @brief Read the latest frame from shared memory.
     * @param buffer Destination buffer (must be at least frame_size bytes)
     * @param maxSize Maximum size of destination buffer
     * @param outWidth Receives frame width
     * @param outHeight Receives frame height
     * @return true on success, false on failure or no new frame
     */
    bool readFrame(void* buffer, size_t maxSize, int& outWidth, int& outHeight);
    
    /**
     * @brief Get current frame dimensions.
     */
    int getWidth() const { return m_shmPtr ? m_shmPtr->width : 0; }
    int getHeight() const { return m_shmPtr ? m_shmPtr->height : 0; }
    
    /**
     * @brief Get frame statistics.
     */
    uint64_t getFrameCounter() const { 
        return m_shmPtr ? m_shmPtr->frame_counter.load(std::memory_order_relaxed) : 0; 
    }
    uint64_t getDroppedFrames() const { 
        return m_shmPtr ? m_shmPtr->dropped_frames.load(std::memory_order_relaxed) : 0; 
    }
    
    /**
     * @brief Check if connected to shared memory.
     */
    bool isConnected() const { return m_shmPtr != nullptr; }
    
    /**
     * @brief Disconnect and cleanup.
     */
    void disconnect();
    
private:
    std::string m_browserId;
    std::string m_shmName;
    int m_shmFd = -1;
    BrowserFrameBufferReader* m_shmPtr = nullptr;
    uint64_t m_lastReadIndex = 0;
    uint64_t m_lastFrameCounter = 0;
};

} // namespace browser_bridge

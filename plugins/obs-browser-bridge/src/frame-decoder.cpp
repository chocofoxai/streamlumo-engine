/**
 * @file frame-decoder.cpp
 * @brief Base64 decoder for CEF browser frame data
 * 
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 StreamLumo
 * 
 * ## Frame Format
 * 
 * The browser helper sends rendered frames as base64-encoded BGRA pixel data
 * within JSON messages. The pixel format is:
 * - B: Blue channel (8 bits)
 * - G: Green channel (8 bits)
 * - R: Red channel (8 bits)
 * - A: Alpha channel (8 bits)
 * 
 * Total size = width * height * 4 bytes
 * 
 * ## JSON Escaping Issue
 * 
 * When the helper encodes the frame data as JSON, forward slashes (/) in the
 * base64 string may be escaped as \/. While this is valid JSON, the base64
 * decoder doesn't understand it and will fail.
 * 
 * Example:
 * - Raw base64: "abc/xyz"
 * - JSON encoded: "abc\/xyz" 
 * - We must convert back to: "abc/xyz" before decoding
 * 
 * This was a source of decode failures during development. The fix is to
 * scan for \/ sequences and replace them with / before base64 decoding.
 * 
 * ## Performance Considerations
 * 
 * For a 1920x1080 frame:
 * - Raw BGRA: 1920 * 1080 * 4 = ~8MB
 * - Base64: ~11MB (4/3 ratio)
 * 
 * This is inefficient for high frame rates. Future optimization should use
 * shared memory (IPC SHM) instead of base64 over TCP. See documentation:
 * docs/engine/browser-source-integration.md
 * 
 * @see IPCClient for frame reception
 * @see BrowserBridgeSource for texture upload
 */

#include "frame-decoder.hpp"
#include <stdexcept>

namespace browser_bridge {
namespace frame_decoder {

// Base64 decode lookup table
// Values 0-63 are valid base64 characters (A-Z, a-z, 0-9, +, /)
// Value 64 indicates invalid character (or padding '=')
static const uint8_t base64_table[256] = {
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
    64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
};

/**
 * Decodes base64-encoded BGRA frame data.
 * 
 * This function handles the JSON escaping issue where forward slashes
 * may be escaped as \/ in the base64 string. It unescapes these before
 * performing the base64 decode.
 * 
 * ## Why Unescape First?
 * 
 * The browser helper uses NSJSONSerialization on macOS which escapes
 * forward slashes in JSON strings. While this is technically valid JSON,
 * the base64 decoder sees '\' as an invalid character and fails.
 * 
 * Example decode failure without unescape:
 * - Input: "abc\/xyz" (JSON escaped)
 * - base64_table['\\'] = 64 (invalid)
 * - Decode fails!
 * 
 * With unescape:
 * - Input: "abc\/xyz" → "abc/xyz"
 * - base64_table['/'] = 63 (valid)
 * - Decode succeeds!
 * 
 * @param base64 The base64-encoded string (may contain \/ escapes)
 * @param output Output vector for decoded BGRA bytes
 * @return true on success, false on decode error
 */
bool decodeBase64BGRA(const std::string &base64, std::vector<uint8_t> &output) {
    // IMPORTANT: JSON encoding may escape / as \/ 
    // We must unescape before base64 decode or the decoder will fail
    // This was discovered during testing when frames failed to decode
    std::string unescaped;
    unescaped.reserve(base64.size());
    
    for (size_t i = 0; i < base64.size(); ++i) {
        if (base64[i] == '\\' && i + 1 < base64.size() && base64[i + 1] == '/') {
            unescaped.push_back('/');
            ++i; // Skip the escaped /
        } else {
            unescaped.push_back(base64[i]);
        }
    }
    
    size_t len = unescaped.size();
    if (len == 0) {
        output.clear();
        return true;
    }
    
    // Calculate output size (accounting for padding)
    size_t padding = 0;
    if (len >= 1 && unescaped[len - 1] == '=') padding++;
    if (len >= 2 && unescaped[len - 2] == '=') padding++;
    
    size_t outputLen = (len * 3) / 4 - padding;
    output.resize(outputLen);
    
    size_t i = 0;
    size_t j = 0;
    
    while (i + 4 <= len) {
        uint8_t a = base64_table[(uint8_t)unescaped[i++]];
        uint8_t b = base64_table[(uint8_t)unescaped[i++]];
        uint8_t c = base64_table[(uint8_t)unescaped[i++]];
        uint8_t d = base64_table[(uint8_t)unescaped[i++]];
        
        // Skip invalid characters
        if (a == 64 || b == 64) {
            return false;
        }
        
        // First byte
        if (j < outputLen) {
            output[j++] = (a << 2) | (b >> 4);
        }
        
        // Second byte (if not padding)
        if (c != 64 && j < outputLen) {
            output[j++] = (b << 4) | (c >> 2);
        }
        
        // Third byte (if not padding)
        if (d != 64 && j < outputLen) {
            output[j++] = (c << 6) | d;
        }
    }
    
    return true;
}

} // namespace frame_decoder
} // namespace browser_bridge
